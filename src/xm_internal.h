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
#include <stddef.h>
#include <stdbit.h>

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

#ifdef NDEBUG
#define assert(x) assume(x)
#else
#include <assert.h>
#endif

#define HAS_EFFECT(x) (!(((XM_DISABLED_EFFECTS) >> (x)) & 1))
#define HAS_VOLUME_EFFECT(x) (!(((XM_DISABLED_VOLUME_EFFECTS) >> (x)) & 1))
#define HAS_FEATURE(x) (!(((XM_DISABLED_FEATURES) >> (x)) & 1))

static_assert(_Generic((xm_sample_point_t){},
                        int8_t: true, int16_t: true, float: true,
                        default: false),
               "Unsupported value of XM_SAMPLE_TYPE");
static_assert(!(XM_LIBXM_DELTA_SAMPLES && _Generic((xm_sample_point_t){},
                                                    float: true,
                                                    default: false)),
               "XM_LIBXM_DELTA_SAMPLES cannot be used "
               "with XM_SAMPLE_TYPE=float");

static_assert(XM_LOOPING_TYPE >= 0 && XM_LOOPING_TYPE <= 2,
              "Invalid value of XM_LOOPING_TYPE");
static_assert(XM_LOOPING_TYPE != 1 || !HAS_EFFECT(0xB),
              "XM_LOOPING_TYPE=1 requires disabling Bxx (jump to order) effect");

static_assert(XM_SAMPLE_RATE >= 0 && XM_SAMPLE_RATE <= UINT16_MAX,
              "Unsupported value of XM_SAMPLE_RATE");

/* ----- Libxm constants ----- */

#define WAVEFORM_SINE 0
#define WAVEFORM_RAMP_DOWN 1
#define WAVEFORM_SQUARE 2
#define WAVEFORM_RAMP_UP 3

#define FEATURE_PINGPONG_LOOPS 0
#define FEATURE_NOTE_KEY_OFF 1
#define FEATURE_NOTE_SWITCH 2
#define FEATURE_MULTISAMPLE_INSTRUMENTS 3
#define FEATURE_VOLUME_ENVELOPES 4
#define FEATURE_PANNING_ENVELOPES 5
#define FEATURE_FADEOUT_VOLUME 6
#define FEATURE_AUTOVIBRATO 7
#define FEATURE_LINEAR_FREQUENCIES 8
#define FEATURE_AMIGA_FREQUENCIES 9
#define FEATURE_WAVEFORM_SINE (12|WAVEFORM_SINE)
#define FEATURE_WAVEFORM_RAMP_DOWN (12|WAVEFORM_RAMP_DOWN)
#define FEATURE_WAVEFORM_SQUARE (12|WAVEFORM_SQUARE)
#define FEATURE_WAVEFORM_RAMP_UP (12|WAVEFORM_RAMP_UP)
#define FEATURE_ACCURATE_SAMPLE_OFFSET_EFFECT 16
#define FEATURE_ACCURATE_ARPEGGIO_OVERFLOW 17
#define FEATURE_ACCURATE_ARPEGGIO_GLISSANDO 18
#define FEATURE_INVALID_INSTRUMENTS 19
#define FEATURE_INVALID_SAMPLES 20
#define FEATURE_INVALID_NOTES 21
#define FEATURE_CLAMP_PERIODS 22
#define FEATURE_SAMPLE_RELATIVE_NOTES 23
#define FEATURE_SAMPLE_FINETUNES 24
#define FEATURE_SAMPLE_PANNINGS 25
#define FEATURE_DEFAULT_GLOBAL_VOLUME 26
#define FEATURE_VARIABLE_TEMPO 27 /* 27..32 (5 bits) */
#define FEATURE_VARIABLE_BPM 32 /* 32..40 (8 bits) */
#define HAS_HARDCODED_TEMPO ((XM_DISABLED_FEATURES >> FEATURE_VARIABLE_TEMPO) & 31)
#define HAS_HARDCODED_BPM ((XM_DISABLED_FEATURES >> FEATURE_VARIABLE_BPM) & 255)
#define FEATURE_PANNING_COLUMN 40 /* Not vanilla XM. Applied after
                                     note/instrument, but before volume/effect
                                     columns */

static_assert(HAS_FEATURE(FEATURE_LINEAR_FREQUENCIES)
              || HAS_FEATURE(FEATURE_AMIGA_FREQUENCIES),
               "Must enable at least one frequency type (linear or Amiga)");

/* These are not a 1:1 match with XM semantics, rather, they are the values
   stored after a context has been loaded. */
