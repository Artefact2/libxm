/* Author: Romain "Artefact2" Dalmaso <artefact2@gmail.com> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#include "xm_internal.h"



void xm_set_max_loop_count(xm_context_t* context, uint8_t loopcnt) {
	context->module.max_loop_count = loopcnt;
}

uint8_t xm_get_loop_count(const xm_context_t* context) {
	return context->loop_count;
}



void xm_seek(xm_context_t* ctx, uint8_t pot, uint8_t row, uint8_t tick) {
	ctx->current_table_index = pot;
	ctx->current_row = row;
	ctx->current_tick = tick;
	ctx->remaining_samples_in_tick = 0;
}



bool xm_mute_channel(xm_context_t* ctx, uint8_t channel, bool mute) {
	bool old = ctx->channels[channel - 1].muted;
	ctx->channels[channel - 1].muted = mute;
	return old;
}

bool xm_mute_instrument(xm_context_t* ctx, uint8_t instr, bool mute) {
	bool old = ctx->instruments[instr - 1].muted;
	ctx->instruments[instr - 1].muted = mute;
	return old;
}



#if XM_STRINGS
const char* xm_get_module_name(const xm_context_t* ctx) {
	return ctx->module.name;
}

const char* xm_get_tracker_name(const xm_context_t* ctx) {
	return ctx->module.trackername;
}

const char* xm_get_instrument_name(const xm_context_t* ctx, uint8_t i) {
	return ctx->instruments[i-1].name;
}

const char* xm_get_sample_name(const xm_context_t* ctx, uint8_t i, uint8_t s) {
	return ctx->samples[ctx->instruments[i-1].samples_index + s].name;
}
#else
const char* xm_get_module_name([[maybe_unused]] const xm_context_t* ctx) {
	return "";
}

const char* xm_get_tracker_name([[maybe_unused]] const xm_context_t* ctx) {
	return "";
}

const char* xm_get_instrument_name([[maybe_unused]] const xm_context_t* ctx,
                                   [[maybe_unused]] uint8_t i) {
	return "";
}

const char* xm_get_sample_name([[maybe_unused]] const xm_context_t* ctx,
                               [[maybe_unused]] uint8_t i,
                               [[maybe_unused]] uint8_t s) {
	return "";
}
#endif



uint8_t xm_get_number_of_channels(const xm_context_t* ctx) {
	return ctx->module.num_channels;
}

uint16_t xm_get_module_length(const xm_context_t* ctx) {
	return ctx->module.length;
}

uint16_t xm_get_number_of_patterns(const xm_context_t* ctx) {
	return ctx->module.num_patterns;
}

uint16_t xm_get_number_of_rows(const xm_context_t* ctx, uint16_t pattern) {
	return ctx->patterns[pattern].num_rows;
}

uint8_t xm_get_number_of_instruments(const xm_context_t* ctx) {
	return ctx->module.num_instruments;
}

uint8_t xm_get_number_of_samples(const xm_context_t* ctx, uint8_t i) {
	return ctx->instruments[i-1].num_samples;
}

xm_sample_point_t* xm_get_sample_waveform(xm_context_t* ctx,
                                          uint8_t instrument,
                                          uint8_t sample, uint32_t* length) {
	xm_sample_t* s = ctx->samples + ctx->instruments[instrument-1].samples_index + sample;
	*length = s->length;
	return ctx->samples_data + s->index;
}



void xm_get_playing_speed(const xm_context_t* ctx,
                          uint8_t* bpm, uint8_t* tempo) {
	if(bpm) *bpm = CURRENT_BPM(ctx);
	if(tempo) *tempo = CURRENT_TEMPO(ctx);
}

void xm_get_position(const xm_context_t* ctx, uint8_t* pattern_index,
                     uint8_t* pattern, uint8_t* row, uint32_t* samples) {
	if(pattern_index) *pattern_index = ctx->current_table_index;
	if(pattern) *pattern = ctx->module.pattern_table[ctx->current_table_index];
	if(row) *row = ctx->current_row - 1;
	if(samples) {
		#if XM_TIMING_FUNCTIONS
		*samples = ctx->generated_samples;
		#else
		*samples = 0;
		#endif
	}
}

#if XM_TIMING_FUNCTIONS
uint32_t xm_get_latest_trigger_of_instrument(const xm_context_t* ctx,
                                             uint8_t instr) {
	return ctx->instruments[instr-1].latest_trigger;
}
uint32_t xm_get_latest_trigger_of_sample(const xm_context_t* ctx,
                                         uint8_t instr, uint8_t sample) {
	return ctx->samples[ctx->instruments[instr-1].samples_index + sample].latest_trigger;
}
uint32_t xm_get_latest_trigger_of_channel(const xm_context_t* ctx,
                                          uint8_t chn) {
	return ctx->channels[chn - 1].latest_trigger;
}
#else
uint32_t xm_get_latest_trigger_of_instrument([[maybe_unused]] const xm_context_t* ctx, [[maybe_unused]] uint8_t instr) {
	return 0;
}
uint32_t xm_get_latest_trigger_of_sample([[maybe_unused]] const xm_context_t* ctx, [[maybe_unused]] uint8_t instr, [[maybe_unused]] uint8_t sample) {
	return 0;
}
uint32_t xm_get_latest_trigger_of_channel([[maybe_unused]] const xm_context_t* ctx, [[maybe_unused]] uint8_t chn) {
	return 0;
}
#endif

bool xm_is_channel_active(const xm_context_t* ctx, uint8_t chn) {
	const xm_channel_context_t* ch = ctx->channels + (chn - 1);
	return ch->sample != NULL
		&& (ch->actual_volume[0] + ch->actual_volume[1]) > 0.001f;
}

float xm_get_frequency_of_channel(const xm_context_t* ctx, uint8_t chn) {
	return (float)ctx->channels[chn - 1].step
		* (float)ctx->module.rate / (float)SAMPLE_MICROSTEPS;
}

float xm_get_volume_of_channel(const xm_context_t* ctx, uint8_t chn) {
	/* Instead of duplicating the panning and volume formulas, just
	   reciprocate the panning math from cached computed volumes */
	float x = ctx->channels[chn-1].actual_volume[0];
	float y = ctx->channels[chn-1].actual_volume[1];
	return sqrtf(x*x + y*y);
}

