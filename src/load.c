/* Author: Romain "Artefact2" Dalmaso <artefact2@gmail.com> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#include "xm_internal.h"
#include <string.h>

#define READ(type, pointer) (*((type *)(pointer)))

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

	num_channels = READ(uint16_t, moddata + offset + 8);
	DEBUG("got %i channels", num_channels);

	num_patterns = READ(uint16_t, moddata + offset + 10);
	memory_needed += num_patterns * sizeof(xm_pattern_t);
	DEBUG("got %i patterns", num_patterns);

	num_instruments = READ(uint16_t, moddata + offset + 12);
	memory_needed += num_instruments * sizeof(xm_instrument_t);
	DEBUG("got %i instruments", num_instruments);

	/* Header size */
	offset += READ(uint32_t, moddata + offset);

	/* Read pattern headers */
	for(uint16_t i = 0; i < num_patterns; ++i) {
		uint16_t num_rows;

		num_rows = READ(uint16_t, moddata + offset + 5);
		memory_needed += num_rows * num_channels * sizeof(xm_pattern_slot_t);

		/* Pattern header length + packed pattern data size */
		offset += READ(uint32_t, moddata + offset) + READ(uint16_t, moddata + offset + 7);
	}

	/* Read instrument headers */
	for(uint16_t i = 0; i < num_instruments; ++i) {
		uint16_t num_samples;
		uint32_t sample_header_size = 0;
		uint32_t sample_size_aggregate = 0;

		num_samples = READ(uint16_t, moddata + offset + 27);
		memory_needed += num_samples * sizeof(xm_sample_t);

		if(num_samples > 0) {
			sample_header_size = READ(uint32_t, moddata + offset + 29);
		}

		/* Instrument header size */
		offset += READ(uint32_t, moddata + offset);

		for(uint16_t j = 0; j < num_samples; ++j) {
			uint32_t sample_size;
			uint8_t flags;

			sample_size = READ(uint32_t, moddata + offset);
			flags = READ(uint8_t, moddata + offset + 14);
			sample_size_aggregate += sample_size;

			if(flags & (1 << 4)) {
				/* 16 bit sample */
				memory_needed += sample_size * (sizeof(float) >> 1);
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
	xm_module_t* mod = &(ctx->module);

	/* Read XM header */
	memcpy(mod->name, moddata + offset + 17, MODULE_NAME_LENGTH);
	memcpy(mod->trackername, moddata + offset + 38, TRACKER_NAME_LENGTH);
	offset += 60;

	/* Read module header */
	uint32_t header_size = READ(uint32_t, moddata + offset);

	mod->length = READ(uint16_t, moddata + offset + 4);
	mod->restart_position = READ(uint16_t, moddata + offset + 6);
	mod->num_channels = READ(uint16_t, moddata + offset + 8);
	mod->num_patterns = READ(uint16_t, moddata + offset + 10);
	mod->num_instruments = READ(uint16_t, moddata + offset + 12);

	mod->patterns = (xm_pattern_t*)mempool;
	mempool += mod->num_patterns * sizeof(xm_pattern_t);

    mod->instruments = (xm_instrument_t*)mempool;
	mempool += mod->num_instruments * sizeof(xm_instrument_t);

	uint16_t flags = READ(uint32_t, moddata + offset + 14);
	mod->frequency_type = (flags & (1 << 0)) ? XM_LINEAR_FREQUENCIES : XM_AMIGA_FREQUENCIES;

	ctx->tempo = READ(uint16_t, moddata + offset + 16);
	ctx->bpm = READ(uint16_t, moddata + offset + 18);

	memcpy(mod->pattern_table, moddata + offset + 20, PATTERN_ORDER_TABLE_LENGTH);
	offset += header_size;

	/* Read patterns */
	for(uint16_t i = 0; i < mod->num_patterns; ++i) {
		uint16_t packed_patterndata_size = READ(uint16_t, moddata + offset + 7);
		xm_pattern_t* pat = mod->patterns + i;

		pat->num_rows = READ(uint16_t, moddata + offset + 5);
		pat->slots = (xm_pattern_slot_t*)mempool;
		mempool += mod->num_channels * pat->num_rows * sizeof(xm_pattern_slot_t);

		/* Pattern header length */
		offset += READ(uint32_t, moddata + offset);

		if(packed_patterndata_size == 0) {
			/* No pattern data is present */
			memset(pat->slots, 0, sizeof(xm_pattern_slot_t) * pat->num_rows * mod->num_channels);
		} else {
			/* This isn't your typical for loop */
			for(uint16_t j = 0, k = 0; j < packed_patterndata_size; ++k) {
				uint8_t note = READ(uint8_t,  moddata + offset + j);
				xm_pattern_slot_t* slot = pat->slots + k;

				if(note & (1 << 7)) {
					/* MSB is set, this is a compressed packet */
					++j;

					if(note & (1 << 0)) {
						/* Note follows */
						slot->note = READ(uint8_t, moddata + offset + j);
						++j;							
					} else {
						slot->note = 0;
					}

					if(note & (1 << 1)) {
						/* Instrument follows */
						slot->instrument = READ(uint8_t, moddata + offset + j);
						++j;							
					} else {
						slot->instrument = 0;
					}

					if(note & (1 << 2)) {
						/* Volume column follows */
						slot->volume_column = READ(uint8_t, moddata + offset + j);
						++j;							
					} else {
						slot->volume_column = 0;
					}

					if(note & (1 << 3)) {
						/* Effect follows */
						slot->effect_type = READ(uint8_t, moddata + offset + j);
						++j;							
					} else {
						slot->effect_type = 0;
					}

					if(note & (1 << 4)) {
						/* Effect parameter follows */
						slot->effect_param = READ(uint8_t, moddata + offset + j);
						++j;							
					} else {
						slot->effect_param = 0;
					}
				} else {
					/* Uncompressed packet */
					slot->note = note;
					slot->instrument = READ(uint8_t, moddata + offset + j + 1);
					slot->volume_column = READ(uint8_t, moddata + offset + j + 2);
					slot->effect_type = READ(uint8_t, moddata + offset + j + 3);
					slot->effect_param = READ(uint8_t, moddata + offset + j + 4);
					j += 5;
				}
			}
		}

		offset += packed_patterndata_size;
	}

	/* Read instruments */
	for(uint16_t i = 0; i < ctx->module.num_instruments; ++i) {
		uint32_t sample_header_size = 0;
		xm_instrument_t* instr = mod->instruments + i;

		memcpy(instr->name, moddata + offset + 4, INSTRUMENT_NAME_LENGTH);
	    instr->num_samples = READ(uint16_t, moddata + offset + 27);

		bool sample_is_16bit[instr->num_samples]; /* VLA */

		if(instr->num_samples > 0) {
			/* Read extra header properties */
			sample_header_size = READ(uint32_t, moddata + offset + 29);
			memcpy(instr->sample_of_notes, moddata + offset + 33, NUM_NOTES);

			instr->volume_envelope.num_points = READ(uint8_t, moddata + offset + 225);
			instr->panning_envelope.num_points = READ(uint8_t, moddata + offset + 226);

			for(uint8_t j = 0; j < instr->volume_envelope.num_points; ++j) {
				instr->volume_envelope.points[j].frame = READ(uint16_t, moddata + offset + 129 + 4 * j);
				instr->volume_envelope.points[j].value = READ(uint16_t, moddata + offset + 129 + 4 * j + 2);
			}

			for(uint8_t j = 0; j < instr->panning_envelope.num_points; ++j) {
				instr->panning_envelope.points[j].frame = READ(uint16_t, moddata + offset + 177 + 4 * j);
				instr->panning_envelope.points[j].value = READ(uint16_t, moddata + offset + 177 + 4 * j + 2);
			}

			instr->volume_envelope.sustain_point = READ(uint8_t, moddata + offset + 227);
			instr->volume_envelope.loop_start_point = READ(uint8_t, moddata + offset + 228);
			instr->volume_envelope.loop_end_point = READ(uint8_t, moddata + offset + 229);

			instr->panning_envelope.sustain_point = READ(uint8_t, moddata + offset + 230);
			instr->panning_envelope.loop_start_point = READ(uint8_t, moddata + offset + 231);
			instr->panning_envelope.loop_end_point = READ(uint8_t, moddata + offset + 232);

			uint8_t flags = READ(uint8_t, moddata + offset + 233);
			instr->volume_envelope.enabled = flags & (1 << 0);
			instr->volume_envelope.sustain_enabled = flags & (1 << 1);
			instr->volume_envelope.loop_enabled = flags & (1 << 2);

			flags = READ(uint8_t, moddata + offset + 234);
			instr->panning_envelope.enabled = flags & (1 << 0);
			instr->panning_envelope.sustain_enabled = flags & (1 << 1);
			instr->panning_envelope.loop_enabled = flags & (1 << 2);

			instr->vibrato_type = READ(uint8_t, moddata + offset + 235);
			instr->vibrato_sweep = READ(uint8_t, moddata + offset + 236);
			instr->vibrato_depth = READ(uint8_t, moddata + offset + 237);
			instr->vibrato_rate = READ(uint8_t, moddata + offset + 238);
			instr->volume_fadeout = READ(uint16_t, moddata + offset + 239);

			instr->samples = (xm_sample_t*)mempool;
			mempool += instr->num_samples * sizeof(xm_sample_t);
		} else {
			instr->samples = NULL;
		}

		/* Instrument header size */
		offset += READ(uint32_t, moddata + offset);

		for(uint16_t j = 0; j < instr->num_samples; ++j) {
			/* Read sample header */
			xm_sample_t* sample = instr->samples + j;

			sample->length = READ(uint32_t, moddata + offset);
			sample->loop_start = READ(uint32_t, moddata + offset + 4);
			sample->loop_length = READ(uint32_t, moddata + offset + 8);
			sample->volume = (float)READ(uint8_t, moddata + offset + 12) / (float)0x40;
			sample->finetune = READ(int8_t, moddata + offset + 13);

			uint8_t flags = READ(uint8_t, moddata + offset + 14);
			if((flags & 3) == 0) {
				sample->loop_type = XM_NO_LOOP;
			} else if((flags & 3) == 1) {
				sample->loop_type = XM_FORWARD_LOOP;
			} else {
				sample->loop_type = XM_PING_PONG_LOOP;
			}

			sample->panning = (float)READ(uint8_t, moddata + offset + 15) / (float)0xFF;
			sample->relative_note = READ(int8_t, moddata + offset + 16);
			memcpy(sample->name, moddata + 18, SAMPLE_NAME_LENGTH);
			sample->data = (float*)mempool;

			if((sample_is_16bit[j] = (flags & (1 << 4)))) {
				/* 16 bit sample */
				mempool += sample->length * (sizeof(float) >> 1);
				sample->loop_start >>= 1;
				sample->loop_length >>= 1;
				sample->length >>= 1;
			} else {
				/* 8 bit sample */
				mempool += sample->length * sizeof(float);
			}

			offset += sample_header_size;
		}

		for(uint16_t j = 0; j < instr->num_samples; ++j) {
			/* Read sample data */
			xm_sample_t* sample = instr->samples + j;
			uint32_t length = sample->length;

			if(sample_is_16bit[j]) {
				int16_t* sampledata = (int16_t*)(moddata + offset);
				int16_t v = 0;
				for(uint32_t k = 0; k < length; ++k) {
					v = v + sampledata[k];
					sample->data[k] = (float)v / (float)(1 << 15);
				}
				offset += sample->length << 1;
			} else {
				int8_t* sampledata = (int8_t*)(moddata + offset);
				int8_t v = 0;
				for(uint32_t k = 0; k < length; ++k) {
					v = v + sampledata[k];
					sample->data[k] = (float)v / (float)(1 << 7);
				}
				offset += sample->length;
			}
		}
	}

	return mempool;
}
