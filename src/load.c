/* Author: Romain "Artefact2" Dalmaso <artefact2@gmail.com> */
/* Contributor: Dan Spencer <dan@atomicpotato.net> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#include "xm_internal.h"

#define SAMPLE_HEADER_SIZE 40

#define OFFSET(ptr) do { \
		(ptr) = (void*)((intptr_t)(ptr) + (intptr_t)(*ctxp)); \
	} while(0)

/* .xm files are little-endian. */

/* Bounded reader macros.
 * If we attempt to read the buffer out-of-bounds, pretend that the buffer is
 * infinitely padded with zeroes.
 */
#define READ_U8_BOUND(offset, bound) (((offset) < (bound)) ? (*(uint8_t*)(moddata + (offset))) : 0)
#define READ_U16_BOUND(offset, bound) ((uint16_t)READ_U8_BOUND(offset, bound) | ((uint16_t)READ_U8_BOUND((offset) + 1, bound) << 8))
#define READ_U32_BOUND(offset, bound) ((uint32_t)READ_U16_BOUND(offset, bound) | ((uint32_t)READ_U16_BOUND((offset) + 2, bound) << 16))
#define READ_MEMCPY_BOUND(ptr, offset, length, bound) memcpy_pad(ptr, length, moddata, bound, offset)

#define READ_U8(offset) READ_U8_BOUND(offset, moddata_length)
#define READ_U16(offset) READ_U16_BOUND(offset, moddata_length)
#define READ_U32(offset) READ_U32_BOUND(offset, moddata_length)
#define READ_MEMCPY(ptr, offset, length) READ_MEMCPY_BOUND(ptr, offset, length, moddata_length)

struct xm_prescan_data_s {
	uint16_t num_channels;
	uint16_t num_patterns;
	uint16_t num_instruments;
	uint16_t num_samples;
	uint32_t num_rows;
	uint32_t samples_data_length;
	uint16_t pot_length;
};

/* ----- Static functions ----- */

static void memcpy_pad(void*, size_t, const void*, size_t, size_t);
static void xm_prescan_module(const char*, size_t, struct xm_prescan_data_s*);
static size_t xm_load_module_header(xm_context_t*, const char*, size_t);
static size_t xm_load_pattern(xm_context_t*, xm_pattern_t*, const char*, size_t, size_t);
static size_t xm_load_instrument(xm_context_t*, xm_instrument_t*, const char*, size_t, size_t);
static void xm_fix_envelope(xm_envelope_t*);
static size_t xm_load_sample_header(xm_context_t*, xm_sample_t*, bool*, const char*, size_t, size_t);
static size_t xm_load_sample_data(bool, uint32_t, int16_t*, const char*, size_t, size_t);

/* ----- Function definitions ----- */

static void memcpy_pad(void* dst, size_t dst_len, const void* src, size_t src_len, size_t offset) {
	uint8_t* dst_c = dst;
	const uint8_t* src_c = src;

	/* how many bytes can be copied without overrunning `src` */
	size_t copy_bytes = (src_len >= offset) ? (src_len - offset) : 0;
	copy_bytes = copy_bytes > dst_len ? dst_len : copy_bytes;

	__builtin_memcpy(dst_c, src_c + offset, copy_bytes);
	/* padded bytes */
	__builtin_memset(dst_c + copy_bytes, 0, dst_len - copy_bytes);
}



