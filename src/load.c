/* Author: Romain "Artefact2" Dalmaso <artefact2@gmail.com> */
/* Contributor: Dan Spencer <dan@atomicpotato.net> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#include "xm_internal.h"

#define EMPTY_PATTERN_NUM_ROWS 64
#define SAMPLE_HEADER_SIZE 40
#define SAMPLE_FLAG_16B 0b00010000
#define SAMPLE_FLAG_PING_PONG 0b00000010
#define SAMPLE_FLAG_FORWARD 0b00000001
#define ENVELOPE_FLAG_ENABLED 0b00000001
#define ENVELOPE_FLAG_SUSTAIN 0b00000010
#define ENVELOPE_FLAG_LOOP 0b00000100

#define ASSERT_ALIGNED(ptr, type)                                       \
	assert((uintptr_t)((void*)(ptr)) % alignof(type) == 0)

/* Bounded reader macros (assume little-endian, .XM files always are).
 * If we attempt to read the buffer out-of-bounds, pretend that the buffer is
 * infinitely padded with zeroes.
 */
#define READ_U8_BOUND(offset, bound) ((uint8_t)(((offset) < (bound)) ? (*(uint8_t*)(moddata + (offset))) : 0))
#define READ_U16_BOUND(offset, bound) ((uint16_t)((uint16_t)READ_U8_BOUND(offset, bound) | ((uint16_t)READ_U8_BOUND((offset) + 1, bound) << 8)))
#define READ_U32_BOUND(offset, bound) ((uint32_t)((uint32_t)READ_U16_BOUND(offset, bound) | ((uint32_t)READ_U16_BOUND((offset) + 2, bound) << 16)))
#define READ_MEMCPY_BOUND(ptr, offset, length, bound) memcpy_pad(ptr, length, moddata, bound, offset)

#define READ_U8(offset) READ_U8_BOUND(offset, moddata_length)
#define READ_U16(offset) READ_U16_BOUND(offset, moddata_length)
#define READ_U32(offset) READ_U32_BOUND(offset, moddata_length)
#define READ_MEMCPY(ptr, offset, length) READ_MEMCPY_BOUND(ptr, offset, length, moddata_length)

#define TRIM_SAMPLE_LENGTH(length, loop_start, loop_length, flags) \
	(flags & (SAMPLE_FLAG_PING_PONG | SAMPLE_FLAG_FORWARD) ?	\
	 ((loop_start > length ? length :				\
	  (loop_start + (loop_start + loop_length > length ? 0 : loop_length)) \
	   )) : length)

struct xm_prescan_data_s {
	uint32_t context_size;
	uint32_t num_rows;
	uint32_t samples_data_length;
	uint16_t num_patterns;
	uint16_t num_samples;
	uint16_t pot_length;
	uint8_t num_channels;
	uint8_t num_instruments;
};
const uint8_t XM_PRESCAN_DATA_SIZE = sizeof(xm_prescan_data_t);

/* ----- Static functions ----- */

static void memcpy_pad(void*, size_t, const void*, size_t, size_t);
static uint32_t xm_load_module_header(xm_context_t*, const char*, uint32_t);
static uint32_t xm_load_pattern(xm_context_t*, xm_pattern_t*, const char*, uint32_t, uint32_t);
static uint32_t xm_load_instrument(xm_context_t*, xm_instrument_t*, const char*, uint32_t, uint32_t);
static void xm_load_envelope_points(xm_envelope_t*, const char*);
static void xm_check_and_fix_envelope(xm_envelope_t*, uint8_t);
static uint32_t xm_load_sample_header(xm_sample_t*, bool*, const char*, uint32_t, uint32_t);
static void xm_load_8b_sample_data(uint32_t, xm_sample_point_t*, const char*, uint32_t, uint32_t);
static void xm_load_16b_sample_data(uint32_t, xm_sample_point_t*, const char*, uint32_t, uint32_t);
static int8_t xm_dither_16b_8b(int16_t);

static uint64_t xm_fnv1a(const unsigned char*, uint32_t);

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