#define EFFECT_ARPEGGIO 0
#define EFFECT_PORTAMENTO_UP 1
#define EFFECT_PORTAMENTO_DOWN 2
#define EFFECT_TONE_PORTAMENTO 3
#define EFFECT_VIBRATO 4
#define EFFECT_TONE_PORTAMENTO_VOLUME_SLIDE 5
#define EFFECT_VIBRATO_VOLUME_SLIDE 6
#define EFFECT_TREMOLO 7
#define EFFECT_SET_PANNING 8
#define EFFECT_SET_SAMPLE_OFFSET 9
#define EFFECT_VOLUME_SLIDE 0xA
#define EFFECT_JUMP_TO_ORDER 0xB
#define EFFECT_SET_VOLUME 0xC
#define EFFECT_PATTERN_BREAK 0xD
#define EFFECT_SET_TEMPO 0xE /* Remapped from vanilla XM */
#define EFFECT_SET_BPM 0xF
#define EFFECT_SET_GLOBAL_VOLUME 0x10
#define EFFECT_GLOBAL_VOLUME_SLIDE 0x11
#define EFFECT_EXTRA_FINE_PORTAMENTO_UP 0x12 /* Remapped vanilla XM */
#define EFFECT_EXTRA_FINE_PORTAMENTO_DOWN 0x13 /* Remapped vanilla XM */
#define EFFECT_KEY_OFF 0x14
#define EFFECT_SET_ENVELOPE_POSITION 0x15
#define EFFECT_FINE_VIBRATO 0x16 /* Not vanilla XM. Behaves like regular vibrato
                                    at quarter depth, sharing its effect memory.
                                    Used for S3M compatibility. */
/* 0x17, 0x18 unused */
#define EFFECT_PANNING_SLIDE 0x19
/* 0x1A unused */
#define EFFECT_MULTI_RETRIG_NOTE 0x1B
/* 0x1C unused */
#define EFFECT_TREMOR 0x1D
#define EFFECT_ROW_LOOP 0x1E /* Not vanilla XM. Behaves exactly like combined
                              E60 and E6y in the same slot. Used for S3M
                              compatibility. */
/* 0x1F, 0x20 unused */
#define EFFECT_FINE_PORTAMENTO_UP 0x21 /* Remapped from vanilla XM */
#define EFFECT_FINE_PORTAMENTO_DOWN 0x22 /* Remapped from vanilla XM */
#define EFFECT_SET_GLISSANDO_CONTROL 0x23 /* Remapped vanilla XM */
#define EFFECT_SET_VIBRATO_CONTROL 0x24 /* Remapped from vanilla XM */
#define EFFECT_SET_FINETUNE 0x25 /* Remapped from vanilla XM */
#define EFFECT_PATTERN_LOOP 0x26 /* Remapped from vanilla XM */
#define EFFECT_SET_TREMOLO_CONTROL 0x27 /* Remapped from vanilla XM */
/* 0x28 unused */
#define EFFECT_RETRIGGER_NOTE 0x29 /* Remapped from vanilla XM */
#define EFFECT_FINE_VOLUME_SLIDE_UP 0x2A /* Remapped from vanilla XM */
#define EFFECT_FINE_VOLUME_SLIDE_DOWN 0x2B /* Remapped from vanilla XM */
#define EFFECT_CUT_NOTE 0x2C /* Remapped from vanilla XM */
#define EFFECT_DELAY_NOTE 0x2D /* Remapped from vanilla XM */
#define EFFECT_DELAY_PATTERN 0x2E /* Remapped from vanilla XM */
/* 0x2F..=0xFF unused */

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
#define INSTRUMENT_NAME_LENGTH 32
#define MODULE_NAME_LENGTH 32
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
   timings of ticks. Worst case rounding is 1 frame (1/ctx->current_sample_rate
   second worth of audio) error every TICK_SUBSAMPLES ticks. A tick is at least
   0.01s long (255 BPM), so at 44100 Hz the error is 1/44100 second every 81.92
   seconds, or about 0.00003%. */
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

	#if HAS_FEATURE(FEATURE_PINGPONG_LOOPS)
	#define PING_PONG(smp) ((smp)->ping_pong)
	bool ping_pong: 1;
	static_assert(MAX_VOLUME < (1<<7));
	uint8_t volume:7; /* 0..=MAX_VOLUME  */
	#else
	#define PING_PONG(smp) false
	uint8_t volume;
	#endif

	#define HAS_PANNING (XM_PANNING_TYPE == 8)
	#define HAS_SAMPLE_PANNINGS (HAS_PANNING \
	                             && HAS_FEATURE(FEATURE_SAMPLE_PANNINGS))
	#if HAS_SAMPLE_PANNINGS
	#define PANNING(smp) ((smp)->panning)
	uint8_t panning; /* 0..MAX_PANNING */
	#else
	#define PANNING(smp) (MAX_PANNING/2)
	#endif

	#if HAS_FEATURE(FEATURE_SAMPLE_FINETUNES)
	#define FINETUNE(smp) ((smp)->finetune)
	int8_t finetune; /* -16..15 (-1 semitone..+15/16 semitone) */
	#else
	#define FINETUNE(smp) 0
	#endif

	#if HAS_FEATURE(FEATURE_SAMPLE_RELATIVE_NOTES)
	#define RELATIVE_NOTE(smp) ((smp)->relative_note)
	int8_t relative_note;
	#else
	#define RELATIVE_NOTE(smp) 0
	#endif

	#if XM_STRINGS
	static_assert(SAMPLE_NAME_LENGTH % 8 == 0);
	char name[SAMPLE_NAME_LENGTH];
	#endif

	#define SAMPLE_PADDING ( \
		!HAS_FEATURE(FEATURE_SAMPLE_FINETUNES) \
		+ !HAS_FEATURE(FEATURE_SAMPLE_RELATIVE_NOTES)	\
		+ !HAS_SAMPLE_PANNINGS)
	#if SAMPLE_PADDING % 4
	char __pad[SAMPLE_PADDING % 4];
	#endif
};
typedef struct xm_sample_s xm_sample_t;

