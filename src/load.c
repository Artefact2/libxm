/* Author: Romain "Artefact2" Dalmaso <artefact2@gmail.com> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#include "xm_internal.h"
#include <string.h>

int xm_check_header_sanity(char* module) {
	if(memcmp("Extended Module: ", module, 17) != 0) {
		return 1;
	}

	if(module[37] != 0x1A) {
		return 2;
	}

	if(module[59] != 0x01 || module[58] != 0x04) {
		/* Not XM 1.04 */
		return 3;
	}

	return 0;
}

size_t xm_get_memory_needed_for_context(char* moddata) {
	size_t memory_needed = 0;
	size_t offset = 60; /* Skip the first header */
	uint16_t num_channels;
	uint16_t num_patterns;
	uint16_t num_instruments;

	/* Read the module header */

	num_channels = *((uint16_t*)(moddata + offset + 8));
	DEBUG("got %i channels", num_channels);

	num_patterns = *((uint16_t*)(moddata + offset + 10));
	memory_needed += num_patterns * sizeof(xm_pattern_t);
	DEBUG("got %i patterns", num_patterns);

	num_instruments = *((uint16_t*)(moddata + offset + 12));
	memory_needed += num_instruments * sizeof(xm_instrument_t);
	DEBUG("got %i instruments", num_instruments);

	/* Header size */
	offset += *((uint32_t*)(moddata + offset));

	/* Read pattern headers */
	for(uint16_t i = 0; i < num_patterns; ++i) {
		uint16_t num_rows;

		num_rows = *((uint16_t*)(moddata + offset + 5));
		memory_needed += num_rows * num_channels * sizeof(xm_pattern_slot_t);

		/* Pattern header length + packed pattern data size */
		offset += *((uint32_t*)(moddata + offset)) + *((uint16_t*)(moddata + offset + 7));
	}

	/* Read instrument headers */
	for(uint16_t i = 0; i < num_instruments; ++i) {
		uint16_t num_samples;
		uint32_t sample_header_size = 0;
		uint32_t sample_size_aggregate = 0;

		num_samples = *((uint16_t*)(moddata + offset + 27));
		memory_needed += num_samples * sizeof(xm_sample_t);

		if(num_samples > 0) {
			sample_header_size = *((uint32_t*)(moddata + offset + 29));
		}

		/* Instrument header size */
		offset += *((uint32_t*)(moddata + offset));

		for(uint16_t j = 0; j < num_samples; ++j) {
			uint32_t sample_size;
			uint8_t flags;

			sample_size = *((uint32_t*)(moddata + offset));
			flags = *((uint8_t*)(moddata + offset + 14));
			sample_size_aggregate += sample_size;

			if(flags & (1 << 4)) {
				/* 16 bit sample */
				memory_needed += sample_size * sizeof(float) * 0.5;
			} else {
				/* 8 bit sample */
				memory_needed += sample_size * sizeof(float);
			}

			offset += sample_header_size;
		}

		offset += sample_size_aggregate;
	}

	memory_needed += num_channels * sizeof(xm_channel_context_t);
	memory_needed += sizeof(xm_context_t);

	return memory_needed;
}

