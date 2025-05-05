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

#if XM_DEBUG || XM_DEFENSIVE
#include <stdio.h>
#define NOTICE(fmt, ...) do {                                           \
		fprintf(stderr, "%s(): " fmt "\n", __func__ __VA_OPT__(,) __VA_ARGS__); \
		fflush(stderr); \
	} while(0)
#else
#define NOTICE(...)
#endif

#if XM_DEBUG
#define DEBUG NOTICE
#else
#define DEBUG(...)
#endif

_Static_assert(!XM_BIG_ENDIAN, "Big endian support is still a WIP");
_Static_assert(XM_FREQUENCY_TYPES >= 1 && XM_FREQUENCY_TYPES <= 3,
               "Unsupported value of XM_FREQUENCY_TYPES");
_Static_assert(_Generic((xm_sample_point_t){},
                        int8_t: true, int16_t: true, float: true,
                        default: false),
               "Unsupported value of XM_SAMPLE_TYPE");
_Static_assert(!(XM_LIBXM_DELTA_SAMPLES && _Generic((xm_sample_point_t){},
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
#define RAMPING_POINTS 0x20

/* Not the original key off (97), this is the value used by libxm once a ctx
   has been loaded */
#define KEY_OFF_NOTE 128

/* ----- Data types ----- */

enum xm_waveform_type_e {
	XM_SINE_WAVEFORM = 0,
	XM_RAMP_DOWN_WAVEFORM = 1,
	XM_SQUARE_WAVEFORM = 2,
	XM_RANDOM_WAVEFORM = 3,
	XM_RAMP_UP_WAVEFORM = 4,
};
typedef enum xm_waveform_type_e xm_waveform_type_t;

struct xm_envelope_point_s {
	uint16_t frame;
	uint16_t value;
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
	float volume;
	float panning;
	enum {
		XM_NO_LOOP,
		XM_FORWARD_LOOP,
		XM_PING_PONG_LOOP,
	} loop_type;
	int8_t finetune;
	int8_t relative_note;

	#if XM_STRINGS
	char name[SAMPLE_NAME_LENGTH + 1];
	#else
	char __pad[1];
	#endif
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
	xm_waveform_type_t vibrato_type;
	uint8_t vibrato_sweep;
	uint8_t vibrato_depth;
	uint8_t vibrato_rate;
	bool muted;

	#if XM_STRINGS
	char name[INSTRUMENT_NAME_LENGTH + 1];
	#else
	char __pad[1];
	#endif
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

	#if XM_FREQUENCY_TYPES == 3
	enum {
		XM_LINEAR_FREQUENCIES,
		XM_AMIGA_FREQUENCIES,
	} frequency_type;
	#endif

	#if XM_STRINGS
	char name[MODULE_NAME_LENGTH + 1];
	char trackername[TRACKER_NAME_LENGTH + 1];
	#elif XM_FREQUENCY_TYPES == 3
	char __pad[3];
	#endif
};
typedef struct xm_module_s xm_module_t;

struct xm_channel_context_s {
	uint64_t latest_trigger;
	xm_instrument_t* instrument; /* Could be NULL */
	xm_sample_t* sample; /* Could be NULL */
	xm_pattern_slot_t* current;

	float note;
	float orig_note; /* The original note before effect modifications, as read in the pattern. */
	float vibrato_note_offset;
	float autovibrato_note_offset;

	float sample_position;
	float period;
	float tone_portamento_target_period;
	float frequency;
	float step;

	float volume; /* Ideally between 0 (muted) and 1 (loudest) */
	float panning; /* Between 0 (left) and 1 (right); 0.5 is centered */
	float actual_volume[2]; /* Multiplier for left/right channel */
	float tremolo_volume;
	float fadeout_volume;
	float volume_envelope_volume;
	float panning_envelope_panning;

	#if XM_RAMPING
	/* These values are updated at the end of each tick, to save
	 * a couple of float operations on every generated sample. */
	float target_volume[2];

	uint32_t frame_count; /* Gets reset after every note */
	float end_of_previous_sample[RAMPING_POINTS];
	#endif

	uint16_t vibrato_ticks; /* Position in the waveform */
	uint16_t autovibrato_ticks;
	uint16_t volume_envelope_frame_count;
	uint16_t panning_envelope_frame_count;

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
	uint8_t vibrato_param;
	uint8_t tremolo_param;
	uint8_t tremolo_ticks;
	uint8_t tremor_param;
	uint8_t sample_offset_param;

	xm_waveform_type_t vibrato_waveform;
	xm_waveform_type_t tremolo_waveform;

	bool ping; /* For ping-pong samples: true is -->, false is <-- */
	bool sustained;
	bool muted;
	bool arp_in_progress;
	bool vibrato_in_progress;
	bool tremor_on;
	/* True if a new note retriggers the waveform */
	bool vibrato_waveform_retrigger;
	bool tremolo_waveform_retrigger;
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

	float global_volume;
	float amplification;
	uint64_t generated_samples;
	uint32_t rate;
	uint16_t tempo;
	uint16_t bpm;

	#if XM_RAMPING
	/* How much is a channel final volume allowed to change per
	 * sample; this is used to avoid abrubt volume changes which
	 * manifest as "clicks" in the generated sound. */
	float volume_ramp;
	#endif

	float remaining_samples_in_tick;

	/* Can go beyond 255, with high tempo and a pattern delay */
	uint16_t current_tick;

	/* Extra ticks to be played before going to the next row -
	 * Used for EEy effect */
	uint16_t extra_ticks;

	uint8_t current_table_index;
	uint8_t current_row;

	bool position_jump;
	bool pattern_break;
	uint8_t jump_dest;
	uint8_t jump_row;

	uint8_t loop_count;
	uint8_t max_loop_count;
};