static void xm_prescan_module(const char* moddata, size_t moddata_length, struct xm_prescan_data_s* out) {
	size_t offset = 60; /* Skip the first header */

	/* Read the module header */
	out->pot_length = READ_U16(offset + 4);
	out->num_channels = READ_U16(offset + 8);
	out->num_patterns = READ_U16(offset + 10);
	out->num_instruments = READ_U16(offset + 12);
	out->num_samples = 0;
	out->num_rows = 0;
	out->samples_data_length = 0;

	/* Header size */
	offset += READ_U32(offset);

	/* Read pattern headers */
	for(uint16_t i = 0; i < out->num_patterns; ++i) {
		out->num_rows += READ_U16(offset + 5);

		/* Pattern header length + packed pattern data size */
		offset += READ_U32(offset) + READ_U16(offset + 7);
	}

	/* Read instrument headers */
	for(uint16_t i = 0; i < out->num_instruments; ++i) {
		uint16_t num_samples = READ_U16(offset + 27);
		uint32_t inst_samples_bytes = 0;
		out->num_samples += num_samples;

		#if XM_DEFENSIVE
		/* Notice that, even if there's a "sample header size" in the
		   instrument header, that value seems ignored, and might even
		   be wrong in some corrupted modules. */
		if(num_samples > 0) {
			uint32_t sample_header_size = READ_U32(offset + 29);
			if(sample_header_size != SAMPLE_HEADER_SIZE) {
				NOTICE("ignoring dodgy sample header size (%d) for instrument %d", sample_header_size, i+1);
			}
		}
		#endif

		/* Instrument header size */
		offset += READ_U32(offset);

		for(uint16_t j = 0; j < num_samples; ++j) {
			uint32_t sample_length = READ_U32(offset);
			if(READ_U8(offset + 14) & 64) {
				/* 16-bit sample data */
				#if XM_DEFENSIVE
				if(sample_length % 2) {
					NOTICE("sample %d of instrument %d is 16-bit with an odd length!", j, i+1);
				}
				#endif
				out->samples_data_length += sample_length/2;
			} else {
				/* 8-bit sample data */
				out->samples_data_length += sample_length;
			}
			inst_samples_bytes += sample_length;
			offset += SAMPLE_HEADER_SIZE;
		}

		offset += inst_samples_bytes;
	}

	DEBUG("read %d patterns, %d channels, %d rows, %d instruments, %d samples, %d sample frames, %d pot length",
	      out->num_patterns, out->num_channels, out->num_rows,
	      out->num_instruments, out->num_samples,
	      out->samples_data_length, out->pot_length);
}

static size_t xm_load_module_header(xm_context_t* ctx, const char* moddata, size_t moddata_length) {
	size_t offset = 0;
	xm_module_t* mod = &(ctx->module);

	/* Read XM header */
#if XM_STRINGS
	READ_MEMCPY(mod->name, offset + 17, MODULE_NAME_LENGTH);
	READ_MEMCPY(mod->trackername, offset + 38, TRACKER_NAME_LENGTH);
#endif
	offset += 60;

	/* Read module header */
	uint32_t header_size = READ_U32(offset);

	mod->length = READ_U16(offset + 4);
	mod->restart_position = READ_U16(offset + 6);
	mod->num_channels = READ_U16(offset + 8);
	mod->num_patterns = READ_U16(offset + 10);
	mod->num_instruments = READ_U16(offset + 12);

	if(mod->length > PATTERN_ORDER_TABLE_LENGTH) {
		NOTICE("clamping module pot length %d to %d\n", mod->length, PATTERN_ORDER_TABLE_LENGTH);
		mod->length = PATTERN_ORDER_TABLE_LENGTH;
	}

	uint16_t flags = READ_U32(offset + 14);
	#if XM_FREQUENCY_TYPES == 3
	mod->frequency_type = (flags & 1) ? XM_LINEAR_FREQUENCIES : XM_AMIGA_FREQUENCIES;
	#endif
	#if XM_DEFENSIVE
	if(flags & 0b11111110) {
		NOTICE("unknown flags set in module header (%d)", flags);
	}
	#endif

	ctx->tempo = READ_U16(offset + 16);
	ctx->bpm = READ_U16(offset + 18);

	/* Read POT and delete invalid patterns */
	READ_MEMCPY(mod->pattern_table, offset + 20, PATTERN_ORDER_TABLE_LENGTH);
	for(uint16_t i = 0; i < mod->length; ++i) {
		if(mod->pattern_table[i] < mod->num_patterns) {
			continue;
		}
		NOTICE("removing invalid pattern %d in pattern order table", mod->pattern_table[i]);
		mod->length -= 1;
		__builtin_memmove(mod->pattern_table + i,
		                  mod->pattern_table + i + 1,
		                  mod->length - i);
	}

	return offset + header_size;
}

