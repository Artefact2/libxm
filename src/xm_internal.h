/* Author: Romain "Artefact2" Dalmaso <artefact2@gmail.com> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#include <xm.h>
#include <math.h>
#include <string.h>
#include <stdckdint.h>

#define POINTER_SIZE (UINTPTR_MAX == UINT64_MAX ? 8 : 4)

#if XM_VERBOSE
#include <stdio.h>
#define NOTICE(fmt, ...) do {                                           \
		fprintf(stderr, "%s(): " fmt "\n", __func__ __VA_OPT__(,) __VA_ARGS__); \
		fflush(stderr); \
	} while(0)
#define TRACE(fmt, ...) NOTICE("pot %x row %x tick %x: " fmt, \
                               ctx->current_table_index, ctx->current_row, \
                               ctx->current_tick __VA_OPT__(,) __VA_ARGS__)
#else
#define NOTICE(...)
#define TRACE(...)
#endif

#define assume(x) do { if(!(x)) { __builtin_unreachable(); } } while(0)

#if NDEBUG
#define assert(x) assume(x)
#else
#include <assert.h>
#endif

#define HAS_EFFECT(x) (!(((XM_DISABLED_EFFECTS) >> (x)) & 1))

#define HAS_VOLUME_COLUMN ((~(XM_DISABLED_VOLUME_EFFECTS)) & 65534)
#define HAS_VOLUME_EFFECT(x) (!(((XM_DISABLED_VOLUME_EFFECTS) >> (x)) & 1))

static_assert(XM_FREQUENCY_TYPES >= 1 && XM_FREQUENCY_TYPES <= 3,
               "Unsupported value of XM_FREQUENCY_TYPES");
#if XM_FREQUENCY_TYPES == 1
#define AMIGA_FREQUENCIES(mod) false
#elif XM_FREQUENCY_TYPES == 2
#define AMIGA_FREQUENCIES(mod) true
#else
#define AMIGA_FREQUENCIES(mod) ((mod)->amiga_frequencies)
#endif

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

#define EFFECT_ARPEGGIO 0
#define EFFECT_TREMOLO 7
#define EFFECT_SET_GLOBAL_VOLUME 16
#define EFFECT_GLOBAL_VOLUME_SLIDE 17
#define EFFECT_MULTI_RETRIG_NOTE 27
#define EFFECT_TREMOR 29

#define VOLUME_EFFECT_SLIDE_DOWN 6
#define VOLUME_EFFECT_SLIDE_UP 7
#define VOLUME_EFFECT_FINE_SLIDE_DOWN 8
#define VOLUME_EFFECT_FINE_SLIDE_UP 9
#define VOLUME_EFFECT_VIBRATO_SPEED 0xA
#define VOLUME_EFFECT_VIBRATO 0xB
#define VOLUME_EFFECT_SET_PANNING 0xC
#define VOLUME_EFFECT_PANNING_SLIDE_LEFT 0xD
#define VOLUME_EFFECT_PANNING_SLIDE_RIGHT 0xE
#define VOLUME_EFFECT_TONE_PORTAMENTO 0xF

/* These are the lengths we store in the context, including the terminating
   NUL, not necessarily the lengths of strings in loaded formats. */
#define SAMPLE_NAME_LENGTH 24
#define INSTRUMENT_NAME_LENGTH 24
#define MODULE_NAME_LENGTH 24
#define TRACKER_NAME_LENGTH 24

#define PATTERN_ORDER_TABLE_LENGTH 256
#define MAX_NOTE 96
#define MAX_ENVELOPE_POINTS 12
#define MAX_ROWS_PER_PATTERN 256
#define RAMPING_POINTS 255
#define MAX_VOLUME 64
#define MAX_FADEOUT_VOLUME 32768
#define MAX_PANNING 256 /* cannot be stored in a uint8_t, this is ft2
                           behaviour */
#define MAX_ENVELOPE_VALUE 64
#define MIN_BPM 32
#define MAX_BPM 255
#define MAX_PATTERNS 256
#define MAX_INSTRUMENTS UINT8_MAX
#define MAX_CHANNELS UINT8_MAX
#define MAX_SAMPLES_PER_INSTRUMENT UINT8_MAX

/* Not the original key off (97), this is the value used by libxm once a ctx
   has been loaded */
#define NOTE_KEY_OFF 128

/* A special note in libxm, this acts like a regular note trigger of whatever
   note was last seen in the channel. Used by E90 retrigger effect. */
#define NOTE_RETRIGGER (MAX_NOTE+1)

