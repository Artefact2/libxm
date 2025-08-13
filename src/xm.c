/* Author: Romain "Artefact2" Dalmaso <artefact2@gmail.com> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#include "xm_internal.h"



void xm_set_max_loop_count([[maybe_unused]] xm_context_t* context,
                           [[maybe_unused]] uint8_t loopcnt) {
	#if XM_LOOPING_TYPE == 2
	context->module.max_loop_count = loopcnt;
	#endif
}

uint8_t xm_get_loop_count([[maybe_unused]] const xm_context_t* context) {
	return LOOP_COUNT(context);
}



void xm_seek(xm_context_t* ctx, uint8_t pot, uint8_t row, uint8_t tick) {
	ctx->current_table_index = pot;
	ctx->current_row = row;
	ctx->current_tick = tick;
	ctx->remaining_samples_in_tick = 0;
}



bool xm_mute_channel(xm_context_t* ctx, uint8_t channel,
                     [[maybe_unused]] bool mute) {
	assert(channel <= ctx->module.num_channels);

	#if XM_MUTING_FUNCTIONS
	bool old = ctx->channels[channel - 1].muted;
	ctx->channels[channel - 1].muted = mute;
	return old;
	#else
	return false;
	#endif
}

bool xm_mute_instrument(xm_context_t* ctx, uint8_t instr,
                        [[maybe_unused]] bool mute) {
	assert(instr >= 1 && instr <= NUM_INSTRUMENTS(&ctx->module));

	#if XM_MUTING_FUNCTIONS
	bool old = ctx->instruments[instr - 1].muted;
	ctx->instruments[instr - 1].muted = mute;
	return old;
	#else
	return false;
	#endif
}



#if XM_STRINGS
const char* xm_get_module_name(const xm_context_t* ctx) {
	return ctx->module.name;
}

const char* xm_get_tracker_name(const xm_context_t* ctx) {
	return ctx->module.trackername;
}

const char* xm_get_instrument_name(const xm_context_t* ctx, uint8_t i) {
	assert(i >= 1 && i <= NUM_INSTRUMENTS(&ctx->module));

	#if HAS_INSTRUMENTS
	return ctx->instruments[i-1].name;
	#else
	return "";
	#endif
}

const char* xm_get_sample_name(const xm_context_t* ctx, uint8_t i, uint8_t s) {
	assert(i >= 1 && i <= NUM_INSTRUMENTS(&ctx->module));

	#if HAS_FEATURE(FEATURE_MULTISAMPLE_INSTRUMENTS)
	assert(s < ctx->instruments[i-1].num_samples);
	return ctx->samples[ctx->instruments[i-1].samples_index + s].name;
	#else
	assert(s == 0);
	return ctx->samples[i-1].name;
	#endif
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
	return NUM_INSTRUMENTS(&ctx->module);
}

uint8_t xm_get_number_of_samples(const xm_context_t* ctx, uint8_t i) {
	assert(i >= 1 && i <= NUM_INSTRUMENTS(&ctx->module));

	#if HAS_FEATURE(FEATURE_MULTISAMPLE_INSTRUMENTS)
	return ctx->instruments[i-1].num_samples;
	#else
	return 1;
	#endif
}

xm_sample_point_t* xm_get_sample_waveform(xm_context_t* ctx,
                                          uint8_t instrument,
                                          uint8_t sample, uint32_t* length) {
	assert(instrument > 0 && instrument <= NUM_INSTRUMENTS(&ctx->module));

	xm_sample_t* s;
	#if HAS_FEATURE(FEATURE_MULTISAMPLE_INSTRUMENTS)
	assert(sample < ctx->instruments[instrument-1].num_samples);
	s = ctx->samples + ctx->instruments[instrument-1].samples_index + sample;
	#else
	assert(sample == 0);
	s = ctx->samples + instrument - 1;
	#endif

	*length = s->length;
	return ctx->samples_data + s->index;
}



void xm_get_playing_speed([[maybe_unused]] const xm_context_t* ctx,
                          uint8_t* bpm, uint8_t* tempo) {
	if(bpm) *bpm = CURRENT_BPM(ctx);
	if(tempo) *tempo = CURRENT_TEMPO(ctx);
}

void xm_get_position(const xm_context_t* ctx, uint8_t* pattern_index,
                     uint8_t* pattern, uint8_t* row, uint32_t* samples) {
	static_assert(PATTERN_ORDER_TABLE_LENGTH - 1 <= UINT8_MAX);
	if(pattern_index) *pattern_index = (uint8_t)ctx->current_table_index;
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

uint32_t xm_get_latest_trigger_of_instrument(const xm_context_t* ctx,
                                             uint8_t instr) {
	assert(instr >= 1 && instr <= NUM_INSTRUMENTS(&ctx->module));

	#if XM_TIMING_FUNCTIONS
	return ctx->instruments[instr-1].latest_trigger;
	#else
	return 0;
	#endif
}

uint32_t xm_get_latest_trigger_of_sample(const xm_context_t* ctx,
                                         uint8_t instr,
                                         [[maybe_unused]] uint8_t sample) {
	assert(instr >= 1 && instr <= NUM_INSTRUMENTS(&ctx->module));

	#if XM_TIMING_FUNCTIONS
	#if HAS_FEATURE(FEATURE_MULTISAMPLE_INSTRUMENTS)
	assert(sample < ctx->instruments[instr-1].num_samples);
	return ctx->samples[ctx->instruments[instr-1].samples_index + sample].latest_trigger;
	#else
	assert(sample == 0);
	return ctx->samples[instr-1].latest_trigger;
	#endif
	#else
	return 0;
	#endif
}

uint32_t xm_get_latest_trigger_of_channel(const xm_context_t* ctx,
                                          uint8_t chn) {
	assert(chn >= 1 && chn <= ctx->module.num_channels);

	#if XM_TIMING_FUNCTIONS
	return ctx->channels[chn - 1].latest_trigger;
	#else
	return 0;
	#endif
}

bool xm_is_channel_active(const xm_context_t* ctx, uint8_t chn) {
	assert(chn >= 1 && chn <= ctx->module.num_channels);
	const xm_channel_context_t* ch = ctx->channels + (chn - 1);
	return ch->sample != NULL
		&& (ch->actual_volume[0] + ch->actual_volume[1]) > 0.001f;
}

float xm_get_frequency_of_channel(const xm_context_t* ctx, uint8_t chn) {
	assert(chn >= 1 && chn <= ctx->module.num_channels);
	return (float)ctx->channels[chn - 1].step
		* (float)CURRENT_SAMPLE_RATE(ctx) / (float)SAMPLE_MICROSTEPS;
}

float xm_get_volume_of_channel(const xm_context_t* ctx, uint8_t chn) {
	assert(chn >= 1 && chn <= ctx->module.num_channels);

	/* Instead of duplicating the panning and volume formulas, just
	   reciprocate the panning math from cached computed volumes */
	float x = ctx->channels[chn-1].actual_volume[0];
	float y = ctx->channels[chn-1].actual_volume[1];
	return sqrtf(x*x + y*y);
}