#define HAS_SUSTAIN (HAS_FEATURE(FEATURE_NOTE_KEY_OFF) \
                     || HAS_EFFECT(EFFECT_KEY_OFF))
#define HAS_FADEOUT_VOLUME (HAS_FEATURE(FEATURE_FADEOUT_VOLUME) \
                            && HAS_SUSTAIN)
#define HAS_INSTRUMENTS (XM_TIMING_FUNCTIONS \
                         || HAS_FEATURE(FEATURE_VOLUME_ENVELOPES)     \
                         || (HAS_PANNING \
                             && HAS_FEATURE(FEATURE_PANNING_ENVELOPES)) \
                         || HAS_FEATURE(FEATURE_MULTISAMPLE_INSTRUMENTS) \
                         || HAS_FADEOUT_VOLUME \
                         || HAS_FEATURE(FEATURE_AUTOVIBRATO) \
                         || XM_MUTING_FUNCTIONS \
                         || XM_STRINGS)
#if HAS_INSTRUMENTS
struct xm_instrument_s {
	#if XM_TIMING_FUNCTIONS
	uint32_t latest_trigger;
	#endif

	#if HAS_FEATURE(FEATURE_VOLUME_ENVELOPES)
	xm_envelope_t volume_envelope;
	#endif

	#if HAS_PANNING && HAS_FEATURE(FEATURE_PANNING_ENVELOPES)
	xm_envelope_t panning_envelope;
	#endif

	#if HAS_FEATURE(FEATURE_MULTISAMPLE_INSTRUMENTS)
	static_assert(MAX_NOTE % 2 == 0);
	uint8_t sample_of_notes[MAX_NOTE];
	/* ctx->samples[index..(index+num_samples)] */
	uint16_t samples_index;
	#endif

	#if HAS_FADEOUT_VOLUME
	uint16_t volume_fadeout;
	#endif

	#if HAS_FEATURE(FEATURE_MULTISAMPLE_INSTRUMENTS)
	uint8_t num_samples;
	#endif

	#if HAS_FEATURE(FEATURE_AUTOVIBRATO)
	uint8_t vibrato_type;
	uint8_t vibrato_sweep;
	uint8_t vibrato_depth;
	uint8_t vibrato_rate;
	#endif

	#if XM_MUTING_FUNCTIONS
	#define INSTRUMENT_MUTED(inst) ((inst)->muted)
	bool muted;
	#else
	#define INSTRUMENT_MUTED(inst) false
	#endif

	#if XM_STRINGS
	static_assert(INSTRUMENT_NAME_LENGTH % 8 == 0);
	char name[INSTRUMENT_NAME_LENGTH];
	#endif

	#define INSTRUMENT_PADDING (2 \
		+ 4*!HAS_FEATURE(FEATURE_AUTOVIBRATO) \
		+ 2*!HAS_FADEOUT_VOLUME \
		+ (MAX_NOTE + 3)*!HAS_FEATURE(FEATURE_MULTISAMPLE_INSTRUMENTS) \
		+ !XM_MUTING_FUNCTIONS)
	#if INSTRUMENT_PADDING % 4
	char __pad[INSTRUMENT_PADDING % 4];
	#endif
};
#else
#define INSTRUMENT_MUTED(inst) false
struct xm_instrument_s;
#endif
typedef struct xm_instrument_s xm_instrument_t;

struct xm_pattern_slot_s {
	uint8_t note; /* 0..=MAX_NOTE or NOTE_KEY_OFF or NOTE_RETRIGGER */
	uint8_t instrument; /* 1..=128 */