static size_t xm_load_pattern(xm_context_t* ctx, xm_pattern_t* pat, const char* moddata, size_t moddata_length, size_t offset) {
	uint16_t packed_patterndata_size = READ_U16(offset + 7);
	pat->num_rows = READ_U16(offset + 5);
	pat->slots_index = ctx->module.num_rows * ctx->module.num_channels;
	ctx->module.num_rows += pat->num_rows;
	xm_pattern_slot_t* slots = ctx->pattern_slots + pat->slots_index;

	#if XM_DEFENSIVE
	uint8_t packing_type = READ_U8(offset + 4);
	if(packing_type != 0) {
		NOTICE("unknown packing type %d in pattern", packing_type);
	}
	#endif

	/* Pattern header length */
	offset += READ_U32(offset);

	if(packed_patterndata_size == 0) {
		/* No pattern data is present */
		/* NB: pattern slot data is already zeroed */
		return offset;
	}

	/* j counts bytes in the file, k counts pattern slots */
	for(uint16_t j = 0, k = 0; j < packed_patterndata_size; ++k) {
		uint8_t note = READ_U8(offset + j);
		xm_pattern_slot_t* slot = slots + k;

		if(note & (1 << 7)) {
			/* MSB is set, this is a compressed packet */
			++j;

			if(note & (1 << 0)) {
				/* Note follows */
				slot->note = READ_U8(offset + j);
				++j;
			}

			if(note & (1 << 1)) {
				/* Instrument follows */
				slot->instrument = READ_U8(offset + j);
				++j;
			}

			if(note & (1 << 2)) {
				/* Volume column follows */
				slot->volume_column = READ_U8(offset + j);
				++j;
			}

			if(note & (1 << 3)) {
				/* Effect follows */
				slot->effect_type = READ_U8(offset + j);
				++j;
			}

			if(note & (1 << 4)) {
				/* Effect parameter follows */
				slot->effect_param = READ_U8(offset + j);
				++j;
			}
		} else {
			/* Uncompressed packet */
			slot->note = note;
			slot->instrument = READ_U8(offset + j + 1);
			slot->volume_column = READ_U8(offset + j + 2);
			slot->effect_type = READ_U8(offset + j + 3);
			slot->effect_param = READ_U8(offset + j + 4);
			j += 5;
		}
	}

	return offset + packed_patterndata_size;
}