/* A special note in libxm, this just immediately changes the sample based on
   the last instrument seen, without resetting the period or the sample
   position. Used for PT2-style ghost instruments. */
#define NOTE_SWITCH (MAX_NOTE+2)

/* How much is a channel final volume allowed to change per audio frame; this is
   used to avoid abrubt volume changes which manifest as "clicks" in the
   generated sound. */
#define RAMPING_VOLUME_RAMP (1.f/256.f)

/* Final amplification factor for the generated audio frames. This value is a
   compromise between too quiet output and clipping. */
#define AMPLIFICATION .25f

/* Granularity of sample count for ctx->remaining_samples_in_tick, for precise
   timings of ticks. Worst case rounding is 1 frame (1/ctx->rate second worth of
   audio) error every TICK_SUBSAMPLES ticks. A tick is at least 0.01s long (255
   BPM), so at 44100 Hz the error is 1/44100 second every 81.92 seconds, or
   about 0.00003%. */
#define TICK_SUBSAMPLES (1<<13)

/* Granularity of ch->step and ch->sample_position, for precise pitching of
   samples. Minimum sample step is about 0.008 per frame, at 65535 Hz, when
   playing C-0. For C-1 at 48000 Hz, the step is about 0.02.

   For example, with 2^12 microsteps, that means the worst pitch error is
   log2((.008 * 2^12 + 0.5)/(.008 * 2^12))*1200 = 26 cents. (Playing C-1 at 48000
   Hz, the error is 10 cents.) However, this only leaves 20 bits for the sample
   position, effectively limiting the maximum sample size to 1M frames. */
#define SAMPLE_MICROSTEPS (1<<XM_MICROSTEP_BITS)

#define MAX_SAMPLE_LENGTH (UINT32_MAX/SAMPLE_MICROSTEPS)

/* ----- Data types ----- */

struct xm_envelope_point_s {
	uint16_t frame;
	static_assert(MAX_ENVELOPE_VALUE < UINT8_MAX);
	uint8_t value; /* 0..=MAX_ENVELOPE_VALUE */
	char __pad[1];
};
typedef struct xm_envelope_point_s xm_envelope_point_t;

struct xm_envelope_s {
	xm_envelope_point_t points[MAX_ENVELOPE_POINTS];

	static_assert(MAX_ENVELOPE_POINTS + 128 < UINT8_MAX);
	uint8_t num_points; /* either 0 or 2..MAX_ENVELOPE_POINTS */
	uint8_t sustain_point;
	uint8_t loop_start_point;
	uint8_t loop_end_point;
};
typedef struct xm_envelope_s xm_envelope_t;

struct xm_sample_s {
	#if XM_TIMING_FUNCTIONS
	uint32_t latest_trigger;
	#endif

	/* ctx->samples_data[index..(index+length)] */
	uint32_t index;
	uint32_t length; /* same as loop_end (seeking beyond a loop with 9xx is
	                    invalid anyway) */
	uint32_t loop_length; /* is zero for sample without looping */
	bool ping_pong: 1;
	static_assert(MAX_VOLUME < (1<<7));
	uint8_t volume:7; /* 0..=MAX_VOLUME  */
	uint8_t panning; /* 0..MAX_PANNING */
	int8_t finetune; /* -16..15 (-1 semitone..+15/16 semitone) */
	int8_t relative_note;

	#if XM_STRINGS
	static_assert(SAMPLE_NAME_LENGTH % 8 == 0);
	char name[SAMPLE_NAME_LENGTH];
	#endif
};
typedef struct xm_sample_s xm_sample_t;

struct xm_instrument_s {
	#if XM_TIMING_FUNCTIONS
	uint32_t latest_trigger;
	#endif

	xm_envelope_t volume_envelope;
	xm_envelope_t panning_envelope;
	uint8_t sample_of_notes[MAX_NOTE];
	/* ctx->samples[index..(index+num_samples)] */
	uint16_t samples_index;
	uint16_t volume_fadeout;
	uint8_t num_samples;
	uint8_t vibrato_type;
	uint8_t vibrato_sweep;
	uint8_t vibrato_depth;
	uint8_t vibrato_rate;
	bool muted;

	#if XM_STRINGS
	static_assert(INSTRUMENT_NAME_LENGTH % 8 == 0);
	char name[INSTRUMENT_NAME_LENGTH];
	#endif

	char __pad[2];
};
typedef struct xm_instrument_s xm_instrument_t;

struct xm_pattern_slot_s {
	uint8_t note; /* 0..=MAX_NOTE or NOTE_KEY_OFF or NOTE_RETRIGGER */
	uint8_t instrument; /* 1..=128 */

