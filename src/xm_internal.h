/* Author: Romain "Artefact2" Dalmaso <artefact2@gmail.com> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#include <xm.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <assert.h>
#include <stdckdint.h>

#if XM_DEFENSIVE
#include <stdio.h>
#define NOTICE(fmt, ...) do {                                           \
		fprintf(stderr, "%s(): " fmt "\n", __func__ __VA_OPT__(,) __VA_ARGS__); \
		fflush(stderr); \
	} while(0)
#else
#define NOTICE(...)
#endif

#if NDEBUG || !XM_DEFENSIVE
#define UNREACHABLE() __builtin_unreachable()
#else
#define UNREACHABLE() assert(0)
#endif

static_assert(XM_FREQUENCY_TYPES >= 1 && XM_FREQUENCY_TYPES <= 3,
               "Unsupported value of XM_FREQUENCY_TYPES");
static_assert(_Generic((xm_sample_point_t){},
                        int8_t: true, int16_t: true, float: true,
                        default: false),
               "Unsupported value of XM_SAMPLE_TYPE");
static_assert(!(XM_LIBXM_DELTA_SAMPLES && _Generic((xm_sample_point_t){},
                                                    float: true,
                                                    default: false)),
               "XM_LIBXM_DELTA_SAMPLES cannot be used "
               "with XM_SAMPLE_TYPE=float");

/* ----- XM constants ----- */

#define SAMPLE_NAME_LENGTH 22
#define INSTRUMENT_NAME_LENGTH 22
#define MODULE_NAME_LENGTH 20
#define TRACKER_NAME_LENGTH 20
#define PATTERN_ORDER_TABLE_LENGTH 256
#define NUM_NOTES 96
#define MAX_ENVELOPE_POINTS 12
#define MAX_ROWS_PER_PATTERN 256
#define RAMPING_POINTS 31
#define MAX_VOLUME 64
#define MAX_FADEOUT_VOLUME 32768
#define MAX_PANNING 256 /* cannot be stored in a uint8_t, this is ft2
                           behaviour */
#define MAX_ENVELOPE_VALUE 64
#define MIN_BPM 32
#define MAX_BPM 255

/* Not the original key off (97), this is the value used by libxm once a ctx
   has been loaded */
#define KEY_OFF_NOTE 128

/* How much is a channel final volume allowed to change per audio frame; this is
   used to avoid abrubt volume changes which manifest as "clicks" in the
   generated sound. */
#define RAMPING_VOLUME_RAMP (1.f/128.f)

/* Final amplification factor for the generated audio frames. This value is a
   compromise between too quiet output and clipping. */
#define AMPLIFICATION .25f

/* ----- Data types ----- */

struct xm_envelope_point_s {
	uint16_t frame;
	uint8_t value; /* 0..=MAX_ENVELOPE_VALUE */
	char __pad[1];
};
typedef struct xm_envelope_point_s xm_envelope_point_t;

struct xm_envelope_s {
	xm_envelope_point_t points[MAX_ENVELOPE_POINTS];
	uint8_t num_points;
	uint8_t sustain_point;
	uint8_t loop_start_point;
	uint8_t loop_end_point;
	bool enabled:1;
	bool sustain_enabled:1;
	bool loop_enabled:1;
	uint16_t __pad:13;
};
typedef struct xm_envelope_s xm_envelope_t;

struct xm_sample_s {
	uint64_t latest_trigger;
	/* ctx->samples_data[index..(index+length)] */
	uint32_t index;
	uint32_t length;
	uint32_t loop_start;
	uint32_t loop_length;
	uint32_t loop_end;
	uint8_t volume; /* 0..=MAX_VOLUME  */
	uint8_t panning; /* 0..MAX_PANNING */
	enum: uint8_t {
		XM_NO_LOOP = 0,
		XM_FORWARD_LOOP = 1,
		XM_PING_PONG_LOOP = 2,
	} loop_type;
	int8_t finetune;
	int8_t relative_note;

	#if XM_STRINGS
	/* Pad the name length to a multiple of 8, this makes struct packing
	   easier */
	static_assert(SAMPLE_NAME_LENGTH < 24); /* name[23] is for \0 */
	char name[24];
	#endif

	char __pad[7];
};
typedef struct xm_sample_s xm_sample_t;

struct xm_instrument_s {
	uint64_t latest_trigger;
	xm_envelope_t volume_envelope;
	xm_envelope_t panning_envelope;
	uint8_t sample_of_notes[NUM_NOTES];
	/* ctx->samples[index..(index+num_samples)] */
	uint16_t samples_index;
	uint16_t num_samples;
	uint16_t volume_fadeout;
	uint8_t vibrato_type;
	uint8_t vibrato_sweep;
	uint8_t vibrato_depth;
	uint8_t vibrato_rate;
	bool muted;

	#if XM_STRINGS
	static_assert(INSTRUMENT_NAME_LENGTH < 24);
	char name[24];
	#endif

	char __pad[1];
};
typedef struct xm_instrument_s xm_instrument_t;

struct xm_pattern_slot_s {
	uint8_t note; /* 1..=96 = Notes 0..=95, KEY_OFF_NOTE = Key Off */
	uint8_t instrument; /* 1..=128 */
	uint8_t volume_column;
	uint8_t effect_type;
	uint8_t effect_param;
};
typedef struct xm_pattern_slot_s xm_pattern_slot_t;