float xm_get_panning_of_channel(const xm_context_t* ctx, uint8_t chn) {
	assert(chn >= 1 && chn <= ctx->module.num_channels);

	float x = ctx->channels[chn-1].actual_volume[0];
	float y = ctx->channels[chn-1].actual_volume[1];
	x *= x;
	y *= y;
	return y / (x + y);
}

uint8_t xm_get_instrument_of_channel(const xm_context_t* ctx, uint8_t chn) {
	assert(chn >= 1 && chn <= ctx->module.num_channels);
	const xm_channel_context_t* ch = ctx->channels + (chn - 1);

	#if HAS_INSTRUMENTS
	if(ch->instrument == NULL) return 0;
	assert(ch->instrument - ctx->instruments < UINT8_MAX);
	return (uint8_t)(1 + (ch->instrument - ctx->instruments));
	#else
	if(ch->sample == NULL) return 0;
	assert(ch->sample - ctx->samples < UINT8_MAX);
	return (uint8_t)(1 + (ch->sample - ctx->samples));
	#endif
}

void xm_reset_context(xm_context_t* ctx) {
	__builtin_memset(ctx->channels, 0, sizeof(xm_channel_context_t)
	                                     * ctx->module.num_channels);

	#if HAS_PANNING && HAS_EFFECT(EFFECT_SET_CHANNEL_PANNING)
	for(uint8_t ch = 0; ch < ctx->module.num_channels; ++ch) {
		ctx->channels[ch].base_panning =
			DEFAULT_CHANNEL_PANNING(&ctx->module, ch);
	}
	#endif

	#if XM_LOOPING_TYPE == 2
	__builtin_memset(ctx->row_loop_count, 0, MAX_ROWS_PER_PATTERN
	                                           * ctx->module.length);
	#endif

	__builtin_memset((char*)ctx
	                   + offsetof(xm_context_t, remaining_samples_in_tick),
	                 0,
	                 sizeof(xm_context_t)
	                   - offsetof(xm_context_t, remaining_samples_in_tick));

	#if HAS_GLOBAL_VOLUME
	ctx->global_volume = DEFAULT_GLOBAL_VOLUME(&ctx->module);
	#endif

	#if HAS_EFFECT(EFFECT_SET_TEMPO)
	ctx->current_tempo = DEFAULT_TEMPO(&ctx->module);
	#endif

	#if HAS_EFFECT(EFFECT_SET_BPM)
	ctx->current_bpm = DEFAULT_BPM(&ctx->module);
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

void xm_set_sample_rate([[maybe_unused]] xm_context_t* ctx,
                        [[maybe_unused]] uint16_t rate) {
	#if XM_SAMPLE_RATE == 0
	ctx->current_sample_rate = rate;
	#endif
}

uint16_t xm_get_sample_rate([[maybe_unused]] const xm_context_t* ctx) {
	return CURRENT_SAMPLE_RATE(ctx);
}

/* For debugging */
void xm_print_pattern([[maybe_unused]] xm_context_t* ctx,
                      [[maybe_unused]] uint8_t pat) {
	#if XM_VERBOSE
	fprintf(stderr, "+-%02X-+", pat);
	for(uint8_t ch = 0; ch < ctx->module.num_channels; ++ch) {
		fprintf(stderr, "---- CH %02d -----+", ch + 1);
	}
	fprintf(stderr, "\n");
	for(uint8_t row = 0; row < 64; ++row) {
		fprintf(stderr, "| %02X | ", row);
		for(uint8_t ch = 0; ch < ctx->module.num_channels; ++ch) {
			xm_pattern_slot_t* s = ctx->pattern_slots
				+ (ctx->patterns[pat].rows_index + row)
				  * ctx->module.num_channels
				+ ch;

			if(s->note == NOTE_KEY_OFF) {
				fprintf(stderr, "OFF ");
			} else if(s->note == NOTE_SWITCH) {
				fprintf(stderr, "... ");
			} else if(s->note) {
				static const char* const notes[] = {
					"C-", "C#", "D-", "D#", "E-", "F-",
					"F#", "G-", "G#", "A-", "A#", "B-" };
				fprintf(stderr, "%s%u ",
				        notes[(s->note - 1) % 12],
				        (s->note - 1) / 12);
			} else {
				fprintf(stderr, "... ");
			}
			if(s->instrument) {
				fprintf(stderr, "%02X ", s->instrument);
			} else {
				fprintf(stderr, ".. ");
			}
			if(VOLUME_COLUMN(s)) {
				fprintf(stderr, "%02X ", VOLUME_COLUMN(s));
			} else {
				fprintf(stderr, ".. ");
			}
			if(s->effect_type || s->effect_param) {
				fprintf(stderr, "%02X%02X | ", s->effect_type,
				       s->effect_param);
			} else {
				fprintf(stderr, ".... | ");
			}
		}
		fprintf(stderr, "\n");
	}
	fprintf(stderr, "+----+");
	for(uint8_t ch = 0; ch < ctx->module.num_channels; ++ch) {
		fprintf(stderr, "----------------+");
	}
	fprintf(stderr, "\n");
	#endif
}