bool xm_prescan_module(const char* moddata, uint32_t moddata_length, xm_prescan_data_t* out) {
	#if XM_DEFENSIVE
	if(moddata_length < 60
	   || memcmp("Extended Module: ", moddata, 17) != 0
	   || moddata[37] != 0x1A
	   || moddata[59] != 0x01
	   || moddata[58] != 0x04) {
		NOTICE("input data does not look like a supported XM module");
		return false;
	}
	#endif

	uint32_t offset = 60; /* Skip the first header */

	/* Read the module header */
	out->pot_length = READ_U16(offset + 4);
	uint16_t num_channels = READ_U16(offset + 8);
	if(num_channels > MAX_CHANNELS) {
		NOTICE("module has too many channels (%u > %u)",
		       num_channels, MAX_CHANNELS);
		return false;
	}
	out->num_channels = (uint8_t)num_channels;
	out->num_patterns = READ_U16(offset + 10);
	if(out->num_patterns > MAX_PATTERNS) {
		NOTICE("module has too many patterns (%u > %u)",
		       out->num_patterns, MAX_PATTERNS);
		return false;
	}
	uint16_t num_instruments = READ_U16(offset + 12);
	if(num_instruments > MAX_INSTRUMENTS) {
		NOTICE("module has too many instruments (%u > %u)",
		       num_instruments, MAX_INSTRUMENTS);
		return false;
	}
	static_assert(MAX_INSTRUMENTS <= UINT8_MAX);
	out->num_instruments = (uint8_t)num_instruments;
	out->num_samples = 0;
	out->num_rows = 0;
	out->samples_data_length = 0;

	uint8_t pot[PATTERN_ORDER_TABLE_LENGTH];
	READ_MEMCPY(pot, offset + 20, PATTERN_ORDER_TABLE_LENGTH);

	/* Header size */
	offset += READ_U32(offset);

	/* Read pattern headers */
	for(uint16_t i = 0; i < out->num_patterns; ++i) {
		uint16_t num_rows = READ_U16(offset + 5);
		uint16_t packed_size = READ_U16(offset + 7);
		if(packed_size == 0 && num_rows != EMPTY_PATTERN_NUM_ROWS) {
			NOTICE("empty pattern %x has incorrect number of rows, "
			       "overriding (%u -> %u)",
			       i, num_rows, EMPTY_PATTERN_NUM_ROWS);
			num_rows = EMPTY_PATTERN_NUM_ROWS;
		}

		if(num_rows > MAX_ROWS_PER_PATTERN) {
			NOTICE("pattern %x has too many rows (%u > %u)",
			       i, num_rows, MAX_ROWS_PER_PATTERN);
			return false;
		}

		out->num_rows += num_rows;

		/* Pattern header length + packed pattern data size */
		offset += READ_U32(offset) + (uint32_t)READ_U16(offset + 7);
	}

	/* Maybe add space for an empty pattern */
	if(out->pot_length > PATTERN_ORDER_TABLE_LENGTH) {
		out->pot_length = PATTERN_ORDER_TABLE_LENGTH;
	}
	for(uint16_t i = 0; i < out->pot_length; ++i) {
		if(pot[i] >= out->num_patterns) {
			if(out->num_patterns >= MAX_PATTERNS) {
				NOTICE("no room left for blank pattern to replace an invalid pattern");
				return false;
			}

			NOTICE("replacing invalid pattern %d in pattern order table with empty pattern", pot[i]);
			out->num_rows += EMPTY_PATTERN_NUM_ROWS;
			out->num_patterns += 1;
			/* All invalid patterns will share the same empty
			   pattern. Loop can safely stop after finding at least
			   one. */
			break;
		}
	}

	/* Read instrument headers */
	for(uint16_t i = 0; i < out->num_instruments; ++i) {
		uint16_t num_samples = READ_U16(offset + 27);
		if(num_samples > MAX_SAMPLES_PER_INSTRUMENT) {
			NOTICE("instrument %u has too many samples (%u > %u)",
			       i + 1, num_samples, MAX_SAMPLES_PER_INSTRUMENT);
			return false;
		}
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

		/* Read sample headers */
		for(uint16_t j = 0; j < num_samples; ++j) {
			uint32_t sample_length = READ_U32(offset);
			uint32_t sample_bytes = sample_length;
			uint32_t loop_start = READ_U32(offset + 4);
			uint32_t loop_length = READ_U32(offset + 8);
			uint8_t flags = READ_U8(offset + 14);
			sample_length = TRIM_SAMPLE_LENGTH(sample_length,
			                                   loop_start,
			                                   loop_length,
			                                   flags);
			if(flags & SAMPLE_FLAG_16B) {
				/* 16-bit sample data */
				#if XM_DEFENSIVE
				if(sample_length % 2) {
					NOTICE("sample %d of instrument %d is 16-bit with an odd length!", j, i+1);
				}
				#endif
				sample_length /= 2;
			}
			uint32_t max = MAX_SAMPLE_LENGTH;
			if(flags & SAMPLE_FLAG_PING_PONG) max /= 2;
			if(sample_length > max) {
				NOTICE("sample %d of instrument %d is too big "
				       "(%u > %u)", j, i+1, sample_length, max);
				return false;
			}
			out->samples_data_length += sample_length;
			inst_samples_bytes += sample_bytes;
			offset += SAMPLE_HEADER_SIZE;
		}

		offset += inst_samples_bytes;
	}

	uint32_t sz = sizeof(xm_context_t);
	if(ckd_add(&sz, sz, sizeof(xm_pattern_t) * out->num_patterns)
	   || ckd_add(&sz, sz, sizeof(xm_pattern_slot_t)
	              * out->num_rows * out->num_channels)
	   || ckd_add(&sz, sz, sizeof(xm_instrument_t) * out->num_instruments)
	   || ckd_add(&sz, sz, sizeof(xm_sample_t) * out->num_samples)
	   || ckd_add(&sz, sz, sizeof(xm_sample_point_t)
	              * out->samples_data_length)
	   || ckd_add(&sz, sz, sizeof(xm_channel_context_t) * out->num_channels)
	   || ckd_add(&sz, sz, sizeof(uint8_t) * MAX_ROWS_PER_PATTERN
	              * out->pot_length)) {
		NOTICE("module too big for uint32");
		return false;
	}
	if(XM_DEFENSIVE && sz > 128 << 20) {
		NOTICE("module is suspiciously large (%u bytes), aborting load "
		       "as this is probably a corrupt/malicious file, "
		       "or a bug in libxm", sz);
		return false;
	}

	out->context_size = (uint32_t)sz;

	NOTICE("read %d patterns, %d channels, %d rows, %d instruments, "
	       "%d samples, %d sample frames, %d pot length",
	      out->num_patterns, out->num_channels, out->num_rows,
	      out->num_instruments, out->num_samples,
	      out->samples_data_length, out->pot_length);

	return true;
}