	#if HAS_VOLUME_COLUMN
	#define VOLUME_COLUMN(s) ((s)->volume_column)
	uint8_t volume_column;
	#else
	#define VOLUME_COLUMN(s) 0
	#endif

	uint8_t effect_type;
	uint8_t effect_param;
};
typedef struct xm_pattern_slot_s xm_pattern_slot_t;

struct xm_pattern_s {
	/* ctx->pattern_slots[index*num_chans..(index+num_rows)*num_chans] */
	static_assert((MAX_PATTERNS - 1) * MAX_ROWS_PER_PATTERN < UINT16_MAX);
	uint16_t rows_index;
	uint16_t num_rows;
};
typedef struct xm_pattern_s xm_pattern_t;

struct xm_module_s {
	uint32_t samples_data_length;
	uint32_t num_rows;
	uint16_t length;
	uint16_t num_patterns;
	uint16_t num_samples;
	uint8_t num_channels;
	uint8_t num_instruments;
	uint8_t pattern_table[PATTERN_ORDER_TABLE_LENGTH];
	uint8_t restart_position;

	bool amiga_frequencies;

	#if XM_STRINGS
	static_assert(MODULE_NAME_LENGTH % 8 == 0);
	static_assert(TRACKER_NAME_LENGTH % 8 == 0);
	char name[MODULE_NAME_LENGTH];
	char trackername[TRACKER_NAME_LENGTH];
	#endif

	char __pad[2];
};
typedef struct xm_module_s xm_module_t;

struct xm_channel_context_s {
	xm_instrument_t* instrument; /* Last instrument triggered by a note.
	                                Could be NULL. */
	xm_sample_t* sample; /* Last sample triggered by a note. Could be
	                        NULL */
	xm_pattern_slot_t* current;

	#if XM_TIMING_FUNCTIONS
	uint32_t latest_trigger; /* In generated samples (1/ctx->rate secs) */
	#endif

	uint32_t sample_position; /* In microsteps */
	uint32_t step; /* In microsteps */

	float actual_volume[2]; /* Multiplier for left/right channel */
	#if XM_RAMPING
	/* These values are updated at the end of each tick, to save
	 * a couple of float operations on every generated sample. */
	float target_volume[2];
	uint32_t frame_count; /* Gets reset after every note */
	static_assert(RAMPING_POINTS % 2 == 1);
	float end_of_previous_sample[RAMPING_POINTS];
	#endif

	uint16_t period; /* 1/64 semitone increments (linear frequencies) */
	uint16_t tone_portamento_target_period;

	uint16_t fadeout_volume; /* 0..=MAX_FADEOUT_VOLUME */
	uint16_t autovibrato_ticks;
	uint16_t volume_envelope_frame_count;
	uint16_t panning_envelope_frame_count;
	uint8_t volume_envelope_volume; /* 0..=MAX_ENVELOPE_VALUE  */
	uint8_t panning_envelope_panning; /* 0..=MAX_ENVELOPE_VALUE */

	uint8_t volume; /* 0..=MAX_VOLUME */
	int8_t volume_offset; /* -MIN_VOLUME..MAX_VOLUME. Reset by note trigger
                                   or by any volume command. Shared by 7xy:
                                   Tremolo and Txy: Tremor. */
	uint8_t panning; /* 0..MAX_PANNING  */
	uint8_t orig_note; /* Last valid note seen in a slot. Could be 0. */
	int8_t finetune;
	uint8_t next_instrument; /* Last instrument seen in the
	                            instrument column. Could be 0. */

	uint8_t volume_slide_param;
	uint8_t fine_volume_slide_up_param;
	uint8_t fine_volume_slide_down_param;

	#if HAS_EFFECT(EFFECT_GLOBAL_VOLUME_SLIDE)
	uint8_t global_volume_slide_param;
	#endif

	uint8_t panning_slide_param;
	uint8_t portamento_up_param;
	uint8_t portamento_down_param;
	uint8_t fine_portamento_up_param;
	uint8_t fine_portamento_down_param;
	uint8_t extra_fine_portamento_up_param;
	uint8_t extra_fine_portamento_down_param;
	uint8_t glissando_control_param;
	int8_t glissando_control_error;
	uint8_t tone_portamento_param;

	#if HAS_EFFECT(EFFECT_MULTI_RETRIG_NOTE)
	uint8_t multi_retrig_param;
	uint8_t multi_retrig_ticks;
	#endif

	uint8_t pattern_loop_origin; /* Where to restart a E6y loop */
	uint8_t pattern_loop_count; /* How many loop passes have been done */
	uint8_t sample_offset_param;

