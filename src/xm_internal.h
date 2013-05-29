/* Author: Romain "Artefact2" Dalmaso <artefact2@gmail.com> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#include <xm.h>
#include <stdbool.h>
#include <math.h>
#include <string.h>

#if XM_DEBUG
#include <stdio.h>
#define DEBUG(fmt, ...) fprintf(stderr, "%s(): " fmt "\n", __func__, __VA_ARGS__), fflush(stderr)
#else
#define DEBUG(...)
#endif

/* ----- XM constants ----- */

#define SAMPLE_NAME_LENGTH 22
#define INSTRUMENT_NAME_LENGTH 22
#define MODULE_NAME_LENGTH 20
#define TRACKER_NAME_LENGTH 20
#define PATTERN_ORDER_TABLE_LENGTH 256
#define NUM_NOTES 96
#define NUM_ENVELOPE_POINTS 12
#define MAX_NUM_ROWS 256

/* ----- Data types ----- */

enum xm_loop_type_e {
	XM_NO_LOOP,
	XM_FORWARD_LOOP,
	XM_PING_PONG_LOOP,
};
typedef enum xm_loop_type_e xm_loop_type_t;

enum xm_frequency_type_e {
	XM_LINEAR_FREQUENCIES,
	XM_AMIGA_FREQUENCIES,
};
typedef enum xm_frequency_type_e xm_frequency_type_t;

struct xm_envelope_point_s {
	uint16_t frame;
	uint16_t value;
};
typedef struct xm_envelope_point_s xm_envelope_point_t;

struct xm_envelope_s {
	xm_envelope_point_t points[NUM_ENVELOPE_POINTS];
	uint8_t num_points;
	uint8_t sustain_point;
	uint8_t loop_start_point;
	uint8_t loop_end_point;
	bool enabled;
	bool sustain_enabled;
	bool loop_enabled;
};
typedef struct xm_envelope_s xm_envelope_t;

struct xm_sample_s {
	char name[SAMPLE_NAME_LENGTH];

	uint32_t length;
	uint32_t loop_start;
	uint32_t loop_length;
	float volume;
	int8_t finetune;
	xm_loop_type_t loop_type;
	float panning;
	int8_t relative_note;

	float* data;
 };
 typedef struct xm_sample_s xm_sample_t;

 struct xm_instrument_s {
	 char name[INSTRUMENT_NAME_LENGTH];
	 uint16_t num_samples;
	 uint8_t sample_of_notes[NUM_NOTES];
	 xm_envelope_t volume_envelope;
	 xm_envelope_t panning_envelope;
	 uint8_t vibrato_type;
	 uint8_t vibrato_sweep;
	 uint8_t vibrato_depth;
	 uint8_t vibrato_rate;
	 uint16_t volume_fadeout;

	 xm_sample_t* samples;
 };
 typedef struct xm_instrument_s xm_instrument_t;

 struct xm_pattern_slot_s {
	 uint8_t note; /* 1-96, 97 = Key Off note */
	 uint8_t instrument; /* 1-128 */
	 uint8_t volume_column;
	 uint8_t effect_type;
	 uint8_t effect_param;
 };
 typedef struct xm_pattern_slot_s xm_pattern_slot_t;

 struct xm_pattern_s {
	 uint16_t num_rows;
	 xm_pattern_slot_t* slots; /* Array of size num_rows * num_channels */
 };
 typedef struct xm_pattern_s xm_pattern_t;

 struct xm_module_s {
	 char name[MODULE_NAME_LENGTH];
	 char trackername[TRACKER_NAME_LENGTH];
	 uint16_t length;
	 uint16_t restart_position;
	 uint16_t num_channels;
	 uint16_t num_patterns;
	 uint16_t num_instruments;
	 xm_frequency_type_t frequency_type;
	 uint8_t pattern_table[PATTERN_ORDER_TABLE_LENGTH];

	 xm_pattern_t* patterns;
	 xm_instrument_t* instruments; /* Instrument 1 has index 0,
									* instrument 2 has index 1, etc. */
 };
 typedef struct xm_module_s xm_module_t;

 struct xm_channel_context_s {
	 float note;
	 xm_instrument_t* instrument; /* Could be NULL */
	 xm_sample_t* sample; /* Could be NULL */
	 float sample_position;
	 float period;
	 float frequency;
	 float step;

	 float volume; /* Ideally between 0 (muted) and 1 (loudest) */
	 float panning; /* Between 0 (left) and 1 (right); 0.5 is centered */

	 bool sustained;
	 float fadeout_volume;
	 float volume_envelope_volume;
	 float panning_envelope_panning;
	 uint16_t volume_envelope_frame_count;
	 uint16_t panning_envelope_frame_count;

	 uint8_t current_volume_effect;
	 uint8_t current_effect;
	 uint8_t current_effect_param;

	 bool arp_in_progress;
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
	 float tone_portamento_target_period;
	 uint8_t multi_retrig_param;
	 bool note_delay;
	 uint8_t note_delay_param;
	 xm_pattern_slot_t* note_delay_note;

	 float final_volume_left;
	 float final_volume_right;
 };
 typedef struct xm_channel_context_s xm_channel_context_t;

 struct xm_context_s {
	 void* allocated_memory;
	 xm_module_t module;
	 uint32_t rate;

	 uint16_t tempo;
	 uint16_t bpm;
	 float global_volume;
	 float amplification;

	 uint8_t current_table_index;
	 uint8_t current_row;
	 uint8_t current_tick;
	 float remaining_samples_in_tick;

	 bool position_jump;
	 bool pattern_break;
	 uint8_t jump_dest;
	 uint8_t jump_row;

	 uint8_t* row_loop_count; /* Array of size MAX_NUM_ROWS * module_length */
	 uint8_t loop_count;

	 xm_channel_context_t* channels;
};

/* ----- Internal API ----- */

/** Check the module header (first 60 bytes).
 *
 * @returns 0 if everything looks OK.
 */
int xm_check_header_sanity(char*);

/** Get the number of bytes needed to store the module data in a
 * dynamically allocated blank context.
 *
 * Things that are dynamically allocated:
 * - sample data
 * - sample structures in instruments
 * - pattern data
 * - row loop count arrays
 * - pattern structures in module
 * - instrument structures in module
 * - channel contexts
 * - context structure itself
 */
size_t xm_get_memory_needed_for_context(char*);

/** Populate the context from module data.
 *
 * @returns pointer to the memory pool
 */
char* xm_load_module(xm_context_t*, char*, char*);