static size_t xm_load_instrument(xm_context_t* ctx, xm_instrument_t* instr, const char* moddata, size_t moddata_length, size_t offset) {
	#if XM_STRINGS
	READ_MEMCPY(instr->name, offset + 4, INSTRUMENT_NAME_LENGTH);
	instr->name[INSTRUMENT_NAME_LENGTH] = 0;
	#endif

	uint32_t ins_header_size = READ_U32(offset);
	/* Original FT2 would load instruments with a direct read into the
           instrument data structure that was previously zeroed. This means that
           if the declared length was less than INSTRUMENT_HEADER_LENGTH, all
           excess data would be zeroed. This is used by the XM compressor
           BoobieSqueezer. To implement this, bound all reads to the header
           size. */
	size_t orig_moddata_length = moddata_length;
	moddata_length = offset + ins_header_size;

	#if XM_DEFENSIVE
	uint8_t type = READ_U8(offset + 26);
	if(type != 0) {
		NOTICE("ignoring non-zero type %d on instrument %ld",
		       type, (instr - ctx->instruments) + 1);
	}
	#endif

	instr->num_samples = READ_U16(offset + 27);
	if(instr->num_samples == 0) {
		return offset + ins_header_size;
	}

	/* Read extra header properties */
	READ_MEMCPY(instr->sample_of_notes, offset + 33, NUM_NOTES);
	READ_MEMCPY(instr->volume_envelope.points, offset + 129, 48);
	READ_MEMCPY(instr->panning_envelope.points, offset + 177, 48);

	instr->volume_envelope.num_points = READ_U8(offset + 225);
	if (instr->volume_envelope.num_points > MAX_ENVELOPE_POINTS) {
		NOTICE("clamping volume envelope num_points (%d -> %d) for instrument %ld",
		       instr->volume_envelope.num_points,
		       MAX_ENVELOPE_POINTS,
		       (instr - ctx->instruments) + 1);
		instr->volume_envelope.num_points = MAX_ENVELOPE_POINTS;
	}

	instr->panning_envelope.num_points = READ_U8(offset + 226);
	if (instr->panning_envelope.num_points > MAX_ENVELOPE_POINTS) {
		NOTICE("clamping panning envelope num_points (%d -> %d) for instrument %ld",
		       instr->panning_envelope.num_points,
		       MAX_ENVELOPE_POINTS,
		       (instr - ctx->instruments) + 1);
		instr->panning_envelope.num_points = MAX_ENVELOPE_POINTS;
	}

	instr->volume_envelope.sustain_point = READ_U8(offset + 227);
	instr->volume_envelope.loop_start_point = READ_U8(offset + 228);
	instr->volume_envelope.loop_end_point = READ_U8(offset + 229);

	instr->panning_envelope.sustain_point = READ_U8(offset + 230);
	instr->panning_envelope.loop_start_point = READ_U8(offset + 231);
	instr->panning_envelope.loop_end_point = READ_U8(offset + 232);

	// Fix broken modules with loop points outside of defined points
	xm_fix_envelope(&(instr->volume_envelope));
	xm_fix_envelope(&(instr->panning_envelope));

	uint8_t flags = READ_U8(offset + 233);
	instr->volume_envelope.enabled = flags & (1 << 0);
	instr->volume_envelope.sustain_enabled = flags & (1 << 1);
	instr->volume_envelope.loop_enabled = flags & (1 << 2);

	flags = READ_U8(offset + 234);
	instr->panning_envelope.enabled = flags & (1 << 0);
	instr->panning_envelope.sustain_enabled = flags & (1 << 1);
	instr->panning_envelope.loop_enabled = flags & (1 << 2);

	instr->vibrato_type = READ_U8(offset + 235);
	if(instr->vibrato_type == 2) {
		instr->vibrato_type = 1;
	} else if(instr->vibrato_type == 1) {
		instr->vibrato_type = 2;
	}
	instr->vibrato_sweep = READ_U8(offset + 236);
	instr->vibrato_depth = READ_U8(offset + 237);
	instr->vibrato_rate = READ_U8(offset + 238);
	instr->volume_fadeout = READ_U16(offset + 239);

	offset += ins_header_size;
	moddata_length = orig_moddata_length;

	/* Read sample headers */
	instr->samples_index = ctx->module.num_samples;
	ctx->module.num_samples += instr->num_samples;
	bool samples_16bit[instr->num_samples]; /* true => 16 bit sample */
	for(uint16_t i = 0; i < instr->num_samples; ++i) {
		offset = xm_load_sample_header(ctx, ctx->samples + instr->samples_index + i, samples_16bit + i, moddata, moddata_length, offset);
	}

	/* Read sample data */
	for(uint16_t i = 0; i < instr->num_samples; ++i) {
		xm_sample_t* s = ctx->samples + instr->samples_index + i;
		offset = xm_load_sample_data(samples_16bit[i], s->length, ctx->samples_data + s->index, moddata, moddata_length, offset);
	}

	return offset;
}

static void xm_fix_envelope(xm_envelope_t* env) {
	if (env->num_points > 0) {
		if(env->loop_start_point >= env->num_points) {
			NOTICE("clamped invalid envelope loop start point");
			env->loop_start_point = env->num_points - 1;
		}
		if(env->loop_end_point >= env->num_points) {
			NOTICE("clamped invalid envelope loop end point");
			env->loop_end_point = env->num_points - 1;
		}
	}
}