	uint8_t tremolo_param;
	uint8_t tremolo_control_param;
	uint8_t tremolo_ticks; /* Mod 0x40, so wraparound is fine. XXX: is it
	                          shared with vibrato ticks? tremor ticks? */

	uint8_t vibrato_param;
	uint8_t vibrato_control_param;
	uint8_t vibrato_ticks;
	int8_t vibrato_offset; /* in 1/64 semitone increments */

	#define HAS_VIBRATO_RESET ((HAS_EFFECT(4) || HAS_EFFECT(6)) \
		&& HAS_VOLUME_EFFECT(VOLUME_EFFECT_VIBRATO))
	#if HAS_VIBRATO_RESET
	#define SHOULD_RESET_VIBRATO(ch) ((ch)->should_reset_vibrato)
	bool should_reset_vibrato;
	#elif HAS_VOLUME_EFFECT(VOLUME_EFFECT_VIBRATO)
	#define SHOULD_RESET_VIBRATO(ch) false
	#else
	#define SHOULD_RESET_VIBRATO(ch) true
	#endif

	int8_t autovibrato_offset; /* in 1/64 semitone increments */

	#if HAS_EFFECT(EFFECT_ARPEGGIO)
	#define ARP_NOTE_OFFSET(ch) ((ch)->arp_note_offset)
	bool should_reset_arpeggio;
	uint8_t arp_note_offset; /* in full semitones */
	#else
	#define ARP_NOTE_OFFSET(ch) 0
	#endif

	#if HAS_EFFECT(EFFECT_TREMOR)
	uint8_t tremor_param;
	uint8_t tremor_ticks; /* Decrements from max 16 */
	bool tremor_on;
	#endif

	bool sustained;
	bool muted;

	#define CHANNEL_CONTEXT_PADDING (1 \
		+ 4*XM_TIMING_FUNCTIONS \
		+ !HAS_VIBRATO_RESET \
		+ 2*!HAS_EFFECT(EFFECT_MULTI_RETRIG_NOTE) \
		+ 3*!HAS_EFFECT(EFFECT_TREMOR) \
		+ 2*!HAS_EFFECT(EFFECT_ARPEGGIO) \
		+ !HAS_EFFECT(EFFECT_GLOBAL_VOLUME_SLIDE))
	#if CHANNEL_CONTEXT_PADDING % POINTER_SIZE
	char __pad[CHANNEL_CONTEXT_PADDING % POINTER_SIZE];
	#endif
};
typedef struct xm_channel_context_s xm_channel_context_t;

struct xm_context_s {
	xm_pattern_t* patterns;
	xm_pattern_slot_t* pattern_slots;
	xm_instrument_t* instruments; /* Instrument 1 has index 0,
	                               * instrument 2 has index 1, etc. */
	xm_sample_t* samples;
	xm_sample_point_t* samples_data;
	xm_channel_context_t* channels;
	uint8_t* row_loop_count;

	xm_module_t module;

	#if XM_TIMING_FUNCTIONS
	uint32_t generated_samples;
	#endif

	uint32_t remaining_samples_in_tick; /* In 1/TICK_SUBSAMPLE increments */

	uint16_t rate; /* Output sample rate, typically 44100 or 48000 */

	uint8_t current_tick; /* Typically 0..(ctx->tempo) */
	uint8_t extra_rows_done;
	uint8_t current_row;
	uint8_t extra_rows;

	uint8_t current_table_index; /* 0..(module.length) */

	#define HAS_GLOBAL_VOLUME (HAS_EFFECT(EFFECT_SET_GLOBAL_VOLUME) \
	                           || HAS_EFFECT(EFFECT_GLOBAL_VOLUME_SLIDE))
	#if HAS_GLOBAL_VOLUME
	#define GLOBAL_VOLUME(ctx) ((ctx)->global_volume)
	uint8_t global_volume; /* 0..=MAX_VOLUME */
	#else
	#define GLOBAL_VOLUME(ctx) MAX_VOLUME
	#endif

	uint8_t tempo; /* 0..MIN_BPM */
	uint8_t bpm; /* MIN_BPM..=MAX_BPM */

	bool position_jump;
	bool pattern_break;
	uint8_t jump_dest;
	uint8_t jump_row;

	uint8_t loop_count;
	uint8_t max_loop_count;

	#define CONTEXT_PADDING (4*XM_TIMING_FUNCTIONS\
		+ !HAS_GLOBAL_VOLUME)
	#if CONTEXT_PADDING % POINTER_SIZE
	char __pad[CONTEXT_PADDING % POINTER_SIZE];
	#endif
};
