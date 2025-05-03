/* Author: Romain "Artefact2" Dalmaso <artefact2@gmail.com> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#include "xm_internal.h"

#if XM_DEFENSIVE
#define CHECK_PATTERN(ctx, p) do { \
		if((p) >= (ctx)->module.num_patterns) { \
			NOTICE("invalid pattern %d", (p)); \
			return 0; \
		} \
	} while(0)
#define CHECK_CHANNEL(ctx, c) do { \
		if((c) == 0 || (c) > (ctx)->module.num_channels) { \
			NOTICE("invalid channel %d", (c)); \
			return 0; \
		} \
	} while(0)
#define CHECK_INSTRUMENT(ctx, i) do { \
		if((i) == 0 || (i) > (ctx)->module.num_instruments) { \
			NOTICE("invalid instrument %d", (i)); \
			return 0; \
		} \
	} while(0)
#define CHECK_SAMPLE(ctx, i, s) do { \
		CHECK_INSTRUMENT((ctx), (i)); \
		if((s) >= (ctx)->instruments[(i)-1].num_samples) { \
			NOTICE("invalid sample %d for instrument %d", (s), (i)); \
			return 0; \
		} \
	} while(0)
#else
#define CHECK_PATTERN(ctx, p)
#define CHECK_CHANNEL(ctx, c)
#define CHECK_INSTRUMENT(ctx, i)
#define CHECK_SAMPLE(ctx, i, s)
#endif



void xm_set_max_loop_count(xm_context_t* context, uint8_t loopcnt) {
	context->max_loop_count = loopcnt;
}

uint8_t xm_get_loop_count(xm_context_t* context) {
	return context->loop_count;
}



void xm_seek(xm_context_t* ctx, uint8_t pot, uint8_t row, uint16_t tick) {
	ctx->current_table_index = pot;
	ctx->current_row = row;
	ctx->current_tick = tick;
	ctx->remaining_samples_in_tick = 0;
}



bool xm_mute_channel(xm_context_t* ctx, uint16_t channel, bool mute) {
	CHECK_CHANNEL(ctx, channel);
	bool old = ctx->channels[channel - 1].muted;
	ctx->channels[channel - 1].muted = mute;
	return old;
}

bool xm_mute_instrument(xm_context_t* ctx, uint16_t instr, bool mute) {
	CHECK_INSTRUMENT(ctx, instr);
	bool old = ctx->instruments[instr - 1].muted;
	ctx->instruments[instr - 1].muted = mute;
	return old;
}



#if XM_STRINGS
const char* xm_get_module_name(xm_context_t* ctx) {
	return ctx->module.name;
}

const char* xm_get_tracker_name(xm_context_t* ctx) {
	return ctx->module.trackername;
}
#else
const char* xm_get_module_name(__attribute__((unused)) xm_context_t* ctx) {
	return NULL;
}

const char* xm_get_tracker_name(__attribute__((unused)) xm_context_t* ctx) {
	return NULL;
}
#endif



uint16_t xm_get_number_of_channels(xm_context_t* ctx) {
	return ctx->module.num_channels;
}

uint16_t xm_get_module_length(xm_context_t* ctx) {
	return ctx->module.length;
}

uint16_t xm_get_number_of_patterns(xm_context_t* ctx) {
	return ctx->module.num_patterns;
}

uint16_t xm_get_number_of_rows(xm_context_t* ctx, uint16_t pattern) {
	CHECK_PATTERN(ctx, pattern);
	return ctx->patterns[pattern].num_rows;
}

uint16_t xm_get_number_of_instruments(xm_context_t* ctx) {
	return ctx->module.num_instruments;
}

uint16_t xm_get_number_of_samples(xm_context_t* ctx, uint16_t i) {
	CHECK_INSTRUMENT(ctx, i);
	return ctx->instruments[i-1].num_samples;
}

xm_sample_point_t* xm_get_sample_waveform(xm_context_t* ctx, uint16_t instrument, uint16_t sample, uint32_t* length) {
	CHECK_SAMPLE(ctx, instrument, sample);
	xm_sample_t* s = ctx->samples + ctx->instruments[instrument-1].samples_index + sample;
	*length = s->length;
	return ctx->samples_data + s->index;
}



void xm_get_playing_speed(xm_context_t* ctx, uint16_t* bpm, uint16_t* tempo) {
	if(bpm) *bpm = ctx->bpm;
	if(tempo) *tempo = ctx->tempo;
}

void xm_get_position(xm_context_t* ctx, uint8_t* pattern_index, uint8_t* pattern, uint8_t* row, uint64_t* samples) {
	if(pattern_index) *pattern_index = ctx->current_table_index;
	if(pattern) *pattern = ctx->module.pattern_table[ctx->current_table_index];
	if(row) *row = ctx->current_row;
	if(samples) *samples = ctx->generated_samples;
}

uint64_t xm_get_latest_trigger_of_instrument(xm_context_t* ctx, uint16_t instr) {
	CHECK_INSTRUMENT(ctx, instr);
	return ctx->instruments[instr-1].latest_trigger;
}

uint64_t xm_get_latest_trigger_of_sample(xm_context_t* ctx, uint16_t instr, uint16_t sample) {
	CHECK_SAMPLE(ctx, instr, sample);
	return ctx->samples[ctx->instruments[instr-1].samples_index + sample].latest_trigger;
}

uint64_t xm_get_latest_trigger_of_channel(xm_context_t* ctx, uint16_t chn) {
	CHECK_CHANNEL(ctx, chn);
	return ctx->channels[chn - 1].latest_trigger;
}

bool xm_is_channel_active(xm_context_t* ctx, uint16_t chn) {
	CHECK_CHANNEL(ctx, chn);
	xm_channel_context_t* ch = ctx->channels + (chn - 1);
	return ch->instrument != NULL && ch->sample != NULL && ch->sample_position >= 0;
}

float xm_get_frequency_of_channel(xm_context_t* ctx, uint16_t chn) {
	CHECK_CHANNEL(ctx, chn);
	return ctx->channels[chn - 1].frequency;
}

float xm_get_volume_of_channel(xm_context_t* ctx, uint16_t chn) {
	CHECK_CHANNEL(ctx, chn);
	return ctx->channels[chn - 1].volume * ctx->global_volume;
}

float xm_get_panning_of_channel(xm_context_t* ctx, uint16_t chn) {
	CHECK_CHANNEL(ctx, chn);
	return ctx->channels[chn - 1].panning;
}

uint16_t xm_get_instrument_of_channel(xm_context_t* ctx, uint16_t chn) {
	CHECK_CHANNEL(ctx, chn);
	xm_channel_context_t* ch = ctx->channels + (chn - 1);
	if(ch->instrument == NULL) return 0;
	return 1 + (ch->instrument - ctx->instruments);
}