static uint32_t xm_load_module_header(xm_context_t* ctx,
                                      const char* moddata,
                                      uint32_t moddata_length) {
	uint32_t offset = 0;
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
	uint16_t restart_position = READ_U16(offset + 6);
	if(restart_position >= PATTERN_ORDER_TABLE_LENGTH) {
		NOTICE("zeroing invalid restart position (%u -> 0)",
		       restart_position);
		restart_position = 0;
	}
	static_assert(UINT8_MAX >= PATTERN_ORDER_TABLE_LENGTH - 1);
	mod->restart_position = (uint8_t)restart_position;
	/* Prescan already checked MAX_CHANNELS */
	static_assert(MAX_CHANNELS <= UINT8_MAX);
	mod->num_channels = READ_U8(offset + 8);
	mod->num_patterns = READ_U16(offset + 10);
	assert(mod->num_patterns <= MAX_PATTERNS);
	/* Prescan already checked MAX_INSTRUMENTS */
	static_assert(MAX_INSTRUMENTS <= UINT8_MAX);
	mod->num_instruments = READ_U8(offset + 12);

	if(mod->restart_position >= mod->length) {
		NOTICE("invalid restart_position, resetting to zero");
		mod->restart_position = 0;
	}

	if(mod->length > PATTERN_ORDER_TABLE_LENGTH) {
		NOTICE("clamping module pot length %d to %d\n",
		       mod->length, PATTERN_ORDER_TABLE_LENGTH);
		mod->length = PATTERN_ORDER_TABLE_LENGTH;
	}

	uint16_t flags = READ_U16(offset + 14);
	mod->frequency_type = (flags & 1) ?
		XM_LINEAR_FREQUENCIES : XM_AMIGA_FREQUENCIES;

	#if XM_DEFENSIVE
	if(flags & 0b11111110) {
		NOTICE("unknown flags set in module header (%d)", flags);
	}
	#endif

	uint16_t tempo = READ_U16(offset + 16);
	uint16_t bpm = READ_U16(offset + 18);
	if(tempo >= MIN_BPM) {
		NOTICE("clamping tempo (%u -> %u)", tempo, MIN_BPM-1);
		tempo = MIN_BPM-1;
	}
	if(bpm > MAX_BPM) {
		NOTICE("clamping bpm (%u -> %u)", bpm, MAX_BPM);
		bpm = MAX_BPM;
	}
	ctx->tempo = (uint8_t)tempo;
	ctx->bpm = (uint8_t)bpm;

	READ_MEMCPY(mod->pattern_table, offset + 20, PATTERN_ORDER_TABLE_LENGTH);

	return offset + header_size;
}