static size_t xm_load_sample_header(xm_context_t* ctx,
                                    xm_sample_t* sample,
                                    bool* is_16bit,
                                    const char* moddata,
                                    size_t moddata_length,
                                    size_t offset) {
	sample->length = READ_U32(offset);
	sample->loop_start = READ_U32(offset + 4);
	sample->loop_length = READ_U32(offset + 8);
	sample->loop_end = sample->loop_start + sample->loop_length;
	sample->volume = (float)READ_U8(offset + 12) / (float)0x40;
	sample->finetune = (int8_t)READ_U8(offset + 13);

	/* Fix invalid loop definitions */
	if (sample->loop_start > sample->length) {
		NOTICE("fixing invalid sample loop start");
		sample->loop_start = sample->length;
		sample->loop_length = sample->loop_end - sample->loop_start;
	}
	if (sample->loop_end > sample->length) {
		NOTICE("fixing invalid sample loop end");
		sample->loop_end = sample->length;
		sample->loop_length = sample->loop_end - sample->loop_start;
	}

	uint8_t flags = READ_U8(offset + 14);
	switch(flags & 0b00000011) {
	#if XM_DEFENSIVE
	default:
		NOTICE("unknown loop type (%d) in sample", flags & 3);
		__attribute__((fallthrough));
	#endif
	case 0:
		sample->loop_type = XM_NO_LOOP;
		break;
	case 1:
		sample->loop_type = XM_FORWARD_LOOP;
		break;
	case 2:
		sample->loop_type = XM_PING_PONG_LOOP;
		break;
	}

	/* Fix zero length loops */
	if(sample->loop_length == 0 && sample->loop_type != XM_NO_LOOP) {
		NOTICE("fixing loop type for sample with loop length 0");
		sample->loop_type = XM_NO_LOOP;
	}

	#if XM_DEFENSIVE
	if(flags & 0b11101100) {
		NOTICE("ignoring unknown flags (%d) in sample", flags);
	}
	#endif

	sample->panning = (float)READ_U8(offset + 15) / (float)0xFF;
	sample->relative_note = (int8_t)READ_U8(offset + 16);

	#if XM_STRINGS
	READ_MEMCPY(sample->name, offset + 18, SAMPLE_NAME_LENGTH);
	sample->name[SAMPLE_NAME_LENGTH] = 0;
	#endif

	*is_16bit = flags & 0b00010000;
	if(*is_16bit) {
		sample->loop_start >>= 1;
		sample->loop_length >>= 1;
		sample->loop_end >>= 1;
		sample->length >>= 1;
	}

	sample->index = ctx->module.samples_data_length;
	ctx->module.samples_data_length += sample->length;

	return offset + SAMPLE_HEADER_SIZE;
}

static size_t xm_load_sample_data(bool is_16bit,
                                  uint32_t length,
                                  int16_t* out,
                                  const char* moddata,
                                  size_t moddata_length,
                                  size_t offset) {
	int16_t v = 0;

	if(is_16bit) {
		for(uint32_t k = 0; k < length; ++k) {
			v = v + (int16_t)READ_U16(offset + (k << 1));
			out[k] = v;
		}
		offset += length << 1;
	} else {
		for(uint32_t k = 0; k < length; ++k) {
			v = v + (int16_t)READ_U8(offset + k);
			out[k] = (v << 8);
		}
		offset += length;
	}

	return offset;
}



int xm_create_context(xm_context_t** ctxp, const char* moddata, uint32_t rate) {
	return xm_create_context_safe(ctxp, moddata, INT32_MAX, rate);
}