	#define HAS_PANNING_COLUMN (HAS_FEATURE(FEATURE_PANNING_COLUMN) \
	                            && HAS_PANNING)
	#if HAS_PANNING_COLUMN
	#define PANNING_COLUMN(s) ((s)->panning_column)
	uint8_t panning_column; /* 1..=255, 0 = no effect */
	#else
	#define PANNING_COLUMN(s) 0
	#endif

	#define HAS_VOLUME_COLUMN ((~(XM_DISABLED_VOLUME_EFFECTS)) & \
	             (HAS_PANNING ? 0b1111111111111110 : 0b1000111111111110))
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

	#if HAS_INSTRUMENTS
	#define NUM_INSTRUMENTS(mod) ((mod)->num_instruments)
	uint8_t num_instruments;
	#else
	#define NUM_INSTRUMENTS(mod) ((uint8_t)(mod)->num_samples)
	#endif

	static_assert(PATTERN_ORDER_TABLE_LENGTH % 8 == 0);
	uint8_t pattern_table[PATTERN_ORDER_TABLE_LENGTH];

	#if XM_LOOPING_TYPE != 1
	#define MAYBE_RESTART_POT(ctx) do { \
			if((ctx)->current_table_index >= (ctx)->module.length) \
				(ctx)->current_table_index = \
					(ctx)->module.restart_position; \
			} while(0)
	uint8_t restart_position;
	#else
	#define MAYBE_RESTART_POT(ctx) do {} while(0)
	#endif

	#if XM_LOOPING_TYPE == 2
	#define MAX_LOOP_COUNT(mod) ((mod)->max_loop_count)
	uint8_t max_loop_count;
	#elif XM_LOOPING_TYPE == 1
	#define MAX_LOOP_COUNT(mod) 1
	#else
	#define MAX_LOOP_COUNT(mod) 0
	#endif

	/* DEFAULT_...(): These are the values stored in the loaded file, not
	   changed by any effects like Gxx, Fxx, etc. Without these, it's
	   impossible to properly implement xm_reset_context(). */

	#if HAS_HARDCODED_TEMPO
	#define DEFAULT_TEMPO(mod) ((uint8_t)HAS_HARDCODED_TEMPO)
	#else
	#define DEFAULT_TEMPO(mod) ((mod)->default_tempo)
	uint8_t default_tempo; /* 0..MIN_BPM */
	#endif

	#if HAS_HARDCODED_BPM
	#define DEFAULT_BPM(mod) ((uint8_t)HAS_HARDCODED_BPM)
	#else
	#define DEFAULT_BPM(mod) ((mod)->default_bpm)
	uint8_t default_bpm; /* MIN_BPM..=MAX_BPM */
	#endif

	#if HAS_FEATURE(FEATURE_DEFAULT_GLOBAL_VOLUME)
	#define DEFAULT_GLOBAL_VOLUME(mod) ((mod)->default_global_volume)
	uint8_t default_global_volume; /* 0..=MAX_VOLUME */
	#else
	#define DEFAULT_GLOBAL_VOLUME(mod) MAX_VOLUME
	#endif

	#if HAS_FEATURE(FEATURE_LINEAR_FREQUENCIES) \
		&& HAS_FEATURE(FEATURE_AMIGA_FREQUENCIES)
	#define AMIGA_FREQUENCIES(mod) ((mod)->amiga_frequencies)
	bool amiga_frequencies;
	#elif HAS_FEATURE(FEATURE_AMIGA_FREQUENCIES)
	#define AMIGA_FREQUENCIES(mod) true
	#else
	#define AMIGA_FREQUENCIES(mod) false
	#endif

	#if XM_STRINGS
	static_assert(MODULE_NAME_LENGTH % 8 == 0);
	static_assert(TRACKER_NAME_LENGTH % 8 == 0);
	char name[MODULE_NAME_LENGTH];
	char trackername[TRACKER_NAME_LENGTH];
	#endif

	#define MODULE_PADDING (2 \
		+ !(HAS_FEATURE(FEATURE_LINEAR_FREQUENCIES) \
		    && HAS_FEATURE(FEATURE_AMIGA_FREQUENCIES)) \
		+ !HAS_INSTRUMENTS \
		+ (XM_LOOPING_TYPE != 2) \
		+ (XM_LOOPING_TYPE == 1) \
		+ (HAS_HARDCODED_TEMPO > 0) \
		+ (HAS_HARDCODED_BPM > 0) \
		+ !HAS_FEATURE(FEATURE_DEFAULT_GLOBAL_VOLUME))
	#if MODULE_PADDING % POINTER_SIZE
	char __pad[MODULE_PADDING % POINTER_SIZE];
	#endif
};
typedef struct xm_module_s xm_module_t;

struct xm_channel_context_s {
	#if HAS_INSTRUMENTS
	#define INSTRUMENT(ch) ((ch)->instrument)
	xm_instrument_t* instrument; /* Last instrument triggered by a note.
	                                Could be NULL. */
	#else
	#define INSTRUMENT(ch) NULL
	#endif

	xm_sample_t* sample; /* Last sample triggered by a note. Could be
	                        NULL */
	xm_pattern_slot_t* current;

	#if XM_TIMING_FUNCTIONS
	uint32_t latest_trigger; /* In generated samples
	                            (1/ctx->current_sample_rate secs) */
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