static uint32_t xm_load_pattern(xm_context_t* ctx,
                                xm_pattern_t* pat,
                                const char* moddata,
                                uint32_t moddata_length,
                                uint32_t offset) {
	uint16_t packed_patterndata_size = READ_U16(offset + 7);
	pat->num_rows = READ_U16(offset + 5);
	assert(pat->num_rows <= MAX_ROWS_PER_PATTERN);
	assert(ctx->module.num_rows <= UINT16_MAX);
	pat->rows_index = (uint16_t)ctx->module.num_rows;
	ctx->module.num_rows += pat->num_rows;
	xm_pattern_slot_t* slots = ctx->pattern_slots
		+ pat->rows_index * ctx->module.num_channels;

	#if XM_DEFENSIVE
	uint8_t packing_type = READ_U8(offset + 4);
	if(packing_type != 0) {
		NOTICE("unknown packing type %d in pattern", packing_type);
	}
	#endif

	/* Pattern header length */
	offset += READ_U32(offset);

	if(packed_patterndata_size == 0) {
		/* Assume empty pattern */
		ctx->module.num_rows -= pat->num_rows;
		pat->num_rows = EMPTY_PATTERN_NUM_ROWS;
		ctx->module.num_rows += pat->num_rows;
		return offset;
	}

	/* Reads beyond the pattern end should be zeroes, this can happen if a
	   pattern is truncated mid-slot. */
	moddata_length = offset + packed_patterndata_size;

	/* j counts bytes in the file, k counts pattern slots */
	uint16_t j, k;
	for(j = 0, k = 0; j < packed_patterndata_size; ++k) {
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

		if(slot->note > 97) {
			NOTICE("pattern %lu slot %lu: deleting invalid note %d",
			       pat - ctx->patterns, slot - slots, slot->note);
			slot->note = 0;
		} else if(slot->note == 97) {
			slot->note = KEY_OFF_NOTE;
		}

		if(slot->effect_type == 0x0E && slot->effect_param >> 4 == 8) {
			/* Convert E8x to 8xx */
			slot->effect_type = 8;
			slot->effect_param = (slot->effect_param & 0xF) * 0x11;
		}

		if(slot->effect_type == 0xE && slot->effect_param == 0xC0) {
			/* Convert EC0 to C00, this is exactly the same effect
			   and saves us a switch case in play.c */
			slot->effect_type = 0xC;
			slot->effect_param = 0;
		}

		if(slot->effect_type == 0xE && slot->effect_param == 0xD0) {
			/* Remove all ED0, these are completely useless and save
			   us a check in play.c */
			slot->effect_type = 0;
			slot->effect_param = 0;
		}

		if(slot->effect_type == 0x0F && slot->effect_param == 0) {
			/* Delete F00 (stops playback) */
			slot->effect_type = 0;
		}

		if(slot->effect_type == 20 && slot->effect_param == 0) {
			/* Convert K00 to key off note. This is vital, as Kxx
			   effect logic would otherwise be applied much later,
			   and this has all kinds of nasty side effects when K00
			   is used with either a note, or an instrument in the
			   same slot. */
			slot->effect_type = 0;
			slot->note = KEY_OFF_NOTE;
		}
	}

	#if XM_DEFENSIVE
	if(k != pat->num_rows * ctx->module.num_channels) {
		NOTICE("incomplete packed pattern data for pattern %ld, expected %u slots, got %u", pat - ctx->patterns, pat->num_rows * ctx->module.num_channels, k);
	}
	#endif
	return offset + packed_patterndata_size;
}