int xm_create_context_safe(xm_context_t** ctxp, const char* moddata, uint32_t moddata_length, uint32_t rate) {
	#if XM_DEFENSIVE
	if(moddata_length < 60
	   || memcmp("Extended Module: ", moddata, 17) != 0
	   || moddata[37] != 0x1A
	   || moddata[59] != 0x01
	   || moddata[58] != 0x04) {
		NOTICE("input data does not look like a supported XM module");
		return 1;
	}
	#endif

	struct xm_prescan_data_s p;
	xm_prescan_module(moddata, moddata_length, &p);
	ssize_t mempool_size = sizeof(xm_context_t)
		+ sizeof(xm_pattern_t) * p.num_patterns
		+ sizeof(xm_pattern_slot_t) * p.num_rows * p.num_channels
		+ sizeof(xm_instrument_t) * p.num_instruments
		+ sizeof(xm_sample_t) * p.num_samples
		+ sizeof(int16_t) * p.samples_data_length
		+ sizeof(xm_channel_context_t) * p.num_channels
		+ sizeof(uint8_t) * MAX_ROWS_PER_PATTERN * p.pot_length;
	char* mempool = calloc(mempool_size, 1);

	#if XM_DEFENSIVE
	if(mempool == NULL) {
		NOTICE("failed to allocate memory for context");
		return 2;
	}
	#endif

	xm_context_t* ctx = (*ctxp = (xm_context_t*)mempool);
	mempool += sizeof(xm_context_t);
	ctx->patterns = (xm_pattern_t*)mempool;
	mempool += sizeof(xm_pattern_t) * p.num_patterns;
	ctx->pattern_slots = (xm_pattern_slot_t*)mempool;
	mempool += sizeof(xm_pattern_slot_t) * p.num_rows * p.num_channels;
	ctx->instruments = (xm_instrument_t*)mempool;
	mempool += sizeof(xm_instrument_t) * p.num_instruments;
	ctx->samples = (xm_sample_t*)mempool;
	mempool += sizeof(xm_sample_t) * p.num_samples;
	ctx->samples_data = (int16_t*)mempool;
	mempool += sizeof(int16_t) * p.samples_data_length;
	ctx->channels = (xm_channel_context_t*)mempool;
	mempool += sizeof(xm_channel_context_t) * p.num_channels;
	ctx->row_loop_count = (uint8_t*)mempool;
	mempool += sizeof(uint8_t) * MAX_ROWS_PER_PATTERN * p.pot_length;
	assert(mempool - (char*)ctx == mempool_size);

	ctx->rate = rate;
	ctx->global_volume = 1.f;
	ctx->amplification = .25f; /* XXX: some bad modules may still clip. Find out something better. */

#if XM_RAMPING
	ctx->volume_ramp = (1.f / 128.f);
#endif

	/* Read module header */
	size_t offset = xm_load_module_header(ctx, moddata, moddata_length);

	/* Read pattern headers + slots */
	for(uint16_t i = 0; i < p.num_patterns; ++i) {
		offset = xm_load_pattern(ctx, ctx->patterns + i, moddata, moddata_length, offset);
	}

	/* Read instruments, samples and sample data */
	for(uint16_t i = 0; i < p.num_instruments; ++i) {
		offset = xm_load_instrument(ctx, ctx->instruments + i, moddata, moddata_length, offset);
	}

	/* Initialise non-zero initial fields of channel ctx */
	for(uint8_t i = 0; i < p.num_channels; ++i) {
		xm_channel_context_t* ch = ctx->channels + i;

		ch->ping = true;
		ch->vibrato_waveform_retrigger = true;
		ch->tremolo_waveform_retrigger = true;
		ch->volume = ch->volume_envelope_volume = ch->fadeout_volume = 1.0f;
		ch->panning = ch->panning_envelope_panning = .5f;
	}

	return 0;
}

void xm_free_context(xm_context_t* context) {
	free(context);
}

void xm_create_context_from_libxmize(xm_context_t** ctxp, char* libxmized, uint32_t rate) {
	*ctxp = (void*)libxmized;
	(*ctxp)->rate = rate;

	/* Reverse steps of libxmize.c */
	OFFSET((*ctxp)->patterns);
	OFFSET((*ctxp)->pattern_slots);
	OFFSET((*ctxp)->instruments);
	OFFSET((*ctxp)->samples);
	OFFSET((*ctxp)->samples_data);
	OFFSET((*ctxp)->channels);
	OFFSET((*ctxp)->row_loop_count);

	/* XXX */
	/* if(XM_LIBXMIZE_DELTA_SAMPLES) { */
	/* 	if((*ctxp)->module.instruments[i].samples[j].length > 1) { */
	/* 		if((*ctxp)->module.instruments[i].samples[j].bits == 8) { */
	/* 			for(size_t k = 1; k < (*ctxp)->module.instruments[i].samples[j].length; ++k) { */
	/* 				(*ctxp)->module.instruments[i].samples[j].data8[k] += (*ctxp)->module.instruments[i].samples[j].data8[k-1]; */
	/* 			} */
	/* 		} else { */
	/* 			for(size_t k = 1; k < (*ctxp)->module.instruments[i].samples[j].length; ++k) { */
	/* 				(*ctxp)->module.instruments[i].samples[j].data16[k] += (*ctxp)->module.instruments[i].samples[j].data16[k-1]; */
	/* 			} */
	/* 		} */
	/* 	} */
	/* } */
}