	#define HAS_TONE_PORTAMENTO (HAS_EFFECT(EFFECT_TONE_PORTAMENTO) \
	                   || HAS_VOLUME_EFFECT(VOLUME_EFFECT_TONE_PORTAMENTO))
	#if HAS_TONE_PORTAMENTO
	uint16_t tone_portamento_target_period;
	#endif

	#if HAS_FADEOUT_VOLUME
	#define FADEOUT_VOLUME(ch) ((ch)->fadeout_volume)
	uint16_t fadeout_volume; /* 0..=MAX_FADEOUT_VOLUME */
	#else
	#define FADEOUT_VOLUME(ch) (MAX_FADEOUT_VOLUME-1)
	#endif

	#if HAS_FEATURE(FEATURE_AUTOVIBRATO)
	uint16_t autovibrato_ticks;
	#endif

	#if HAS_FEATURE(FEATURE_VOLUME_ENVELOPES)
	uint16_t volume_envelope_frame_count;
	#endif

	#if HAS_PANNING && HAS_FEATURE(FEATURE_PANNING_ENVELOPES)
	uint16_t panning_envelope_frame_count;
	#endif

	#if HAS_FEATURE(FEATURE_VOLUME_ENVELOPES)
	#define VOLUME_ENVELOPE_VOLUME(ch) ((ch)->volume_envelope_volume)
	uint8_t volume_envelope_volume; /* 0..=MAX_ENVELOPE_VALUE  */
	#else
	#define VOLUME_ENVELOPE_VOLUME(ch) MAX_ENVELOPE_VALUE
	#endif

	#if HAS_PANNING && HAS_FEATURE(FEATURE_PANNING_ENVELOPES)
	#define PANNING_ENVELOPE_PANNING(ch) ((ch)->panning_envelope_panning)
	uint8_t panning_envelope_panning; /* 0..=MAX_ENVELOPE_VALUE */
	#else
	#define PANNING_ENVELOPE_PANNING(ch) (MAX_ENVELOPE_VALUE / 2)
	#endif

	uint8_t volume; /* 0..=MAX_VOLUME */

	#define HAS_VOLUME_OFFSET (HAS_EFFECT(EFFECT_TREMOLO) \
	                           || HAS_EFFECT(EFFECT_TREMOR))
	#if HAS_VOLUME_OFFSET
	#define VOLUME_OFFSET(ch) ((ch)->volume_offset)
	#define RESET_VOLUME_OFFSET(ch) (ch)->volume_offset = 0
	int8_t volume_offset; /* -MIN_VOLUME..MAX_VOLUME. Reset by note trigger
                                   or by any volume command. Shared by 7xy:
                                   Tremolo and Txy: Tremor. */
	#else
	#define VOLUME_OFFSET(ch) 0
	#define RESET_VOLUME_OFFSET(ch)
	#endif

	#if HAS_PANNING
	uint8_t panning; /* 0..MAX_PANNING  */
	#endif

	uint8_t orig_note; /* Last valid note seen in a slot. Could be 0. */

	#define HAS_FINETUNES (HAS_FEATURE(FEATURE_SAMPLE_FINETUNES)	\
		|| HAS_EFFECT(EFFECT_SET_FINETUNE))
	#if HAS_FINETUNES
	int8_t finetune;
	#endif

	uint8_t next_instrument; /* Last instrument seen in the
	                            instrument column. Could be 0. */

	#define HAS_VOLUME_SLIDE (HAS_EFFECT(EFFECT_VOLUME_SLIDE) \
	                    || HAS_EFFECT(EFFECT_TONE_PORTAMENTO_VOLUME_SLIDE) \
	                    || HAS_EFFECT(EFFECT_VIBRATO_VOLUME_SLIDE))
	#if HAS_VOLUME_SLIDE
	uint8_t volume_slide_param;
	#endif

	#if HAS_EFFECT(EFFECT_FINE_VOLUME_SLIDE_UP)
	uint8_t fine_volume_slide_up_param;
	#endif

	#if HAS_EFFECT(EFFECT_FINE_VOLUME_SLIDE_DOWN)
	uint8_t fine_volume_slide_down_param;
	#endif

	#if HAS_EFFECT(EFFECT_GLOBAL_VOLUME_SLIDE)
	uint8_t global_volume_slide_param;
	#endif

	#if HAS_PANNING && HAS_EFFECT(EFFECT_PANNING_SLIDE)
	uint8_t panning_slide_param;
	#endif

	#if HAS_EFFECT(EFFECT_PORTAMENTO_UP)
	uint8_t portamento_up_param;
	#endif

	#if HAS_EFFECT(EFFECT_PORTAMENTO_DOWN)
	uint8_t portamento_down_param;
	#endif

	#if HAS_EFFECT(EFFECT_FINE_PORTAMENTO_UP)
	uint8_t fine_portamento_up_param;
	#endif

	#if HAS_EFFECT(EFFECT_FINE_PORTAMENTO_DOWN)
	uint8_t fine_portamento_down_param;
	#endif

	#if HAS_EFFECT(EFFECT_EXTRA_FINE_PORTAMENTO_UP)
	uint8_t extra_fine_portamento_up_param;
	#endif

	#if HAS_EFFECT(EFFECT_EXTRA_FINE_PORTAMENTO_DOWN)
	uint8_t extra_fine_portamento_down_param;
	#endif

	#define HAS_GLISSANDO_CONTROL ( \
		(HAS_EFFECT(EFFECT_ARPEGGIO) \
		    && HAS_FEATURE(FEATURE_ACCURATE_ARPEGGIO_GLISSANDO)) \
		|| (HAS_TONE_PORTAMENTO \
		    && HAS_EFFECT(EFFECT_SET_GLISSANDO_CONTROL)))
	#if HAS_GLISSANDO_CONTROL
	uint8_t glissando_control_param;
	int8_t glissando_control_error;
	#endif

	#if HAS_TONE_PORTAMENTO
	uint8_t tone_portamento_param;
	#endif

	#if HAS_EFFECT(EFFECT_MULTI_RETRIG_NOTE)
	uint8_t multi_retrig_param;
	uint8_t multi_retrig_ticks;
	#endif

	#define HAS_LOOPS (HAS_EFFECT(EFFECT_PATTERN_LOOP) \
	                   || HAS_EFFECT(EFFECT_ROW_LOOP))
	#if HAS_LOOPS
	uint8_t pattern_loop_origin; /* Where to restart a E6y loop */
	uint8_t pattern_loop_count; /* How many loop passes have been done */
	#endif

	#define HAS_SAMPLE_OFFSET_INVALID (HAS_EFFECT(EFFECT_SET_SAMPLE_OFFSET) \
	                 && HAS_FEATURE(FEATURE_ACCURATE_SAMPLE_OFFSET_EFFECT))
	#if HAS_EFFECT(EFFECT_SET_SAMPLE_OFFSET)
	uint8_t sample_offset_param;
	#endif

	#if HAS_SAMPLE_OFFSET_INVALID
	#define SAMPLE_OFFSET_INVALID(ch) ((ch)->sample_offset_invalid)
	bool sample_offset_invalid; /* Set to true when seeking beyond sample
	                               end; used by xm_next_of_sample() */
	#else
	#define SAMPLE_OFFSET_INVALID(ch) false
	#endif

	#if HAS_EFFECT(EFFECT_TREMOLO)
	uint8_t tremolo_param;
	uint8_t tremolo_ticks;
	#endif

	#if HAS_EFFECT(EFFECT_TREMOLO) && HAS_EFFECT(EFFECT_SET_TREMOLO_CONTROL)
	#define TREMOLO_CONTROL_PARAM(ch) ((ch)->tremolo_control_param)
	uint8_t tremolo_control_param;
	#else
	#define TREMOLO_CONTROL_PARAM(ch) 0
	#endif

	#define HAS_VIBRATO (HAS_EFFECT(EFFECT_VIBRATO) \
		|| HAS_EFFECT(EFFECT_FINE_VIBRATO) \
		|| HAS_VOLUME_EFFECT(VOLUME_EFFECT_VIBRATO))
	#define HAS_VIBRATO_RESET ((HAS_EFFECT(EFFECT_VIBRATO) \
	                            || HAS_EFFECT(EFFECT_FINE_VIBRATO) \
	                            || HAS_EFFECT(EFFECT_VIBRATO_VOLUME_SLIDE)) \
	                           && HAS_VOLUME_EFFECT(VOLUME_EFFECT_VIBRATO))
	#if HAS_VIBRATO
	#define VIBRATO_OFFSET(ch) ((ch)->vibrato_offset)
	uint8_t vibrato_param;
	uint8_t vibrato_ticks;
	int8_t vibrato_offset; /* in 1/64 semitone increments */
	#else
	#define VIBRATO_OFFSET(ch) 0
	#endif

	#if HAS_VIBRATO_RESET
	#define SHOULD_RESET_VIBRATO(ch) ((ch)->should_reset_vibrato)
	bool should_reset_vibrato;
	#elif HAS_VOLUME_EFFECT(VOLUME_EFFECT_VIBRATO)
	#define SHOULD_RESET_VIBRATO(ch) false
	#else
	#define SHOULD_RESET_VIBRATO(ch) true
	#endif

	#if HAS_VIBRATO && HAS_EFFECT(EFFECT_SET_VIBRATO_CONTROL)
	#define VIBRATO_CONTROL_PARAM(ch) ((ch)->vibrato_control_param)
	uint8_t vibrato_control_param;
	#else
	#define VIBRATO_CONTROL_PARAM(ch) 0
	#endif

	#if HAS_FEATURE(FEATURE_AUTOVIBRATO)
	#define AUTOVIBRATO_OFFSET(ch) ((ch)->autovibrato_offset)
	int8_t autovibrato_offset; /* in 1/64 semitone increments */
	#else
	#define AUTOVIBRATO_OFFSET(ch) 0
	#endif

	#define HAS_ARPEGGIO_RESET (HAS_EFFECT(EFFECT_ARPEGGIO) \
		&& HAS_FEATURE(FEATURE_ACCURATE_ARPEGGIO_GLISSANDO))
	#if HAS_ARPEGGIO_RESET
	bool should_reset_arpeggio;
	#endif

	#if HAS_EFFECT(EFFECT_ARPEGGIO)
	#define ARP_NOTE_OFFSET(ch) ((ch)->arp_note_offset)
	uint8_t arp_note_offset; /* in full semitones */
	#else
	#define ARP_NOTE_OFFSET(ch) 0
	#endif

	#if HAS_EFFECT(EFFECT_TREMOR)
	uint8_t tremor_param;
	uint8_t tremor_ticks; /* Decrements from max 16 */
	bool tremor_on;
	#endif

	#if HAS_SUSTAIN
	#define SUSTAINED(ch) ((ch)->sustained)
	bool sustained;
	#else
	#define SUSTAINED(ch) true
	#endif

	#if XM_MUTING_FUNCTIONS
	#define CHANNEL_MUTED(ch) ((ch)->muted)
	bool muted;
	#else
	#define CHANNEL_MUTED(ch) false
	#endif

	#define CHANNEL_CONTEXT_PADDING (4 \
		+ 4*!XM_TIMING_FUNCTIONS \
		+ 2*!HAS_EFFECT(EFFECT_MULTI_RETRIG_NOTE) \
		+ 3*!HAS_EFFECT(EFFECT_TREMOR) \
		+ !HAS_EFFECT(EFFECT_ARPEGGIO) \
		+ !HAS_ARPEGGIO_RESET \
		+ !HAS_EFFECT(EFFECT_GLOBAL_VOLUME_SLIDE) \
		+ !HAS_EFFECT(EFFECT_PORTAMENTO_UP) \
		+ !HAS_EFFECT(EFFECT_PORTAMENTO_DOWN) \
		+ 2*!HAS_EFFECT(EFFECT_TREMOLO) \
		+ !(HAS_EFFECT(EFFECT_TREMOLO) \
		       && HAS_EFFECT(EFFECT_SET_TREMOLO_CONTROL)) \
		+ !HAS_VOLUME_OFFSET \
		+ !HAS_VOLUME_SLIDE \
		+ 3*!HAS_VIBRATO \
		+ !(HAS_VIBRATO && HAS_VIBRATO_RESET) \
		+ !(HAS_VIBRATO && HAS_EFFECT(EFFECT_SET_VIBRATO_CONTROL)) \
		+ 3*!HAS_TONE_PORTAMENTO \
		+ 2*!HAS_GLISSANDO_CONTROL \
		+ !HAS_EFFECT(EFFECT_EXTRA_FINE_PORTAMENTO_UP) \
		+ !HAS_EFFECT(EFFECT_EXTRA_FINE_PORTAMENTO_DOWN) \
		+ !(HAS_PANNING && HAS_EFFECT(EFFECT_PANNING_SLIDE)) \
		+ 2*!HAS_LOOPS \
		+ !HAS_EFFECT(EFFECT_SET_SAMPLE_OFFSET) \
		+ !HAS_SAMPLE_OFFSET_INVALID \
		+ !HAS_EFFECT(EFFECT_FINE_VOLUME_SLIDE_UP) \
		+ !HAS_EFFECT(EFFECT_FINE_VOLUME_SLIDE_DOWN) \
		+ !HAS_EFFECT(EFFECT_FINE_PORTAMENTO_UP) \
		+ !HAS_EFFECT(EFFECT_FINE_PORTAMENTO_DOWN) \
		+ 3*!HAS_FEATURE(FEATURE_AUTOVIBRATO) \
		+ 2*!HAS_FADEOUT_VOLUME \
		+ 3*!HAS_FEATURE(FEATURE_VOLUME_ENVELOPES) \
		+ 3*!(HAS_PANNING && HAS_FEATURE(FEATURE_PANNING_ENVELOPES)) \
		+ !HAS_SUSTAIN \
		+ !XM_MUTING_FUNCTIONS \
		+ !HAS_PANNING \
		+ !HAS_FINETUNES)
	#if CHANNEL_CONTEXT_PADDING % POINTER_SIZE
	char __pad[CHANNEL_CONTEXT_PADDING % POINTER_SIZE];
	#endif
};
typedef struct xm_channel_context_s xm_channel_context_t;