static uint32_t xm_load_instrument(xm_context_t* ctx,
                                   xm_instrument_t* instr,
                                   const char* moddata,
                                   uint32_t moddata_length,
                                   uint32_t offset) {
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
	uint32_t orig_moddata_length = moddata_length;
	moddata_length = offset + ins_header_size;

	#if XM_DEFENSIVE
	uint8_t type = READ_U8(offset + 26);
	if(type != 0) {
		NOTICE("ignoring non-zero type %d on instrument %ld",
		       type, (instr - ctx->instruments) + 1);
	}
	#endif

	/* Prescan already checked MAX_SAMPLES_PER_INSTRUMENT */
	static_assert(MAX_SAMPLES_PER_INSTRUMENT <= UINT8_MAX);
	instr->num_samples = READ_U8(offset + 27);
	if(instr->num_samples == 0) {
		return offset + ins_header_size;
	}

	/* Read extra header properties */
	READ_MEMCPY(instr->sample_of_notes, offset + 33, NUM_NOTES);

	xm_load_envelope_points(&instr->volume_envelope,
	                        moddata + offset + 129);
	xm_load_envelope_points(&instr->panning_envelope,
	                        moddata + offset + 177);

	instr->volume_envelope.num_points = READ_U8(offset + 225);
	instr->panning_envelope.num_points = READ_U8(offset + 226);
	instr->volume_envelope.sustain_point = READ_U8(offset + 227);
	instr->volume_envelope.loop_start_point = READ_U8(offset + 228);
	instr->volume_envelope.loop_end_point = READ_U8(offset + 229);
	instr->panning_envelope.sustain_point = READ_U8(offset + 230);
	instr->panning_envelope.loop_start_point = READ_U8(offset + 231);
	instr->panning_envelope.loop_end_point = READ_U8(offset + 232);

	uint8_t vol_env_flags = READ_U8(offset + 233);
	uint8_t pan_env_flags = READ_U8(offset + 234);

	xm_check_and_fix_envelope(&(instr->volume_envelope), vol_env_flags);
	xm_check_and_fix_envelope(&(instr->panning_envelope), pan_env_flags);

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
	for(uint16_t i = 0; i < instr->num_samples; ++i) {
		bool is_16bit;
		offset = xm_load_sample_header(ctx->samples + instr->samples_index + i, &is_16bit, moddata, moddata_length, offset);
		if(is_16bit) {
			/* Find some free bit in the struct to pack the
			   16bitness */
			static_assert(MAX_SAMPLE_LENGTH < (1u << 31));
			ctx->samples[instr->samples_index+i].length
				|= (1u << 31);
		}
	}

	/* Read sample data */
	for(uint16_t i = 0; i < instr->num_samples; ++i) {
		xm_sample_t* s = ctx->samples + instr->samples_index + i;
		/* As currently loaded, s->index is the real sample length in
		   the xm file, s->length is after trimming to loop_end (and the
		   actual sample length as stored in the context) */
		xm_sample_point_t* sample_data = ctx->samples_data
			+ ctx->module.samples_data_length;
		if(s->length & (1u << 31)) {
			s->length &= ~(1u << 31);
			if(_Generic((xm_sample_point_t){},
			            int8_t: true,
			            default: false)) {
				NOTICE("instrument %ld, sample %u will be dithered from 16 to 8 bits", instr - ctx->instruments + 1, i);
			}
			xm_load_16b_sample_data(s->length, sample_data, moddata,
			                        moddata_length, offset);
			offset += s->index * 2;
		} else {
			xm_load_8b_sample_data(s->length, sample_data, moddata,
			                       moddata_length, offset);
			offset += s->index;
		}
		s->index = ctx->module.samples_data_length;
		ctx->module.samples_data_length += s->length;
	}

	return offset;
}

static void xm_load_envelope_points(xm_envelope_t* env, const char* moddata) {
	uint32_t moddata_length = MAX_ENVELOPE_POINTS * 4;
	uint16_t env_val;
	for(uint8_t i = 0; i < MAX_ENVELOPE_POINTS; ++i) {
		env->points[i].frame = READ_U16(4u * i);
		env_val = READ_U16(4u * i + 2u);
		if(env_val > MAX_ENVELOPE_VALUE) {
			NOTICE("clamped invalid envelope pt value (%u -> %u)",
			       env_val, MAX_ENVELOPE_VALUE);
			env_val = MAX_ENVELOPE_VALUE;
		}
		env->points[i].value = (uint8_t)env_val;
	}
}

static void xm_check_and_fix_envelope(xm_envelope_t* env, uint8_t flags) {
	/* Check this even for disabled envelopes, because this can potentially
	   lead to out of bounds accesses in the future */
	if(env->num_points > MAX_ENVELOPE_POINTS) {
		NOTICE("clamped invalid envelope num_points (%u -> %u)",
		       env->num_points, MAX_ENVELOPE_POINTS);
		env->num_points = MAX_ENVELOPE_POINTS;
	}
	if(!(flags & ENVELOPE_FLAG_ENABLED)) {
		env->num_points += 128;
		return;
	}
	if(env->num_points < 2) {
		NOTICE("discarding invalid envelope data "
		       "(needs 2 point at least, got %u)",
		       env->num_points);
		env->num_points += 128;
		return;
	}
	for(uint8_t i = 1; i < env->num_points; ++i) {
		if(env->points[i-1].frame < env->points[i].frame) continue;
		NOTICE("discarding invalid envelope data "
		       "(point %u frame %X -> point %u frame %X)",
		       i-1, env->points[i-1].frame,
		       i, env->points[i].frame);
		env->num_points += 128;
		return;
	}
	if(env->loop_start_point >= env->num_points) {
		NOTICE("clamped invalid envelope loop start point (%u -> %u)",
		       env->loop_start_point, env->num_points - 1);
		env->loop_start_point = env->num_points - 1;
	}
	if(env->loop_end_point >= env->num_points) {
		NOTICE("clamped invalid envelope loop end point (%u -> %u)",
		       env->loop_end_point, env->num_points - 1);
		env->loop_end_point = env->num_points - 1;
	}
	if(!(flags & ENVELOPE_FLAG_LOOP)) {
		env->loop_start_point += 128;
	}
	if(env->sustain_point >= env->num_points) {
		NOTICE("clamped invalid envelope sustain point (%u -> %u)",
		       env->sustain_point, env->num_points - 1);
		env->sustain_point = env->num_points - 1;
	}
	if(!(flags & ENVELOPE_FLAG_SUSTAIN)) {
		env->sustain_point += 128;
	}
}