struct xm_pattern_s {
	/* ctx->pattern_slots[index..(index+num_rows)] */
	uint32_t slots_index;
	uint16_t num_rows;
	char __pad[2];
};
typedef struct xm_pattern_s xm_pattern_t;

struct xm_module_s {
	uint16_t length;
	uint16_t restart_position;
	uint16_t num_channels;
	uint16_t num_patterns;
	uint16_t num_instruments;
	uint16_t num_samples;
	uint32_t num_rows;
	uint32_t samples_data_length;
	uint8_t pattern_table[PATTERN_ORDER_TABLE_LENGTH];
	enum: uint8_t {
		XM_LINEAR_FREQUENCIES = 0,
		XM_AMIGA_FREQUENCIES = 1,
	} frequency_type;

	#if XM_STRINGS
	static_assert(MODULE_NAME_LENGTH < 24);
	static_assert(TRACKER_NAME_LENGTH < 24);
	char name[24];
	char trackername[24];
	#endif

	char __pad[3];
};
typedef struct xm_module_s xm_module_t;

struct xm_channel_context_s {
	uint64_t latest_trigger;
	xm_instrument_t* instrument; /* Could be NULL */
	xm_sample_t* sample; /* Could be NULL */
	xm_pattern_slot_t* current;

	float note;
	float orig_note; /* The original note before effect modifications, as
	                    read in the pattern. */

	float sample_position;
	float period;
	float tone_portamento_target_period;
	float frequency;
	float step;

	float actual_volume[2]; /* Multiplier for left/right channel */
	#if XM_RAMPING
	/* These values are updated at the end of each tick, to save
	 * a couple of float operations on every generated sample. */
	float target_volume[2];
	uint32_t frame_count; /* Gets reset after every note */
	static_assert(RAMPING_POINTS % 2 == 1);
	float end_of_previous_sample[RAMPING_POINTS];
	#endif
	uint16_t fadeout_volume; /* 0..=MAX_FADEOUT_VOLUME */

	uint16_t autovibrato_ticks;
	uint16_t volume_envelope_frame_count;
	uint16_t panning_envelope_frame_count;
	uint8_t volume_envelope_volume; /* 0..=MAX_ENVELOPE_VALUE  */
	uint8_t panning_envelope_panning; /* 0..=MAX_ENVELOPE_VALUE */

	uint8_t volume; /* 0..=MAX_VOLUME  */
	uint8_t panning; /* 0..MAX_PANNING  */

	int8_t autovibrato_note_offset; /* in 1/128 note increments */
	uint8_t arp_note_offset;
	uint8_t volume_slide_param;
	uint8_t fine_volume_slide_param;
	uint8_t global_volume_slide_param;
	uint8_t panning_slide_param;
	uint8_t portamento_up_param;
	uint8_t portamento_down_param;
	uint8_t fine_portamento_up_param;
	uint8_t fine_portamento_down_param;
	uint8_t extra_fine_portamento_up_param;
	uint8_t extra_fine_portamento_down_param;
	uint8_t tone_portamento_param;
	uint8_t multi_retrig_param;
	uint8_t note_delay_param;
	uint8_t pattern_loop_origin; /* Where to restart a E6y loop */
	uint8_t pattern_loop_count; /* How many loop passes have been done */
	uint8_t tremor_param;
	uint8_t sample_offset_param;

	uint8_t tremolo_param;
	uint8_t tremolo_control_param;
	uint8_t tremolo_ticks;
	int8_t tremolo_volume_offset; /* -64..63 */

	uint8_t vibrato_param;
	uint8_t vibrato_control_param;
	uint8_t vibrato_ticks;
	int8_t vibrato_note_offset; /* in 1/16 note increments */

	bool sustained;
	bool muted;
	bool should_reset_vibrato;
	bool tremor_on;

	char __pad[1];
};
typedef struct xm_channel_context_s xm_channel_context_t;

struct xm_context_s {
	xm_module_t module;
	xm_pattern_t* patterns;
	xm_pattern_slot_t* pattern_slots;
	xm_instrument_t* instruments; /* Instrument 1 has index 0,
	                               * instrument 2 has index 1, etc. */
	xm_sample_t* samples;
	xm_sample_point_t* samples_data;
	xm_channel_context_t* channels;
	uint8_t* row_loop_count;

	uint64_t generated_samples;

	float remaining_samples_in_tick;

	uint16_t rate; /* Output sample rate, typically 44100 or 48000 */

	uint8_t current_tick; /* Typically 0..(ctx->tempo) */
	uint8_t extra_rows; /* See EEy Pattern Delay effect */

	uint8_t tempo; /* 0..MIN_BPM */
	uint8_t bpm; /* MIN_BPM..=MAX_BPM */

	uint8_t global_volume; /* 0..=MAX_VOLUME */
	uint8_t current_table_index; /* 0..(module.length) */
	uint8_t current_row;

	bool position_jump;
	bool pattern_break;
	uint8_t jump_dest;
	uint8_t jump_row;

	uint8_t loop_count;
	uint8_t max_loop_count;

	char __pad[5];
};