char* xm_load_module(xm_context_t* ctx, char* moddata, char* mempool) {
	size_t offset = 0;

	/* Read XM header */
	memcpy(ctx->module.name, moddata + offset + 17, MODULE_NAME_LENGTH);
	memcpy(ctx->module.trackername, moddata + offset + 38, TRACKER_NAME_LENGTH);
	offset += 60;

	/* Read module header */
	uint32_t header_size = *((uint32_t*)(moddata + offset));

	ctx->module.length = *((uint16_t*)(moddata + offset + 4));
	ctx->module.restart_position = *((uint16_t*)(moddata + offset + 6));
	ctx->module.num_channels = *((uint16_t*)(moddata + offset + 8));
	ctx->module.num_patterns = *((uint16_t*)(moddata + offset + 10));
	ctx->module.num_instruments = *((uint16_t*)(moddata + offset + 12));

	ctx->module.patterns = (xm_pattern_t*)mempool;
	mempool += ctx->module.num_patterns * sizeof(xm_pattern_t);

	ctx->module.instruments = (xm_instrument_t*)mempool;
	mempool += ctx->module.num_instruments * sizeof(xm_instrument_t);

	uint16_t flags = *((uint32_t*)(moddata + offset + 14));
	if(flags & (1 << 0)) {
		ctx->module.frequency_type = XM_LINEAR_FREQUENCIES;
	} else {
		ctx->module.frequency_type = XM_AMIGA_FREQUENCIES;
	}

	ctx->tempo = *((uint16_t*)(moddata + offset + 16));
	ctx->bpm = *((uint16_t*)(moddata + offset + 18));
	memcpy(ctx->module.pattern_table, moddata + offset + 20, PATTERN_ORDER_TABLE_LENGTH);
	offset += header_size;

	/* Read patterns */
	for(uint16_t i = 0; i < ctx->module.num_patterns; ++i) {
		uint16_t packed_patterndata_size = *((uint16_t*)(moddata + offset + 7));
		uint16_t num_rows = *((uint16_t*)(moddata + offset + 5));

		ctx->module.patterns[i].num_rows = num_rows;

		/* Pattern header length */
		offset += *((uint32_t*)(moddata + offset));

		if(packed_patterndata_size == 0) {
			/* No pattern data is present */
			ctx->module.patterns[i].slots = NULL;
		} else {
			ctx->module.patterns[i].slots = (xm_pattern_slot_t*)mempool;
			mempool += ctx->module.num_channels * num_rows * sizeof(xm_pattern_slot_t);

			/* This isn't your typical for loop */
			for(uint16_t j = 0, k = 0; j < packed_patterndata_size; ++k) {
				uint8_t note = *((uint8_t*)(moddata + offset + j));
				if(note & (1 << 7)) {
					/* MSB is set, this is a compressed packet */
					++j;

					if(note & (1 << 0)) {
						/* Note follows */
						ctx->module.patterns[i].slots[k].note =
							*((uint8_t*)(moddata + offset + j));
						++j;							
					} else {
						ctx->module.patterns[i].slots[k].note = 0;
					}

					if(note & (1 << 1)) {
						/* Instrument follows */
						ctx->module.patterns[i].slots[k].instrument =
							*((uint8_t*)(moddata + offset + j));
						++j;							
					} else {
						ctx->module.patterns[i].slots[k].instrument = 0;
					}

					if(note & (1 << 2)) {
						/* Volume column follows */
						ctx->module.patterns[i].slots[k].volume_column =
							*((uint8_t*)(moddata + offset + j));
						++j;							
					} else {
						ctx->module.patterns[i].slots[k].volume_column = 0;
					}

					if(note & (1 << 3)) {
						/* Effect follows */
						ctx->module.patterns[i].slots[k].effect_type =
							*((uint8_t*)(moddata + offset + j));
						++j;							
					} else {
						ctx->module.patterns[i].slots[k].effect_type = 0;
					}

					if(note & (1 << 4)) {
						/* Effect parameter follows */
						ctx->module.patterns[i].slots[k].effect_param =
							*((uint8_t*)(moddata + offset + j));
						++j;							
					} else {
						ctx->module.patterns[i].slots[k].effect_param = 0;
					}
				} else {
					/* Uncompressed packet */

					ctx->module.patterns[i].slots[k].note = note;

					ctx->module.patterns[i].slots[k].instrument = 
						*((uint8_t*)(moddata + offset + j + 1));
					ctx->module.patterns[i].slots[k].volume_column = 
						*((uint8_t*)(moddata + offset + j + 2));
					ctx->module.patterns[i].slots[k].effect_type = 
						*((uint8_t*)(moddata + offset + j + 3));
					ctx->module.patterns[i].slots[k].effect_param = 
						*((uint8_t*)(moddata + offset + j + 4));

					j += 5;
				}
			}
		}

		offset += packed_patterndata_size;
	}

	/* Read instruments */
	for(uint16_t i = 0; i < ctx->module.num_instruments; ++i) {
		uint32_t sample_header_size = 0;

		memcpy(ctx->module.instruments[i].name, moddata + offset + 4, INSTRUMENT_NAME_LENGTH);
		ctx->module.instruments[i].num_samples = *((uint16_t*)(moddata + offset + 27));

		bool sample_is_16bit[ctx->module.instruments[i].num_samples]; /* VLA */

		if(ctx->module.instruments[i].num_samples > 0) {
			/* Read extra header properties */
			sample_header_size = *((uint32_t*)(moddata + offset + 29));

			memcpy(ctx->module.instruments[i].sample_of_notes, moddata + offset + 33, NUM_NOTES);

			ctx->module.instruments[i].volume_envelope.num_points =
				*((uint8_t*)(moddata + offset + 225));
			ctx->module.instruments[i].panning_envelope.num_points =
				*((uint8_t*)(moddata + offset + 226));

			for(uint8_t j = 0; j < ctx->module.instruments[i].volume_envelope.num_points; ++j) {
				ctx->module.instruments[i].volume_envelope.points[j].frame = 
					*(uint16_t*)(moddata + offset + 129 + 4 * j);
				ctx->module.instruments[i].volume_envelope.points[j].value = 
					*(uint16_t*)(moddata + offset + 129 + 4 * j + 2);
			}

			for(uint8_t j = 0; j < ctx->module.instruments[i].panning_envelope.num_points; ++j) {
				ctx->module.instruments[i].panning_envelope.points[j].frame = 
					*(uint16_t*)(moddata + offset + 177 + 4 * j);
				ctx->module.instruments[i].panning_envelope.points[j].value = 
					*(uint16_t*)(moddata + offset + 177 + 4 * j + 2);
			}

			ctx->module.instruments[i].volume_envelope.sustain_point =
				*((uint8_t*)(moddata + offset + 227));
			ctx->module.instruments[i].volume_envelope.loop_start_point =
				*((uint8_t*)(moddata + offset + 228));
			ctx->module.instruments[i].volume_envelope.loop_end_point =
				*((uint8_t*)(moddata + offset + 229));

			ctx->module.instruments[i].panning_envelope.sustain_point =
				*((uint8_t*)(moddata + offset + 230));
			ctx->module.instruments[i].panning_envelope.loop_start_point =
				*((uint8_t*)(moddata + offset + 231));
			ctx->module.instruments[i].panning_envelope.loop_end_point =
				*((uint8_t*)(moddata + offset + 232));

			uint8_t flags = *((uint8_t*)(moddata + offset + 233));
			ctx->module.instruments[i].volume_envelope.enabled = flags & (1 << 0);
			ctx->module.instruments[i].volume_envelope.sustain_enabled = flags & (1 << 1);
			ctx->module.instruments[i].volume_envelope.loop_enabled = flags & (1 << 2);

			flags = *((uint8_t*)(moddata + offset + 234));
			ctx->module.instruments[i].panning_envelope.enabled = flags & (1 << 0);
			ctx->module.instruments[i].panning_envelope.sustain_enabled = flags & (1 << 1);
			ctx->module.instruments[i].panning_envelope.loop_enabled = flags & (1 << 2);

			ctx->module.instruments[i].vibrato_type = *((uint8_t*)(moddata + offset + 235));
			ctx->module.instruments[i].vibrato_sweep = *((uint8_t*)(moddata + offset + 236));
			ctx->module.instruments[i].vibrato_depth = *((uint8_t*)(moddata + offset + 237));
			ctx->module.instruments[i].vibrato_rate = *((uint8_t*)(moddata + offset + 238));
			ctx->module.instruments[i].volume_fadeout = *((uint16_t*)(moddata + offset + 239));

			ctx->module.instruments[i].samples = (xm_sample_t*)mempool;
			mempool += ctx->module.instruments[i].num_samples * sizeof(xm_sample_t);
		} else {
			ctx->module.instruments[i].samples = NULL;
		}

		/* Instrument header size */
		offset += *((uint32_t*)(moddata + offset));

		for(uint16_t j = 0; j < ctx->module.instruments[i].num_samples; ++j) {
			/* Read sample header */
			ctx->module.instruments[i].samples[j].length = *((uint32_t*)(moddata + offset));
			ctx->module.instruments[i].samples[j].loop_start = *((uint32_t*)(moddata + offset + 4));
			ctx->module.instruments[i].samples[j].loop_length = *((uint32_t*)(moddata + offset + 8));
			ctx->module.instruments[i].samples[j].volume = 
				(float)(*((uint8_t*)(moddata + offset + 12))) / (float)0x40;
			ctx->module.instruments[i].samples[j].finetune = *((int8_t*)(moddata + offset + 13));

			uint8_t flags = *((uint8_t*)(moddata + offset + 14));
			if((flags & 3) == 0) {
				ctx->module.instruments[i].samples[j].loop_type = XM_NO_LOOP;
			} else if((flags & 3) == 1) {
				ctx->module.instruments[i].samples[j].loop_type = XM_FORWARD_LOOP;
			} else {
				ctx->module.instruments[i].samples[j].loop_type = XM_PING_PONG_LOOP;
			}

			ctx->module.instruments[i].samples[j].panning = 
				(float)(*((uint8_t*)(moddata + offset + 15))) / (float)0xFF;
			ctx->module.instruments[i].samples[j].relative_note = *((int8_t*)(moddata + offset + 16));

			memcpy(ctx->module.instruments[i].samples[j].name, moddata + 18, SAMPLE_NAME_LENGTH);

			ctx->module.instruments[i].samples[j].data = (float*)mempool;

			if((sample_is_16bit[j] = (flags & (1 << 4)))) {
				/* 16 bit sample */
				mempool += ctx->module.instruments[i].samples[j].length * (int)(sizeof(float) * 0.5);
				ctx->module.instruments[i].samples[j].loop_start /= 2;
				ctx->module.instruments[i].samples[j].loop_length /= 2;
				ctx->module.instruments[i].samples[j].length /= 2;
			} else {
				/* 8 bit sample */
				mempool += ctx->module.instruments[i].samples[j].length * sizeof(float);
			}

			offset += sample_header_size;
		}

		for(uint16_t j = 0; j < ctx->module.instruments[i].num_samples; ++j) {
			/* Read sample data */
			if(sample_is_16bit[j]) {
				int16_t* sample = (int16_t*)(moddata + offset);
				uint32_t length = ctx->module.instruments[i].samples[j].length;
				int16_t v = 0;
				for(uint32_t k = 0; k < length; ++k) {
					v = v + sample[k];
					ctx->module.instruments[i].samples[j].data[k] = (float)v / (float)(1 << 15);
				}
				offset += 2 * ctx->module.instruments[i].samples[j].length;
			} else {
				int8_t* sample = (int8_t*)(moddata + offset);
				uint32_t length = ctx->module.instruments[i].samples[j].length;
				int8_t v = 0;
				for(uint32_t k = 0; k < length; ++k) {
					v = v + sample[k];
					ctx->module.instruments[i].samples[j].data[k] = (float)v / (float)(1 << 7);
				}
				offset += ctx->module.instruments[i].samples[j].length;
			}
		}
	}

	return mempool;
}