static uint32_t xm_load_sample_header(xm_sample_t* sample, bool* is_16bit,
                                      const char* moddata,
                                      uint32_t moddata_length,
                                      uint32_t offset) {
	sample->length = READ_U32(offset);
	sample->index = sample->length; /* Keep original length around, replace
	                                   it later after sample data is
	                                   loaded */
	uint32_t loop_start = READ_U32(offset + 4);
	sample->loop_length = READ_U32(offset + 8);
	uint8_t flags = READ_U8(offset + 14);
	if(loop_start > sample->length) {
		NOTICE("fixing invalid sample loop start");
		loop_start = sample->length;
	}
	if(loop_start + sample->loop_length > sample->length) {
		NOTICE("fixing invalid sample loop length");
		sample->loop_length = 0;
	}
	/* Trim end of sample beyond the loop end */
	sample->length = TRIM_SAMPLE_LENGTH(sample->length, loop_start,
	                                    sample->loop_length, flags);

	uint8_t volume = READ_U8(offset + 12);
	if(volume > MAX_VOLUME) {
		NOTICE("clamping invalid sample volume (%u > %u)",
		       volume, MAX_VOLUME);
		volume = MAX_VOLUME;
	}
	/* Assigning to bitfields is ugly with -Wconversion, it is how it is */
	static_assert(MAX_VOLUME <= 0x7F);
	sample->volume = (unsigned)volume & 0x7F;

	/* Finetune is stored as a signed int8, but always rounds down instead
	   of the usual truncation */
	sample->finetune = (int8_t)READ_U8(offset + 13);
	sample->finetune = (int8_t)((sample->finetune - INT8_MIN) / 8 - 16);

	/* The XM spec doesn't quite say what to do when bits 0 and 1
	   are set, but FT2 loads it as ping-pong, so it seems bit 1 has
	   precedence. */
	sample->ping_pong = flags & SAMPLE_FLAG_PING_PONG;
	if(!(flags & (SAMPLE_FLAG_FORWARD | SAMPLE_FLAG_PING_PONG))) {
		/* Not a looping sample */
		sample->loop_length = 0;
	}

	#if XM_DEFENSIVE
	if(flags & 0b11101100) {
		NOTICE("ignoring unknown flags (%d) in sample", flags);
	}
	#endif

	sample->panning = READ_U8(offset + 15);
	sample->relative_note = (int8_t)READ_U8(offset + 16);

	#if XM_STRINGS
	READ_MEMCPY(sample->name, offset + 18, SAMPLE_NAME_LENGTH);
	sample->name[SAMPLE_NAME_LENGTH] = 0;
	#endif

	*is_16bit = flags & SAMPLE_FLAG_16B;
	if(*is_16bit) {
		sample->loop_length >>= 1;
		sample->length >>= 1;
		sample->index >>= 1;
	}

	return offset + SAMPLE_HEADER_SIZE;
}

static void xm_load_8b_sample_data(uint32_t length, xm_sample_point_t* out,
                                   const char* moddata,
                                   uint32_t moddata_length, uint32_t offset) {
	int8_t v = 0;
	uint8_t s;
	for(uint32_t k = 0; k < length; ++k) {
		s = READ_U8(offset + k);
		v += (int8_t)s;
		out[k] = _Generic((xm_sample_point_t){},
		                  int8_t: v,
		                  int16_t: (v * 256),
		                  float: (float)v / (float)INT8_MAX);
	}
}