struct xm_context_s {
	xm_pattern_t* patterns;
	xm_pattern_slot_t* pattern_slots;

	#if HAS_INSTRUMENTS
	xm_instrument_t* instruments; /* Instrument 1 has index 0,
	                               * instrument 2 has index 1, etc. */
	#endif

	xm_sample_t* samples;
	xm_sample_point_t* samples_data;
	xm_channel_context_t* channels;

	#if XM_LOOPING_TYPE == 2
	uint8_t* row_loop_count;
	#endif

	xm_module_t module;

	/* Anything below this *will* be zeroed by xm_reset_context(). Fields
	   that should not be zeroed belong in ctx->module. */

	uint32_t remaining_samples_in_tick; /* In 1/TICK_SUBSAMPLE increments */

	#if XM_TIMING_FUNCTIONS
	uint32_t generated_samples;
	#endif

	#if XM_SAMPLE_RATE == 0
	#define CURRENT_SAMPLE_RATE(ctx) ((ctx)->current_sample_rate)
	uint16_t current_sample_rate; /* Output sample rate, typically 44100 or
	                                 48000 */
	#else
	#define CURRENT_SAMPLE_RATE(ctx) ((uint16_t)XM_SAMPLE_RATE)
	#endif

	uint16_t current_table_index; /* 0..(module.length) */
	uint8_t current_tick; /* Typically 0..(ctx->tempo) */
	uint8_t current_row;