float xm_get_panning_of_channel(const xm_context_t* ctx, uint8_t chn) {
	float x = ctx->channels[chn-1].actual_volume[0];
	float y = ctx->channels[chn-1].actual_volume[1];
	x *= x;
	y *= y;
	return y / (x + y);
}

uint8_t xm_get_instrument_of_channel(const xm_context_t* ctx, uint8_t chn) {
	const xm_channel_context_t* ch = ctx->channels + (chn - 1);
	if(ch->instrument == NULL) return 0;
	assert(ch->instrument - ctx->instruments < UINT8_MAX);
	return (uint8_t)(1 + (ch->instrument - ctx->instruments));
}

void xm_reset_context(xm_context_t* ctx) {
	__builtin_memset(ctx->channels, 0, sizeof(xm_channel_context_t)
	                                     * ctx->module.num_channels);

	__builtin_memset(ctx->row_loop_count, 0, MAX_ROWS_PER_PATTERN
	                                           * ctx->module.length);

	__builtin_memset((char*)ctx
	                   + offsetof(xm_context_t, remaining_samples_in_tick),
	                 0,
	                 sizeof(xm_context_t)
	                   - offsetof(xm_context_t, remaining_samples_in_tick));

	#if HAS_GLOBAL_VOLUME
	ctx->global_volume = MAX_VOLUME;
	#endif

	#if HAS_EFFECT(EFFECT_SET_TEMPO)
	ctx->current_tempo = ctx->module.tempo;
	#endif

	#if HAS_EFFECT(EFFECT_SET_BPM)
	ctx->current_bpm = ctx->module.bpm;
	#endif

	#if XM_TIMING_FUNCTIONS
	xm_instrument_t* inst = ctx->instruments;
	for(typeof(ctx->module.num_instruments) i = ctx->module.num_instruments;
	    i; --i, ++inst) {
		inst->latest_trigger = 0;
	}

	xm_sample_t* smp = ctx->samples;
	for(typeof(ctx->module.num_samples) i = ctx->module.num_samples;
	    i; --i, ++smp) {
		smp->latest_trigger = 0;
	}
	#endif
}