static void xm_load_16b_sample_data(uint32_t length, xm_sample_point_t* out,
                                    const char* moddata,
                                    uint32_t moddata_length, uint32_t offset) {
	int16_t v = 0;
	for(uint32_t k = 0; k < length; ++k) {
		v += (int16_t)READ_U16(offset + (k << 1));
		out[k] = _Generic((xm_sample_point_t){},
		                  int8_t: xm_dither_16b_8b(v),
		                  int16_t: v,
		                  float: (float)v / (float)INT16_MAX);
	}
}

static int8_t xm_dither_16b_8b(int16_t x) {
	static uint32_t next = 1;
	next = next * 214013 + 2531011;
	/* Not that this is perf critical, but this should compile to a cmovl
	   (branchless) */
	return (x >= 32512) ? 127 :
		(int8_t)((x + (int16_t)((next >> 16) % 256)) / 256);
}



uint32_t xm_size_for_context(const xm_prescan_data_t* p) {
	return p->context_size;
}

uint32_t xm_context_size(const xm_context_t* ctx) {
	/* Prescan already checked that ctx size < UINT32_MAX */
	/* This prevents duplicating the logic from prescan, but assumes
	   row_loop_count comes *last* in the allocated memory */
	return (uint32_t)((char*)ctx->row_loop_count - (char*)ctx)
		+ (uint32_t)(sizeof(uint8_t) * MAX_ROWS_PER_PATTERN
		             * ctx->module.length);
}

xm_context_t* xm_create_context(char* mempool, const xm_prescan_data_t* p,
                                const char* moddata, uint32_t moddata_length,
                                uint16_t rate) {
	/* Make sure we are not misaligning data by accident */
	ASSERT_ALIGNED(mempool, xm_context_t);
	uint32_t ctx_size = xm_size_for_context(p);
	__builtin_memset(mempool, 0, ctx_size);
	xm_context_t* ctx = (xm_context_t*)mempool;
	mempool += sizeof(xm_context_t);

	ASSERT_ALIGNED(mempool, xm_channel_context_t);
	ctx->channels = (xm_channel_context_t*)mempool;
	mempool += sizeof(xm_channel_context_t) * p->num_channels;

	ASSERT_ALIGNED(mempool, xm_instrument_t);
	ctx->instruments = (xm_instrument_t*)mempool;
	mempool += sizeof(xm_instrument_t) * p->num_instruments;

	ASSERT_ALIGNED(mempool, xm_sample_t);
	ctx->samples = (xm_sample_t*)mempool;
	mempool += sizeof(xm_sample_t) * p->num_samples;

	ASSERT_ALIGNED(mempool, xm_pattern_t);
	ctx->patterns = (xm_pattern_t*)mempool;
	mempool += sizeof(xm_pattern_t) * p->num_patterns;

	ASSERT_ALIGNED(mempool, xm_sample_point_t);
	ctx->samples_data = (xm_sample_point_t*)mempool;
	mempool += sizeof(xm_sample_point_t) * p->samples_data_length;

	ASSERT_ALIGNED(mempool, xm_pattern_slot_t);
	ctx->pattern_slots = (xm_pattern_slot_t*)mempool;
	mempool += sizeof(xm_pattern_slot_t) * p->num_rows * p->num_channels;

	ASSERT_ALIGNED(mempool, uint8_t);
	ctx->row_loop_count = (uint8_t*)mempool;
	mempool += sizeof(uint8_t) * MAX_ROWS_PER_PATTERN * p->pot_length;

	assert(mempool - (char*)ctx == ctx_size);

	ctx->rate = rate;
	ctx->global_volume = MAX_VOLUME;

	/* Read module header */
	uint32_t offset = xm_load_module_header(ctx, moddata, moddata_length);
	assert(ctx->module.num_channels == p->num_channels);
	assert(ctx->module.length == p->pot_length);

	/* Read pattern headers + slots */
	for(uint16_t i = 0; i < ctx->module.num_patterns; ++i) {
		offset = xm_load_pattern(ctx, ctx->patterns + i,
		                         moddata, moddata_length, offset);
	}

	/* Scan for invalid patterns and replace by empty pattern */
	bool has_invalid_patterns = false;
	for(uint16_t i = 0; i < ctx->module.length; ++i) {
		if(ctx->module.pattern_table[i] >= ctx->module.num_patterns) {
			has_invalid_patterns = true;
			break;
		}
	}
	if(has_invalid_patterns) {
		assert(ctx->module.num_patterns <= UINT8_MAX);
		for(uint16_t i = 0; i < ctx->module.length; ++i) {
			if(ctx->module.pattern_table[i]
			   < ctx->module.num_patterns) {
				continue;
			}
			ctx->module.pattern_table[i] =
				(uint8_t)ctx->module.num_patterns;
		}
		ctx->patterns[ctx->module.num_patterns].num_rows
			= EMPTY_PATTERN_NUM_ROWS;
		assert(ctx->module.num_rows < UINT16_MAX);
		ctx->patterns[ctx->module.num_patterns].rows_index
			= (uint16_t)ctx->module.num_rows;
		ctx->module.num_patterns += 1;
		ctx->module.num_rows += EMPTY_PATTERN_NUM_ROWS;
	}
	assert(ctx->module.num_patterns == p->num_patterns);
	assert(ctx->module.num_rows == p->num_rows);

	/* Read instruments, samples and sample data */
	for(uint16_t i = 0; i < p->num_instruments; ++i) {
		offset = xm_load_instrument(ctx, ctx->instruments + i, moddata, moddata_length, offset);
	}
	assert(ctx->module.num_instruments == p->num_instruments);
	assert(ctx->module.num_samples == p->num_samples);
	assert(ctx->module.samples_data_length == p->samples_data_length);

	assert(xm_context_size(ctx) == ctx_size);
	return ctx;
}