	#if HAS_EFFECT(EFFECT_DELAY_PATTERN)
	#define EXTRA_ROWS_DONE(ctx) ((ctx)->extra_rows_done)
	uint8_t extra_rows_done;
	uint8_t extra_rows;
	#else
	#define EXTRA_ROWS_DONE(ctx) 0
	#endif

	#define HAS_GLOBAL_VOLUME (HAS_EFFECT(EFFECT_SET_GLOBAL_VOLUME) \
	                           || HAS_EFFECT(EFFECT_GLOBAL_VOLUME_SLIDE))
	#if HAS_GLOBAL_VOLUME
	#define CURRENT_GLOBAL_VOLUME(ctx) ((ctx)->global_volume)
	uint8_t global_volume; /* 0..=MAX_VOLUME */
	#else
	#define CURRENT_GLOBAL_VOLUME(ctx) DEFAULT_GLOBAL_VOLUME(&ctx->module)
	#endif

	#if HAS_HARDCODED_TEMPO
	#define CURRENT_TEMPO(ctx) HAS_HARDCODED_TEMPO
	#elif HAS_EFFECT(EFFECT_SET_TEMPO)
	#define CURRENT_TEMPO(ctx) ((ctx)->current_tempo)
	uint8_t current_tempo; /* 0..MIN_BPM */
	#else
	#define CURRENT_TEMPO(ctx) DEFAULT_TEMPO(&ctx->module)
	#endif

	#if HAS_EFFECT(EFFECT_SET_BPM)
	#define CURRENT_BPM(ctx) ((ctx)->current_bpm)
	uint8_t current_bpm; /* MIN_BPM..=MAX_BPM */
	#else
	#define CURRENT_BPM(ctx) DEFAULT_BPM(&ctx->module)
	#endif

	#if HAS_EFFECT(EFFECT_PATTERN_BREAK)
	#define PATTERN_BREAK(ctx) ((ctx)->pattern_break)
	bool pattern_break;
	#else
	#define PATTERN_BREAK(ctx) false
	#endif

	#define HAS_POSITION_JUMP (HAS_EFFECT(EFFECT_JUMP_TO_ORDER) \
	                           || HAS_LOOPS)
	#if HAS_POSITION_JUMP
	#define POSITION_JUMP(ctx) ((ctx)->position_jump)
	bool position_jump;
	uint8_t jump_dest;
	#else
	#define POSITION_JUMP(ctx) false
	#endif

	#define HAS_JUMP_ROW (HAS_EFFECT(EFFECT_PATTERN_BREAK) \
	                      || HAS_POSITION_JUMP)
	#if HAS_JUMP_ROW
	uint8_t jump_row;
	#endif

	#if XM_LOOPING_TYPE == 2
	#define LOOP_COUNT(ctx) ((ctx)->loop_count)
	uint8_t loop_count;
	#elif XM_LOOPING_TYPE == 1
	#define LOOP_COUNT(ctx) (CURRENT_TEMPO(ctx) == 0 \
	           || ((ctx)->current_table_index >= (ctx)->module.length))
	#else
	#define LOOP_COUNT(ctx) 0
	#endif

	#define CONTEXT_PADDING (0 \
		+ 4*!XM_TIMING_FUNCTIONS \
		+ !HAS_GLOBAL_VOLUME \
		+ 2*!HAS_POSITION_JUMP \
		+ !HAS_EFFECT(EFFECT_PATTERN_BREAK) \
		+ !HAS_JUMP_ROW \
		+ 2*!HAS_EFFECT(EFFECT_DELAY_PATTERN) \
		+ !HAS_EFFECT(EFFECT_SET_TEMPO) \
		+ !HAS_EFFECT(EFFECT_SET_BPM) \
		+ (XM_LOOPING_TYPE != 2) \
		+ 2*(XM_SAMPLE_RATE != 0))
	#if CONTEXT_PADDING % POINTER_SIZE
	char __pad[CONTEXT_PADDING % POINTER_SIZE];
	#endif
};

/* ----- Internal functions ----- */

void xm_tick(xm_context_t*) __attribute__((nonnull)) __attribute__((visibility("hidden")));
void xm_print_pattern(xm_context_t*, uint8_t) __attribute((nonnull)) __attribute__((visibility("hidden")));