#define CALC_OFFSET(dest, orig) do { \
		(dest) = (void*)((intptr_t)(dest) - (intptr_t)(orig)); \
	} while(0)

#define APPLY_OFFSET(dest, orig) do { \
		(dest) = (void*)((intptr_t)(dest) + (intptr_t)(orig)); \
	} while(0)

static uint64_t xm_fnv1a(const unsigned char* data, uint32_t length) {
	uint64_t h = 14695981039346656037UL;
	for(uint32_t i = 0; i < length; ++i) {
		h ^= data[i];
		h *= 1099511628211UL;
	}
	return h;
}

void xm_context_to_libxm(xm_context_t* ctx, char* out) {
	/* Reset internal pointers and playback position to 0 (normally not
	   needed with correct usage of this function) */
	for(uint16_t i = 0; i < ctx->module.num_channels; ++i) {
		xm_channel_context_t* ch = ctx->channels + i;
		ch->instrument = 0;
		ch->sample = 0;
		ch->current = 0;
	}
	/* Force next generated samples to call xm_row() and refill
	  ch->current */
	ctx->current_tick = 0;

	/* (*) Everything done after this should be deterministically
	   reversible */
	uint32_t ctx_size = xm_context_size(ctx);
	[[maybe_unused]] uint64_t old_hash = xm_fnv1a((void*)ctx, ctx_size);

	uint16_t old_rate = ctx->rate;
	ctx->rate = 0;

	#if XM_LIBXM_DELTA_SAMPLES
	for(uint32_t i = ctx->module.samples_data_length - 1; i > 0; --i) {
		ctx->samples_data[i] -= ctx->samples_data[i-1];
	}
	#endif

	CALC_OFFSET(ctx->patterns, ctx);
	CALC_OFFSET(ctx->pattern_slots, ctx);
	CALC_OFFSET(ctx->instruments, ctx);
	CALC_OFFSET(ctx->samples, ctx);
	CALC_OFFSET(ctx->samples_data, ctx);
	CALC_OFFSET(ctx->channels, ctx);
	CALC_OFFSET(ctx->row_loop_count, ctx);

	__builtin_memcpy(out, ctx, ctx_size);

	/* Restore the context back to the state marked (*) */
	ctx = xm_create_context_from_libxm((void*)ctx, old_rate);

	assert(xm_fnv1a((void*)ctx, ctx_size) == old_hash);
}

xm_context_t* xm_create_context_from_libxm(char* data, uint16_t rate) {
	ASSERT_ALIGNED(data, xm_context_t);
	xm_context_t* ctx = (void*)data;
	ctx->rate = rate;

	/* Reverse steps of xm_context_to_libxm() */
	APPLY_OFFSET(ctx->patterns, ctx);
	APPLY_OFFSET(ctx->pattern_slots, ctx);
	APPLY_OFFSET(ctx->instruments, ctx);
	APPLY_OFFSET(ctx->samples, ctx);
	APPLY_OFFSET(ctx->samples_data, ctx);
	APPLY_OFFSET(ctx->channels, ctx);
	APPLY_OFFSET(ctx->row_loop_count, ctx);

	#if XM_LIBXM_DELTA_SAMPLES
	for(uint32_t i = 1; i < ctx->module.samples_data_length; ++i) {
		ctx->samples_data[i] += ctx->samples_data[i-1];
	}
	#endif

	return ctx;
}
