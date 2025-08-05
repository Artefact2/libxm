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

/* Bounded reader macros.
 * If we attempt to read the buffer out-of-bounds, pretend that the buffer is
 * infinitely padded with zeroes.
 */
#define READ_U8_BOUND(offset, bound) \
	((uint8_t)(((uint32_t)(offset) < (uint32_t)(bound)) ? \
	           (((uint8_t*)moddata)[offset]) : 0))

#define READ_U16_BOUND(offset, bound) \
	((uint16_t)((uint16_t)READ_U8_BOUND(offset, bound) \
	            | ((uint16_t)READ_U8_BOUND((offset) + 1, bound) << 8)))
#define READ_U16BE_BOUND(offset, bound) \
	((uint16_t)(((uint16_t)READ_U8_BOUND(offset, bound) << 8) \
	            | (uint16_t)READ_U8_BOUND((offset) + 1, bound)))

#define READ_U32_BOUND(offset, bound) \
	((uint32_t)((uint32_t)READ_U16_BOUND(offset, bound) \
	            | ((uint32_t)READ_U16_BOUND((offset) + 2, bound) << 16)))
#define READ_U32BE_BOUND(offset, bound) \
	((uint32_t)(((uint32_t)READ_U16BE_BOUND(offset, bound) << 16) \
	            | (uint32_t)READ_U16BE_BOUND((offset) + 2, bound)))

#define READ_MEMCPY_BOUND(dest, offset, length, bound) \
	__builtin_memcpy(dest, (uint8_t*)(moddata) + (offset), \
	                 ((offset) + (length) <= (bound)) ? \
	                 (length) : ((offset) >= (bound) ? \
	                             0 : (bound) - (offset)))

#define READ_U8(offset) READ_U8_BOUND(offset, moddata_length)
#define READ_U16(offset) READ_U16_BOUND(offset, moddata_length)
#define READ_U16BE(offset) READ_U16BE_BOUND(offset, moddata_length)
#define READ_U32(offset) READ_U32_BOUND(offset, moddata_length)
#define READ_U32BE(offset) READ_U32BE_BOUND(offset, moddata_length)
#define READ_MEMCPY(dest, offset, length) \
	READ_MEMCPY_BOUND(dest, offset, length, moddata_length)

#define TRIM_SAMPLE_LENGTH(length, loop_start, loop_length, flags) \
	((flags) & (SAMPLE_FLAG_PING_PONG | SAMPLE_FLAG_FORWARD) ? \
	 ((loop_start > length ? length :				\
	  (loop_start + (loop_start + loop_length > length ? 0 : loop_length)) \
	   )) : length)

#define SAMPLE_POINT_FROM_S8(v) \
	_Generic((xm_sample_point_t){}, int8_t: (v), int16_t: ((v) * 256), \
	         float: (float)(v) / (float)INT8_MAX)

#define SAMPLE_POINT_FROM_S16(v) \
	_Generic((xm_sample_point_t){}, int8_t: xm_dither_16b_8b(v), \
		int16_t: (v), float: (float)(v) / (float)INT16_MAX)

struct xm_prescan_data_s {
	uint32_t context_size;
	static_assert(MAX_PATTERNS * MAX_ROWS_PER_PATTERN <= 0xFFFFFF);
	enum:uint8_t {
		XM_FORMAT_XM0104,
		XM_FORMAT_MOD,
		XM_FORMAT_MOD_FLT8, /* FLT8 requires special logic for its
		                       pattern data */
		XM_FORMAT_S3M,
	} format;
	uint32_t num_rows:24;
	uint32_t samples_data_length;
	uint16_t num_patterns;
	uint16_t num_samples;
	uint16_t pot_length;
	uint8_t num_channels;
	uint8_t num_instruments;
};
const uint8_t XM_PRESCAN_DATA_SIZE = sizeof(xm_prescan_data_t);

/* ----- Static functions ----- */

static int8_t xm_dither_16b_8b(int16_t);
static uint64_t xm_fnv1a(const unsigned char*, uint32_t) __attribute__((const));
static void xm_fixup_common(xm_context_t*);

static bool xm_prescan_xm0104(const char*, uint32_t, xm_prescan_data_t*);
static void xm_load_xm0104(xm_context_t*, const char*, uint32_t);
static uint32_t xm_load_xm0104_module_header(xm_context_t*, uint8_t*, const char*, uint32_t);
static uint32_t xm_load_xm0104_pattern(xm_context_t*, xm_pattern_t*, const char*, uint32_t, uint32_t);
static uint32_t xm_load_xm0104_instrument(xm_context_t*, xm_instrument_t*, const char*, uint32_t, uint32_t);
[[maybe_unused]] static void xm_load_xm0104_envelope_points(xm_envelope_t*, const char*);
[[maybe_unused]] static void xm_check_and_fix_envelope(xm_envelope_t*, uint8_t);
static uint32_t xm_load_xm0104_sample_header(xm_sample_t*, bool*, const char*, uint32_t, uint32_t);
static void xm_load_xm0104_8b_sample_data(uint32_t, xm_sample_point_t*, const char*, uint32_t, uint32_t);
static void xm_load_xm0104_16b_sample_data(uint32_t, xm_sample_point_t*, const char*, uint32_t, uint32_t);
static void xm_fixup_xm0104(xm_context_t*);

static bool xm_prescan_mod(const char*, uint32_t, xm_prescan_data_t*);
static void xm_load_mod(xm_context_t*, const char*, uint32_t, const xm_prescan_data_t*);
static void xm_fixup_mod_flt8(xm_context_t*);

static bool xm_prescan_s3m(const char*, uint32_t, xm_prescan_data_t*);
static void xm_load_s3m(xm_context_t*, const char*, uint32_t, const xm_prescan_data_t*);
static void xm_load_s3m_instrument(xm_context_t*, uint8_t, bool, const char*, uint32_t, uint32_t);
static void xm_load_s3m_pattern(xm_context_t*, uint8_t, const uint8_t*, const uint8_t*, const uint8_t*, bool, const char*, uint32_t, uint32_t);

/* ----- Function definitions ----- */

bool xm_prescan_module(const char* restrict moddata, uint32_t moddata_length,
                       xm_prescan_data_t* restrict out) {
	if(moddata_length >= 60
	   && memcmp("Extended Module: ", moddata, 17) == 0
	   && moddata[37] == 0x1A
	   && moddata[59] == 0x01
	   && moddata[58] == 0x04) {
		out->format = XM_FORMAT_XM0104;
		if(xm_prescan_xm0104(moddata, moddata_length, out)) {
			goto end;
		} else {
			return false;
		}
	}

	if(moddata_length >= 96) {
		if(memcmp("\x10\0\0", moddata + 29, 3) == 0
		   && memcmp("SCRM", moddata + 44, 4) == 0) {
			out->format = XM_FORMAT_S3M;
			if(xm_prescan_s3m(moddata, moddata_length, out)) {
				goto end;
			} else {
				return false;
			}
		}
	}

	/* XXX: detect 15 sample MODs? */
	if(moddata_length >= 154+31*30) {
		out->num_instruments = 31;
		out->format = XM_FORMAT_MOD;
		bool load = true;

		const char chn = moddata[150+31*30];
		const char chn2 = moddata[151+31*30];
		const char chn3 = moddata[153+31*30];

		if(memcmp("M.K.", moddata + 150+31*30, 4) == 0
		   || memcmp("M!K!", moddata + 150+31*30, 4) == 0
		   || memcmp("FLT4", moddata + 150+31*30, 4) == 0) {
			out->num_channels = 4;
		} else if(memcmp("CD81", moddata + 150+31*30, 4) == 0
		          || memcmp("OCTA", moddata + 150+31*30, 4) == 0
		          || memcmp("OKTA", moddata + 150+31*30, 4) == 0) {
			out->num_channels = 8;
		} else if(memcmp("FLT8", moddata + 150+31*30, 4) == 0) {
			/* Load FLT8 patterns as 8 channels, 32 rows. Merge them
			   later in xm_fixup_mod_flt8(). */
			out->num_channels = 8;
			out->format = XM_FORMAT_MOD_FLT8;
		} else if(chn >= '1' && chn <= '9'
		   && memcmp("CHN", moddata + 151+31*30, 3) == 0) {
			out->num_channels = (uint8_t)(chn - '0');
		} else if(chn >= '1' && chn <= '9' && chn2 >= '0' && chn2 <= '9'
		   && (memcmp("CH", moddata + 152+31*30, 2) == 0
		       || memcmp("CN", moddata + 152+31*30, 2) == 0)) {
			out->num_channels = (uint8_t)
				(10 * (chn - '0') + chn2 - '0');
		} else if(chn3 >= '1' && chn3 <= '9'
		   && memcmp("TDZ", moddata + 150+31*30, 3) == 0) {
			out->num_channels = (uint8_t)(chn3 - '0');
		} else {
			load = false;
		}

		if(load) {
			if(xm_prescan_mod(moddata, moddata_length, out)) {
				goto end;
			} else {
				return false;
			}
		}
	}

	NOTICE("input data does not look like a supported module");
	return false;

 end:
	uint32_t sz = sizeof(xm_context_t);
	if(ckd_add(&sz, sz, sizeof(xm_pattern_t) * out->num_patterns)
	   || ckd_add(&sz, sz, sizeof(xm_pattern_slot_t)
	              * out->num_rows * out->num_channels)
	   #if HAS_INSTRUMENTS
	   || ckd_add(&sz, sz, sizeof(xm_instrument_t) * out->num_instruments)
	   #endif
	   || ckd_add(&sz, sz, sizeof(xm_sample_t) * out->num_samples)
	   || ckd_add(&sz, sz, sizeof(xm_sample_point_t)
	              * out->samples_data_length)
	   || ckd_add(&sz, sz, sizeof(xm_channel_context_t) * out->num_channels)
	   #if XM_LOOPING_TYPE == 2
	   || ckd_add(&sz, sz, sizeof(uint8_t) * MAX_ROWS_PER_PATTERN
	                                       * out->pot_length)
	   #endif
	   ) {
		NOTICE("module too big for uint32");
		return false;
	}
	if(sz > 128 << 20) {
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

xm_context_t* xm_create_context(char* restrict mempool,
                                const xm_prescan_data_t* restrict p,
                                const char* restrict moddata,
                                uint32_t moddata_length) {
	/* Make sure we are not misaligning data by accident */
	ASSERT_ALIGNED(mempool, xm_context_t);
	uint32_t ctx_size = xm_size_for_context(p);
	__builtin_memset(mempool, 0, ctx_size);
	xm_context_t* ctx = (xm_context_t*)mempool;
	mempool += sizeof(xm_context_t);

	ASSERT_ALIGNED(mempool, xm_channel_context_t);
	ctx->channels = (xm_channel_context_t*)mempool;
	mempool += sizeof(xm_channel_context_t) * p->num_channels;

	#if HAS_INSTRUMENTS
	ASSERT_ALIGNED(mempool, xm_instrument_t);
	ctx->instruments = (xm_instrument_t*)mempool;
	mempool += sizeof(xm_instrument_t) * p->num_instruments;
	#endif

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

	#if XM_LOOPING_TYPE == 2
	ASSERT_ALIGNED(mempool, uint8_t);
	ctx->row_loop_count = (uint8_t*)mempool;
	mempool += sizeof(uint8_t) * MAX_ROWS_PER_PATTERN * p->pot_length;
	#endif

	assert(mempool - (char*)ctx == ctx_size);

	switch(p->format) {
	case XM_FORMAT_XM0104:
		xm_load_xm0104(ctx, moddata, moddata_length);
		xm_fixup_xm0104(ctx);
		break;

	case XM_FORMAT_MOD:
		xm_load_mod(ctx, moddata, moddata_length, p);
		xm_fixup_xm0104(ctx);
		break;

	case XM_FORMAT_MOD_FLT8:
		xm_load_mod(ctx, moddata, moddata_length, p);
		xm_fixup_mod_flt8(ctx);
		xm_fixup_xm0104(ctx);
		break;

	case XM_FORMAT_S3M:
		xm_load_s3m(ctx, moddata, moddata_length, p);
		break;

	default:
		assert(0);
	}

	assert(ctx->module.num_channels == p->num_channels);
	assert(ctx->module.length == p->pot_length);
	assert(ctx->module.num_patterns == p->num_patterns);
	assert(ctx->module.num_rows == p->num_rows);
	assert(NUM_INSTRUMENTS(&ctx->module) == p->num_instruments);
	assert(ctx->module.num_samples == p->num_samples);
	assert(ctx->module.samples_data_length == p->samples_data_length);
	assert(xm_context_size(ctx) == ctx_size);

	xm_fixup_common(ctx);
	xm_reset_context(ctx);
	return ctx;
}

static void xm_fixup_common(xm_context_t* ctx) {
	xm_pattern_slot_t* slot = ctx->pattern_slots;
	for(uint32_t i = ctx->module.num_rows * ctx->module.num_channels;
	    i; --i, ++slot) {
		if(slot->effect_type == EFFECT_JUMP_TO_ORDER
		   && slot->effect_param >= ctx->module.length) {
			/* Convert invalid Bxx to B00 */
			slot->effect_param = 0;
		}

		if((slot->effect_type == EFFECT_SET_VOLUME
		    || slot->effect_type == EFFECT_SET_GLOBAL_VOLUME)
		   && slot->effect_param > MAX_VOLUME) {
			/* Clamp Cxx and Gxx */
			slot->effect_param = MAX_VOLUME;
		}

		if(slot->effect_type == EFFECT_PATTERN_BREAK) {
			/* Convert Dxx to base 16, saves doing the math in
			   play.c */
			slot->effect_param = (uint8_t)
				(slot->effect_param
				 - 6 * (slot->effect_param >> 4));
		}

		if(slot->effect_type == EFFECT_SET_VIBRATO_CONTROL
		   || slot->effect_type == EFFECT_SET_TREMOLO_CONTROL) {
			/* Convert random waveform to square waveform (FT2
			   behaviour, lets us reuse waveform 3 for
			   autovibrato ramp) */
			/* Also clear useless bit 3 */
			slot->effect_param &=
				((slot->effect_param & 0b11) == 0b11) ?
				0b11110110 : 0b11110111;
		}

		if(slot->effect_type == EFFECT_CUT_NOTE
		   && slot->effect_param == 0) {
			/* Convert EC0 to C00, this is exactly the same effect
			   and saves us a switch case in play.c */
			slot->effect_type = EFFECT_SET_VOLUME;
		}

		if(slot->effect_type == EFFECT_DELAY_NOTE
		   && slot->effect_param == 0) {
			/* Remove all ED0, these are completely useless and save
			   us a check in play.c */
			slot->effect_type = 0;
		}

		if(slot->effect_type == EFFECT_RETRIGGER_NOTE
		   && slot->effect_param == 0) {
			if(slot->note) {
				/* E90 with a note is completely redundant */
			} else {
				/* Convert E90 without a note to a special
				   retrigger note */
				slot->note = NOTE_RETRIGGER;
			}
			slot->effect_type = 0;
		}

		if((slot->effect_type == EFFECT_SET_TEMPO
		    || slot->effect_type == EFFECT_SET_BPM)
		   && slot->effect_param == 0) {
			#if XM_LOOPING_TYPE != 1
			/* F00 is not great for a player, as it stops playback.
			   Some modules use this to indicate the end of the
			   song. Just loop back to the start of the module and
			   let the user decide if they want to continue, based
			   on max_loop_count. */
			slot->effect_type = EFFECT_JUMP_TO_ORDER;
			slot->effect_param = ctx->module.restart_position;
			#endif
		}

		#if HAS_VOLUME_COLUMN
		if((slot->volume_column >> 8) == VOLUME_EFFECT_VIBRATO_SPEED
		   && (slot->volume_column & 0xF) == 0) {
			/* Delete S0, it does nothing and saves a check in
			   play.c. */
			slot->volume_column = 0;
		}
		#endif
	}
}

uint32_t xm_size_for_context(const xm_prescan_data_t* p) {
	return p->context_size;
}

uint32_t xm_context_size(const xm_context_t* ctx) {
	return (uint32_t)
		(sizeof(xm_context_t)
		 + sizeof(xm_channel_context_t) * ctx->module.num_channels
		 #if HAS_INSTRUMENTS
		 + sizeof(xm_instrument_t) * ctx->module.num_instruments
		 #endif
		 + sizeof(xm_sample_t) * ctx->module.num_samples
		 + sizeof(xm_pattern_t) * ctx->module.num_patterns
		 + sizeof(xm_sample_point_t) * ctx->module.samples_data_length
		 + sizeof(xm_pattern_slot_t) * ctx->module.num_rows
		                             * ctx->module.num_channels
		 #if XM_LOOPING_TYPE == 2
		 + sizeof(uint8_t) * ctx->module.length * MAX_ROWS_PER_PATTERN
		 #endif
		 );
}


static int8_t xm_dither_16b_8b(int16_t x) {
	static uint32_t next = 1;
	next = next * 214013 + 2531011;
	/* Not that this is perf critical, but this should compile to a cmovl
	   (branchless) */
	return (x >= 32512) ? 127 :
		(int8_t)((x + (int16_t)((next >> 16) % 256)) / 256);
}

static uint64_t xm_fnv1a(const unsigned char* data, uint32_t length) {
	uint64_t h = 14695981039346656037UL;
	for(uint32_t i = 0; i < length; ++i) {
		h ^= data[i];
		h *= 1099511628211UL;
	}
	return h;
}

#define CALC_OFFSET(dest, orig) do { \
		(dest) = (void*)((intptr_t)(dest) - (intptr_t)(orig)); \
	} while(0)

#define APPLY_OFFSET(dest, orig) do { \
		(dest) = (void*)((intptr_t)(dest) + (intptr_t)(orig)); \
	} while(0)

void xm_context_to_libxm(xm_context_t* restrict ctx, char* restrict out) {
	/* Reset internal pointers and playback position to 0 (normally not
	   needed with correct usage of this function) */
	for(uint16_t i = 0; i < ctx->module.num_channels; ++i) {
		xm_channel_context_t* ch = ctx->channels + i;
		#if HAS_INSTRUMENTS
		ch->instrument = 0;
		#endif
		ch->next_instrument = 0;
		ch->sample = 0;
		ch->current = 0;
	}
	/* Force next generated samples to call xm_row() and refill
	  ch->current */
	ctx->current_tick = 0;
	ctx->remaining_samples_in_tick = 0;

	/* (*) Everything done after this should be deterministically
	   reversible */
	uint32_t ctx_size = xm_context_size(ctx);
	[[maybe_unused]] uint64_t old_hash = xm_fnv1a((void*)ctx, ctx_size);

	#if XM_LIBXM_DELTA_SAMPLES
	for(uint32_t i = ctx->module.samples_data_length - 1; i > 0; --i) {
		ctx->samples_data[i] -= ctx->samples_data[i-1];
	}
	#endif

	CALC_OFFSET(ctx->patterns, ctx);
	CALC_OFFSET(ctx->pattern_slots, ctx);

	#if HAS_INSTRUMENTS
	CALC_OFFSET(ctx->instruments, ctx);
	#endif

	CALC_OFFSET(ctx->samples, ctx);
	CALC_OFFSET(ctx->samples_data, ctx);
	CALC_OFFSET(ctx->channels, ctx);

	#if XM_LOOPING_TYPE == 2
	CALC_OFFSET(ctx->row_loop_count, ctx);
	#endif

	__builtin_memcpy(out, ctx, ctx_size);

	/* Restore the context back to the state marked (*) */
	ctx = xm_create_context_from_libxm((void*)ctx);

	assert(xm_fnv1a((void*)ctx, ctx_size) == old_hash);
}

xm_context_t* xm_create_context_from_libxm(char* data) {
	ASSERT_ALIGNED(data, xm_context_t);
	xm_context_t* ctx = (void*)data;

	/* Reverse steps of xm_context_to_libxm() */
	APPLY_OFFSET(ctx->patterns, ctx);
	APPLY_OFFSET(ctx->pattern_slots, ctx);

	#if HAS_INSTRUMENTS
	APPLY_OFFSET(ctx->instruments, ctx);
	#endif

	APPLY_OFFSET(ctx->samples, ctx);
	APPLY_OFFSET(ctx->samples_data, ctx);
	APPLY_OFFSET(ctx->channels, ctx);

	#if XM_LOOPING_TYPE == 2
	APPLY_OFFSET(ctx->row_loop_count, ctx);
	#endif

	#if XM_LIBXM_DELTA_SAMPLES
	for(uint32_t i = 1; i < ctx->module.samples_data_length; ++i) {
		ctx->samples_data[i] += ctx->samples_data[i-1];
	}
	#endif

	return ctx;
}

/* ----- Fasttracker II .XM (XM 0104): little endian ----- */

static bool xm_prescan_xm0104(const char* restrict moddata,
                              uint32_t moddata_length,
                              xm_prescan_data_t* restrict out) {
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
		out->num_samples += HAS_FEATURE(FEATURE_MULTISAMPLE_INSTRUMENTS)
			? num_samples : 1;

		/* Notice that, even if there's a "sample header size" in the
		   instrument header, that value seems ignored, and might even
		   be wrong in some corrupted modules. */
		if(num_samples > 0) {
			uint32_t sample_header_size = READ_U32(offset + 29);
			if(sample_header_size != SAMPLE_HEADER_SIZE) {
				NOTICE("ignoring dodgy sample header size (%d) for instrument %d", sample_header_size, i+1);
			}
		}

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
				if(sample_length % 2) {
					NOTICE("sample %d of instrument %d is 16-bit with an odd length!", j, i+1);
				}
				sample_length /= 2;
			}
			uint32_t max = MAX_SAMPLE_LENGTH;
			if(flags & SAMPLE_FLAG_PING_PONG) max /= 2;
			if(sample_length > max) {
				NOTICE("sample %d of instrument %d is too big "
				       "(%u > %u)", j, i+1, sample_length, max);
				return false;
			}

			if(HAS_FEATURE(FEATURE_MULTISAMPLE_INSTRUMENTS)
			   || j == 0) {
				out->samples_data_length += sample_length;
			}

			inst_samples_bytes += sample_bytes;
			offset += SAMPLE_HEADER_SIZE;
		}

		offset += inst_samples_bytes;
	}

	return true;
}

static uint32_t xm_load_xm0104_module_header(xm_context_t* ctx,
                                             uint8_t* out_num_instruments,
                                             const char* moddata,
                                             uint32_t moddata_length) {
	uint32_t offset = 0;
	xm_module_t* mod = &(ctx->module);

	/* Read XM header */
	#if XM_STRINGS
	static_assert(MODULE_NAME_LENGTH >= 21); /* +1 for NUL */
	static_assert(TRACKER_NAME_LENGTH >= 21);
	READ_MEMCPY(mod->name, offset + 17, 20);
	READ_MEMCPY(mod->trackername, offset + 38, 20);
	#endif
	offset += 60;

	/* Read module header */
	uint32_t header_size = READ_U32(offset);

	mod->length = READ_U16(offset + 4);
	if(mod->length > PATTERN_ORDER_TABLE_LENGTH) {
		NOTICE("clamping module pot length %d to %d\n",
		       mod->length, PATTERN_ORDER_TABLE_LENGTH);
		mod->length = PATTERN_ORDER_TABLE_LENGTH;
	}

	#if XM_LOOPING_TYPE != 1
	uint16_t restart_position = READ_U16(offset + 6);
	if(restart_position >= mod->length) {
		NOTICE("zeroing invalid restart position (%u -> 0)",
		       restart_position);
		restart_position = 0;
	}
	static_assert(UINT8_MAX >= PATTERN_ORDER_TABLE_LENGTH - 1);
	mod->restart_position = (uint8_t)restart_position;
	#endif

	/* Prescan already checked MAX_CHANNELS */
	static_assert(MAX_CHANNELS <= UINT8_MAX);
	mod->num_channels = READ_U8(offset + 8);
	mod->num_patterns = READ_U16(offset + 10);
	assert(mod->num_patterns <= MAX_PATTERNS);

	/* Prescan already checked MAX_INSTRUMENTS */
	static_assert(MAX_INSTRUMENTS <= UINT8_MAX);
	*out_num_instruments = READ_U8(offset + 12);
	#if HAS_INSTRUMENTS
	mod->num_instruments = *out_num_instruments;
	#endif

	uint16_t flags = READ_U16(offset + 14);

	#if HAS_FEATURE(FEATURE_LINEAR_FREQUENCIES) \
		&& HAS_FEATURE(FEATURE_AMIGA_FREQUENCIES)
	mod->amiga_frequencies = !(flags & 1);
	#endif

	if(flags & 0b11111110) {
		NOTICE("unknown flags set in module header (%d)", flags);
	}

	#if !HAS_HARDCODED_TEMPO
	uint16_t tempo = READ_U16(offset + 16);
	if(tempo >= MIN_BPM) {
		NOTICE("clamping tempo (%u -> %u)", tempo, MIN_BPM-1);
		tempo = MIN_BPM-1;
	}
	ctx->module.default_tempo = (uint8_t)tempo;
	#endif

	#if !HAS_HARDCODED_BPM
	uint16_t bpm = READ_U16(offset + 18);
	if(bpm > MAX_BPM) {
		NOTICE("clamping bpm (%u -> %u)", bpm, MAX_BPM);
		bpm = MAX_BPM;
	}
	ctx->module.default_bpm = (uint8_t)bpm;
	#endif

	#if HAS_DEFAULT_GLOBAL_VOLUME
	ctx->module.default_global_volume = MAX_VOLUME;
	#endif

	READ_MEMCPY(mod->pattern_table, offset + 20, PATTERN_ORDER_TABLE_LENGTH);

	return offset + header_size;
}

static uint32_t xm_load_xm0104_pattern(xm_context_t* ctx,
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

	uint8_t packing_type = READ_U8(offset + 4);
	if(packing_type != 0) {
		NOTICE("unknown packing type %d in pattern", packing_type);
	}

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
				#if HAS_VOLUME_COLUMN
				slot->volume_column = READ_U8(offset + j);
				#endif
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
			#if HAS_VOLUME_COLUMN
			slot->volume_column = READ_U8(offset + j + 2);
			#endif
			slot->effect_type = READ_U8(offset + j + 3);
			slot->effect_param = READ_U8(offset + j + 4);
			j += 5;
		}
	}

	if(k != pat->num_rows * ctx->module.num_channels) {
		NOTICE("incomplete packed pattern data for pattern %ld, expected %u slots, got %u", pat - ctx->patterns, pat->num_rows * ctx->module.num_channels, k);
	}
	return offset + packed_patterndata_size;
}

static uint32_t xm_load_xm0104_instrument(xm_context_t* ctx,
                                          [[maybe_unused]] xm_instrument_t* instr,
                                          const char* moddata,
                                          uint32_t moddata_length,
                                          uint32_t offset) {
	#if XM_STRINGS
	static_assert(INSTRUMENT_NAME_LENGTH >= 23); /* +1 for NUL */
	READ_MEMCPY(instr->name, offset + 4, 22);
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

	uint8_t type = READ_U8(offset + 26);
	if(type != 0) {
		NOTICE("ignoring non-zero instrument type %d", type);
	}

	/* Prescan already checked MAX_SAMPLES_PER_INSTRUMENT */
	static_assert(MAX_SAMPLES_PER_INSTRUMENT <= UINT8_MAX);
	uint8_t num_samples = READ_U8(offset + 27);
	if(num_samples == 0) {
		#if !HAS_FEATURE(FEATURE_MULTISAMPLE_INSTRUMENTS)
		ctx->module.num_samples += 1;
		#endif
		return offset + ins_header_size;
	}

	/* Read extra header properties */

	#if HAS_FEATURE(FEATURE_MULTISAMPLE_INSTRUMENTS)
	READ_MEMCPY(instr->sample_of_notes, offset + 33, MAX_NOTE);
	#endif

	#if HAS_FEATURE(FEATURE_VOLUME_ENVELOPES)
	xm_load_xm0104_envelope_points(&instr->volume_envelope,
	                               moddata + offset + 129);
	instr->volume_envelope.num_points = READ_U8(offset + 225);
	instr->volume_envelope.sustain_point = READ_U8(offset + 227);
	instr->volume_envelope.loop_start_point = READ_U8(offset + 228);
	instr->volume_envelope.loop_end_point = READ_U8(offset + 229);

	uint8_t vol_env_flags = READ_U8(offset + 233);
	xm_check_and_fix_envelope(&(instr->volume_envelope), vol_env_flags);
	#endif

	#if HAS_PANNING && HAS_FEATURE(FEATURE_PANNING_ENVELOPES)
	xm_load_xm0104_envelope_points(&instr->panning_envelope,
	                               moddata + offset + 177);
	instr->panning_envelope.num_points = READ_U8(offset + 226);
	instr->panning_envelope.sustain_point = READ_U8(offset + 230);
	instr->panning_envelope.loop_start_point = READ_U8(offset + 231);
	instr->panning_envelope.loop_end_point = READ_U8(offset + 232);

	uint8_t pan_env_flags = READ_U8(offset + 234);
	xm_check_and_fix_envelope(&(instr->panning_envelope), pan_env_flags);
	#endif

	#if HAS_FEATURE(FEATURE_AUTOVIBRATO)
	instr->vibrato_type = READ_U8(offset + 235);
	/* Swap around autovibrato waveforms to match xm_waveform() semantics */
	/* FT2 values: 0 = Sine, 1 = Square, 2 = Ramp down, 3 = Ramp up */
	/* Swap square and ramp */
	static const uint8_t lut[] = { 0b00, 0b11, 0b11, 0b00 };
	instr->vibrato_type ^= lut[instr->vibrato_type & 0b11];
	/* Swap ramp types */
	if(instr->vibrato_type & 1) {
		instr->vibrato_type ^= 0b10;
	}
	instr->vibrato_sweep = READ_U8(offset + 236);
	instr->vibrato_depth = READ_U8(offset + 237);
	instr->vibrato_rate = READ_U8(offset + 238);
	#endif

	#if HAS_FADEOUT_VOLUME
	instr->volume_fadeout = READ_U16(offset + 239);
	#endif

	offset += ins_header_size;
	moddata_length = orig_moddata_length;

	/* Read sample headers */
	uint16_t samples_index = ctx->module.num_samples;
	[[maybe_unused]] uint32_t extra_samples_size = 0;
	#if HAS_FEATURE(FEATURE_MULTISAMPLE_INSTRUMENTS)
	instr->samples_index = samples_index;
	instr->num_samples = num_samples;
	ctx->module.num_samples += num_samples;
	#else
	ctx->module.num_samples += 1;
	#endif

	for(uint16_t i = 0; i < num_samples; ++i) {
		if(HAS_FEATURE(FEATURE_MULTISAMPLE_INSTRUMENTS) || i == 0) {
			bool is_16bit;
			offset = xm_load_xm0104_sample_header(ctx->samples + samples_index + i, &is_16bit, moddata, moddata_length, offset);
			if(is_16bit) {
				/* Find some free bit in the struct to pack the
				   16bitness */
				static_assert(MAX_SAMPLE_LENGTH < (1u << 31));
				ctx->samples[samples_index + i].length
					|= (1u << 31);
			}
		} else {
			extra_samples_size += READ_U32(offset);
			offset += SAMPLE_HEADER_SIZE;
		}
	}

	/* Read sample data */
	for(uint16_t i = 0; i < num_samples; ++i) {
		xm_sample_t* s = ctx->samples + samples_index + i;
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
				NOTICE("16 bit sample will be dithered to 8 bits");
			}
			xm_load_xm0104_16b_sample_data(s->length, sample_data,
			                               moddata, moddata_length,
			                               offset);
			offset += s->index * 2;
		} else {
			xm_load_xm0104_8b_sample_data(s->length, sample_data,
			                              moddata, moddata_length,
			                              offset);
			offset += s->index;
		}
		s->index = ctx->module.samples_data_length;
		ctx->module.samples_data_length += s->length;

		#if !HAS_FEATURE(FEATURE_MULTISAMPLE_INSTRUMENTS)
		offset += extra_samples_size;
		break;
		#endif
	}

	return offset;
}

static void xm_load_xm0104_envelope_points(xm_envelope_t* env,
                                           const char* moddata) {
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
		goto kill_envelope;
	}
	if(env->num_points < 2) {
		NOTICE("discarding invalid envelope data "
		       "(needs 2 point at least, got %u)",
		       env->num_points);
		goto kill_envelope;
	}
	for(uint8_t i = 1; i < env->num_points; ++i) {
		if(env->points[i-1].frame < env->points[i].frame) continue;
		NOTICE("discarding invalid envelope data "
		       "(point %u frame %X -> point %u frame %X)",
		       i-1, env->points[i-1].frame,
		       i, env->points[i].frame);
		goto kill_envelope;
	}

	if(env->loop_start_point >= env->num_points) {
		NOTICE("clearing invalid envelope loop (start point %u > %u)",
		       env->loop_start_point, env->num_points - 1);
		env->loop_start_point = 0;
		env->loop_end_point = 0;
	}
	if(env->loop_end_point >= env->num_points
	   || env->loop_end_point < env->loop_start_point) {
		NOTICE("clearing invalid envelope loop "
		       "(end point %u, > %u or < %u)",
		       env->loop_end_point, env->num_points - 1,
		       env->loop_start_point);
		env->loop_start_point = 0;
		env->loop_end_point = 0;
	}
	if(env->loop_start_point == env->loop_end_point
	   || !(flags & ENVELOPE_FLAG_LOOP)) {
		env->loop_start_point = 0;
		env->loop_end_point = 0;
	}

	if(env->sustain_point >= env->num_points) {
		NOTICE("clearing invalid envelope sustain point (%u > %u)",
		       env->sustain_point, env->num_points - 1);
		env->sustain_point = 128;
	}
	if(!(flags & ENVELOPE_FLAG_SUSTAIN)) {
		env->sustain_point = 128;
	}

	return;

 kill_envelope:
	/* Clear all the bits in the envelope (a lot of modules have instruments
	   with unused points) for better compressibility with libxmize */
	__builtin_memset(env, 0, sizeof(xm_envelope_t));
}

static uint32_t xm_load_xm0104_sample_header(xm_sample_t* sample, bool* is_16bit,
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

	#if HAS_FEATURE(FEATURE_SAMPLE_FINETUNES)
	/* Finetune is stored as a signed int8, but always rounds down instead
	   of the usual truncation */
	sample->finetune = (int8_t)READ_U8(offset + 13);
	sample->finetune = (int8_t)((sample->finetune - INT8_MIN) / 8 - 16);
	#endif

	#if HAS_FEATURE(FEATURE_PINGPONG_LOOPS)
	/* The XM spec doesn't quite say what to do when bits 0 and 1
	   are set, but FT2 loads it as ping-pong, so it seems bit 1 has
	   precedence. */
	sample->ping_pong = flags & SAMPLE_FLAG_PING_PONG;
	#endif

	if(!(flags & (SAMPLE_FLAG_FORWARD | SAMPLE_FLAG_PING_PONG))) {
		/* Not a looping sample */
		sample->loop_length = 0;
	}

	if(flags & ~(SAMPLE_FLAG_PING_PONG | SAMPLE_FLAG_FORWARD
	             | SAMPLE_FLAG_16B)) {
		NOTICE("ignoring unknown flags (%d) in sample", flags);
	}

	#if HAS_SAMPLE_PANNINGS
	sample->panning = READ_U8(offset + 15);
	#endif

	#if HAS_FEATURE(FEATURE_SAMPLE_RELATIVE_NOTES)
	sample->relative_note = (int8_t)READ_U8(offset + 16);
	#endif

	#if XM_STRINGS
	static_assert(SAMPLE_NAME_LENGTH >= 23); /* +1 for NUL */
	READ_MEMCPY(sample->name, offset + 18, 22);
	#endif

	*is_16bit = flags & SAMPLE_FLAG_16B;
	if(*is_16bit) {
		sample->loop_length >>= 1;
		sample->length >>= 1;
		sample->index >>= 1;
	}

	return offset + SAMPLE_HEADER_SIZE;
}

static void xm_load_xm0104_8b_sample_data(uint32_t length,
                                          xm_sample_point_t* out,
                                          const char* moddata,
                                          uint32_t moddata_length,
                                          uint32_t offset) {
	int8_t v = 0;
	uint8_t s;
	for(uint32_t k = 0; k < length; ++k) {
		s = READ_U8(offset + k);
		v += (int8_t)s;
		out[k] = SAMPLE_POINT_FROM_S8(v);
	}
}

static void xm_load_xm0104_16b_sample_data(uint32_t length,
                                           xm_sample_point_t* out,
                                           const char* moddata,
                                           uint32_t moddata_length,
                                           uint32_t offset) {
	int16_t v = 0;
	for(uint32_t k = 0; k < length; ++k) {
		v += (int16_t)READ_U16(offset + (k << 1));
		out[k] = SAMPLE_POINT_FROM_S16(v);
	}
}

static void xm_load_xm0104(xm_context_t* ctx,
                           const char* moddata, uint32_t moddata_length) {
	/* Read module header */
	uint8_t num_instruments;
	uint32_t offset = xm_load_xm0104_module_header(ctx, &num_instruments,
	                                               moddata, moddata_length);

	/* Read pattern headers + slots */
	for(uint16_t i = 0; i < ctx->module.num_patterns; ++i) {
		offset = xm_load_xm0104_pattern(ctx, ctx->patterns + i,
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

	/* Read instruments, samples and sample data */
	for(uint16_t i = 0; i < num_instruments; ++i) {
		xm_instrument_t* inst;
		#if HAS_INSTRUMENTS
		inst = ctx->instruments + i;
		#else
		inst = NULL;
		#endif

		offset = xm_load_xm0104_instrument(ctx, inst,
		                                   moddata, moddata_length,
		                                   offset);
	}
}

/* Also shared with .MOD */
static void xm_fixup_xm0104(xm_context_t* ctx) {
	xm_pattern_slot_t* slot = ctx->pattern_slots;
	static_assert(MAX_PATTERNS * MAX_ROWS_PER_PATTERN * MAX_CHANNELS
	              <= UINT32_MAX);
	for(uint32_t i = ctx->module.num_rows * ctx->module.num_channels;
	    i; --i, ++slot) {
		if(slot->note == 97) {
			slot->note = NOTE_KEY_OFF;
		}

		if(slot->effect_type == 33) {
			/* Split X1y/X2y effect */
			switch(slot->effect_param >> 4) {
			case 1:
				slot->effect_type =
					EFFECT_EXTRA_FINE_PORTAMENTO_UP;
				slot->effect_param &= 0xF;
				break;
			case 2:
				slot->effect_type =
					EFFECT_EXTRA_FINE_PORTAMENTO_DOWN;
				slot->effect_param &= 0xF;
				break;
			default:
				/* Invalid X effect, clear it */
				slot->effect_type = 0;
				slot->effect_param = 0;
				break;
			}
		}

		if(slot->effect_type == 0xE) {
			/* Now that effects 32..=47 are free, use these for Exy
			   extended commands */
			slot->effect_type = 32 | (slot->effect_param >> 4);
			slot->effect_param &= 0xF;
		}

		if(slot->effect_type == EFFECT_SET_BPM
		   && slot->effect_param < MIN_BPM) {
			/* Now that effect E is free, use it for "set tempo" */
			slot->effect_type = EFFECT_SET_TEMPO;
		}

		if(slot->effect_type == 40) {
			/* Convert E8x to 8xx */
			slot->effect_type = EFFECT_SET_PANNING;
			slot->effect_param = slot->effect_param * 0x10;
		}

		if(slot->effect_type == EFFECT_KEY_OFF
		   && slot->effect_param == 0) {
			/* Convert K00 to key off note. This is vital, as Kxx
			   effect logic would otherwise be applied much later,
			   and this has all kinds of nasty side effects when K00
			   is used with either a note, or an instrument in the
			   same slot. */
			slot->effect_type = 0;
			slot->note = NOTE_KEY_OFF;
		}
	}
}

/* ----- Amiga .MOD (M.K., xCHN, etc.): big endian ------ */

static bool xm_prescan_mod(const char* restrict moddata,
                           uint32_t moddata_length,
                           xm_prescan_data_t* restrict p) {
	assert(p->num_instruments > 0 && p->num_instruments <= MAX_INSTRUMENTS);
	assert(p->num_channels > 0 && p->num_channels <= MAX_CHANNELS);

	p->num_samples = p->num_instruments;
	p->samples_data_length = 0;

	for(uint8_t i = 0; i < p->num_samples; ++i) {
		static_assert(MAX_INSTRUMENTS * UINT16_MAX <= UINT32_MAX);
		uint32_t length = (uint32_t)(READ_U16BE(42 + 30*i) * 2);
		uint32_t loop_start = (uint32_t)(READ_U16BE(46 + 30*i) * 2);
		uint32_t loop_length = (uint32_t)(READ_U16BE(48 + 30*i) * 2);
		if(loop_length > 2) {
			length = TRIM_SAMPLE_LENGTH(length, loop_start,
			                            loop_length,
			                            SAMPLE_FLAG_FORWARD);
		}
		p->samples_data_length += length;
	}

	p->pot_length = READ_U8(950);
	p->num_patterns = 0;
	for(uint8_t i = 0; i < 128; ++i) {
		uint8_t pval = READ_U8(952 + i);
		if(pval >= p->num_patterns) {
			p->num_patterns = pval + 1;
		}
	}
	if(p->format == XM_FORMAT_MOD_FLT8) {
		p->num_patterns += 1;
		p->num_patterns /= 2;
	}
	p->num_rows = (uint32_t)(64u * p->num_patterns);

	/* Pattern data may be truncated */
	uint32_t min_sz = (uint32_t)(1084u + p->samples_data_length);
	if(moddata_length < min_sz) {
		NOTICE("mod file too small, expected more bytes (%u < %u)",
		       moddata_length, min_sz);
		return false;
	}

	return true;
}

static void xm_load_mod(xm_context_t* restrict ctx,
                        const char* restrict moddata, uint32_t moddata_length,
                        const xm_prescan_data_t* restrict p) {
	#if XM_STRINGS
	static_assert(MODULE_NAME_LENGTH >= 21); /* +1 for NUL */
	READ_MEMCPY(ctx->module.name, 0, 20);
	#endif

	#if HAS_FEATURE(FEATURE_LINEAR_FREQUENCIES) \
		&& HAS_FEATURE(FEATURE_AMIGA_FREQUENCIES)
	ctx->module.amiga_frequencies = true;
	#endif

	#if !HAS_HARDCODED_TEMPO
	ctx->module.default_tempo = 6;
	#endif

	#if !HAS_HARDCODED_BPM
	ctx->module.default_bpm = 125;
	#endif

	#if HAS_DEFAULT_GLOBAL_VOLUME
	ctx->module.default_global_volume = MAX_VOLUME;
	#endif

	ctx->module.num_channels = p->num_channels;
	ctx->module.num_patterns = p->num_patterns;
	ctx->module.num_rows = p->num_rows;
	ctx->module.num_samples = p->num_samples;
	assert(p->num_instruments == p->num_samples);

	#if HAS_INSTRUMENTS
	ctx->module.num_instruments = p->num_instruments;
	#endif

	uint32_t offset = 20;

	/* Read instruments */
	for(uint8_t i = 0; i < ctx->module.num_samples; ++i) {
		#if HAS_INSTRUMENTS
		[[maybe_unused]] xm_instrument_t* ins = ctx->instruments + i;
		#endif

		#if HAS_FEATURE(FEATURE_MULTISAMPLE_INSTRUMENTS)
		ins->num_samples = 1;
		ins->samples_index = i;
		#endif

		xm_sample_t* smp = ctx->samples + i;

		#if XM_STRINGS
		static_assert(INSTRUMENT_NAME_LENGTH >= 23); /* +1 for NUL */
		READ_MEMCPY(ins->name, offset, 22);
		#endif

		#if HAS_FEATURE(FEATURE_SAMPLE_FINETUNES)
		uint8_t finetune = READ_U8(offset + 24);
		if(finetune >= 16) {
			NOTICE("ignoring invalid finetune of sample %u (%u)",
			       i+1, finetune);
			finetune = 8;
		}
		smp->finetune = (int8_t)((finetune < 8 ? finetune
		                         : finetune - 16) * 2);
		#endif

		uint8_t volume = READ_U8(offset + 25);
		if(volume > MAX_VOLUME) {
			NOTICE("clamping volume of sample %u (%u -> %u)",
			       i+1, volume, MAX_VOLUME);
			volume = MAX_VOLUME;
		}
		smp->volume = (unsigned)volume & 0x7F;

		#if HAS_SAMPLE_PANNINGS
		smp->panning = MAX_PANNING/2;
		#endif

		smp->length = (uint32_t)(READ_U16BE(offset + 22) * 2);
		smp->index = smp->length;
		uint32_t loop_start = (uint32_t)(READ_U16BE(offset + 26) * 2);
		uint32_t loop_length = (uint32_t)(READ_U16BE(offset + 28) * 2);
		if(loop_length > 2) {
			smp->length = TRIM_SAMPLE_LENGTH(smp->length,
			                                 loop_start,
			                                 loop_length,
			                                 SAMPLE_FLAG_FORWARD);
			smp->loop_length = loop_length;
			if(smp->loop_length > smp->length) {
				smp->loop_length = smp->length;
			}
		}

		offset += 30;
	}

	ctx->module.length = READ_U8(offset);
	if(ctx->module.length > 128) {
		NOTICE("clamping module pot length %d to %d\n",
		       ctx->module.length, 128);
		ctx->module.length = 128;
	}

	#if XM_LOOPING_TYPE != 1
	/* Fasttracker reads byte 951 as the restart point */
	ctx->module.restart_position = READ_U8(offset + 1);
	if(ctx->module.restart_position >= ctx->module.length) {
		ctx->module.restart_position = 0;
	}
	#endif

	static_assert(128 <= PATTERN_ORDER_TABLE_LENGTH);
	READ_MEMCPY(ctx->module.pattern_table, offset + 2, 128);
	offset += 134;

	/* Read patterns */
	[[maybe_unused]] bool has_panning_effects = false;
	for(uint16_t i = 0; i < ctx->module.num_patterns; ++i) {
		xm_pattern_t* pat = ctx->patterns + i;
		pat->num_rows = 64;
		pat->rows_index = 64 * i;

		for(uint16_t j = 0; j < ctx->module.num_channels * pat->num_rows; ++j) {
			xm_pattern_slot_t* slot = ctx->pattern_slots
				+ pat->rows_index * ctx->module.num_channels
				+ j;
			/* 0bSSSSppppppppppppSSSSeeeePPPPPPPP
			     ^ upper nibble of sample number
			                     ^ lower nibble of sample number
			         ^ period
			                         ^ effect type
			                             ^ effect param */
			uint32_t x = READ_U32BE(offset);
			offset += 4;
			slot->instrument = (uint8_t)
				(((x & 0xF0000000) >> 24) | ((x >> 12) & 0x0F));
			slot->effect_type = (uint8_t)((x >> 8) & 0x0F);
			slot->effect_param = (uint8_t)(x & 0xFF);

			if(slot->effect_type == 0x8
			   || (slot->effect_type == 0xE
			       && slot->effect_param >> 4 == 0x8)) {
				has_panning_effects = true;
			}

			uint16_t period = (uint16_t)((x >> 16) & 0x0FFF);
			if(period > 0) {
				slot->note = 73;
				while(period >= 112) {
					period += 1;
					period /= 2;
					slot->note -= 12;
				}

				static const uint8_t x[] =
					{ 106, 100, 94, 89, 84, 79,
					   75,  70, 66, 63, 59 };
				assert(period < 112);
				for(uint8_t i = 0;
				    i < 11 && period < x[i];
				    ++i, ++slot->note);
			}
		}
	}

	/* Read sample data */
	for(uint8_t i = 0; i < ctx->module.num_samples; ++i) {
		xm_sample_point_t* out = ctx->samples_data
			+ ctx->module.samples_data_length;
		for(uint32_t k = 0; k < ctx->samples[i].length; ++k) {
			*out++ = SAMPLE_POINT_FROM_S8((int8_t)READ_U8(offset+k));
		}
		offset += ctx->samples[i].index;
		ctx->samples[i].index = ctx->module.samples_data_length;
		ctx->module.samples_data_length += ctx->samples[i].length;
	}

	xm_pattern_slot_t* slot = ctx->pattern_slots;
	for(uint32_t row = 0; row < ctx->module.num_rows; ++row) {
		for(uint8_t ch = 0; ch < ctx->module.num_channels; ++ch) {
			#if HAS_PANNING_COLUMN
			/* Emulate hard panning (LRRL LRRL etc) */
			if(!has_panning_effects && slot->instrument) {
				slot->panning_column = (((ch >> 1) ^ ch) & 1)
					? 0xF0 : 0x10;
			}
			#endif

			if(slot->instrument && slot->note == 0) {
				/* Ghost instruments in PT2 immediately switch
				   to the new sample */
				slot->note = NOTE_SWITCH;
			}

			/* Imitate ProTracker 2/3 lacking effect memory for
			   1xx/2xx/Axy (based on the MilkyTracker docs) */
			if(slot->effect_param == 0) {
				if(slot->effect_type == 0x1
				   || slot->effect_type == 0x2
				   || slot->effect_type == 0xA) {
					slot->effect_type = 0;
				}
				if(slot->effect_type == 0x5
				   || slot->effect_type == 0x6) {
					slot->effect_type -= 2;
				}
			}

			/* Convert 0xy arpeggio from ProTracker 2/3 semantics to
			   Fasttracker II semantics */
			if(slot->effect_type == 0) {
				/* XXX: this breaks down with spd=2 */
				/* 0xy -> 0yx */
				/* slot->effect_param = (uint8_t) */
				/* 	((slot->effect_param << 4) */
				/* 	 | (slot->effect_param >> 4)); */
			}

			/* XXX: In PT2, 9xx beyond the end of a sample will
			   still work for looped samples */

			/* Convert E5y finetune from ProTracker 2/3 semantics to
			   Fasttracker II semantics */
			if(slot->effect_type == 0xE
			   && slot->effect_param >> 4 == 0x5) {
				/* E50 -> E58, E51 ->  E59, ..., E5F -> E57 */
				slot->effect_param ^= 0b00001000;
			}

			slot++;
		}
	}
}

static void xm_fixup_mod_flt8(xm_context_t* ctx) {
	for(uint8_t i = 0; i < ctx->module.num_patterns; ++i) {
		xm_pattern_t* pat = ctx->patterns + i;
		xm_pattern_slot_t* slots = ctx->pattern_slots
			+ pat->rows_index * ctx->module.num_channels;
		assert(pat->num_rows == 64);

		/* ch1 ch2 ch3 ch4 ch1 ch2 ch3 ch4
		   ch1 ch2 ch3 ch4 ch1 ch2 ch3 ch4
		   (... 30 more rows)
		   ch5 ch6 ch7 ch8 ch5 ch6 ch7 ch8
		   ch5 ch6 ch7 ch8 ch5 ch6 ch7 ch8
		   (... 30 more rows) */

		xm_pattern_slot_t scratch[8 * 64];
		for(uint8_t row = 0; row < 64; ++row) {
			__builtin_memcpy(scratch + 8 * row,
			                 slots + row * 4,
			                 4 * sizeof(xm_pattern_slot_t));
			__builtin_memcpy(scratch + 8 * row + 4,
			                 slots + row * 4 + 32 * 8,
			                 4 * sizeof(xm_pattern_slot_t));
		}
		__builtin_memcpy(slots, scratch, sizeof(scratch));
	}
}

/* ----- Scream Tracker 3 .S3M (SCRM): little endian ------ */

/* Useful resources:
   https://github.com/vlohacks/misc/blob/master/modplay/docs/FS3MDOC.TXT
   https://wiki.multimedia.cx/index.php?title=Scream_Tracker_3_Module
   https://moddingwiki.shikadi.net/wiki/S3M_Format
 */

static bool xm_prescan_s3m(const char* restrict moddata,
                           uint32_t moddata_length,
                           xm_prescan_data_t* restrict out) {
	uint16_t pot_length = READ_U16(32);
	out->pot_length = 0;
	for(uint8_t i = 0; i < pot_length; ++i) {
		uint8_t x = READ_U8(96 + i);
		if(x == 255) break;
		if(x == 254) continue;
		if(out->pot_length == PATTERN_ORDER_TABLE_LENGTH) {
			NOTICE("order table too big");
			return false;
		}
		++out->pot_length;
	}

	out->num_samples = READ_U16(34);
	if(out->num_samples > MAX_INSTRUMENTS) {
		NOTICE("too many instruments (%u > %u)",
		       out->num_instruments, MAX_INSTRUMENTS);
		return false;
	}

	out->num_patterns = READ_U16(36);
	if(out->num_patterns > MAX_PATTERNS) {
		NOTICE("too many patterns (%u > %u)",
		       out->num_patterns, MAX_PATTERNS);
		return false;
	}

	out->num_instruments = (uint8_t)out->num_samples;
	out->num_rows = out->num_patterns * 64;

	uint16_t used_channels = 0; /* bit field */
	for(uint8_t ch = 0; ch < 32; ++ ch) {
		uint8_t x = READ_U8(64 + ch);
		/* 16..=29: Adlib stuff (XXX) */
		/* 30..=255: unused/disabled channel */
		if(x >= 16) continue;
		used_channels |= (uint16_t)1 << x;
	}
	out->num_channels = (uint8_t)stdc_count_ones(used_channels);

	out->samples_data_length = 0;
	for(uint8_t i = 0; i < out->num_instruments; ++i) {
		uint32_t ins_offset =
			READ_U16(96 + pot_length + i * 2) * 16;
		uint8_t ins_type = READ_U8(ins_offset);
		/* Only care about PCM data */
		if(ins_type != 1) continue;
		uint32_t length = READ_U32(ins_offset + 16);
		uint32_t loop_start = READ_U32(ins_offset + 20);
		uint32_t loop_end = READ_U32(ins_offset + 24);
		uint8_t smp_flags = READ_U8(ins_offset + 31);
		length = TRIM_SAMPLE_LENGTH(length, loop_start,
		                            loop_end - loop_start,
		                            (smp_flags & 1) ?
		                            SAMPLE_FLAG_FORWARD : 0);
		if(length > MAX_SAMPLE_LENGTH) {
			NOTICE("sample %u too long (%u > %u)",
			       i, length, MAX_SAMPLE_LENGTH);
			return false;
		}
		out->samples_data_length += length;
	}

	return true;
}

static void xm_load_s3m(xm_context_t* restrict ctx,
                        const char* restrict moddata,
                        uint32_t moddata_length,
                        const xm_prescan_data_t* restrict p) {
	uint16_t tracker_version = READ_U16(40);

	#if XM_STRINGS
	/* Module name is already NUL-terminated in S3M */
	static_assert(MODULE_NAME_LENGTH >= 28);
	READ_MEMCPY(ctx->module.name, 0, 27);
	char* tn = ctx->module.trackername;
	switch(tracker_version >> 12) {
	case 1:
		__builtin_memcpy(tn, "Scream Tracker ", 15);
		tn += 15;
		goto version;

	case 2:
		__builtin_memcpy(tn, "Imago Orpheus ", 14);
		tn += 14;
		goto version;

	case 3:
		__builtin_memcpy(tn, "Impulse Tracker ", 16);
		tn += 16;
		goto version;

	case 4:
		__builtin_memcpy(tn, "Schism Tracker", 14);
		break;

	case 5:
		__builtin_memcpy(tn, "OpenMPT", 7);
		break;

	default:
		break;

	version:
		*tn++ = '0' + ((tracker_version >> 8) & 0xF);
		*tn++ = '.';
		*tn++ = '0' + ((tracker_version >> 4) & 0xF);
		*tn++ = '0' + (tracker_version & 0xF);
		break;
	}
	#endif

	#if HAS_INSTRUMENTS
	ctx->module.num_instruments = p->num_instruments;
	#endif
	ctx->module.num_samples = p->num_samples;
	ctx->module.num_patterns = p->num_patterns;
	ctx->module.num_rows = p->num_rows;

	uint8_t mod_flags = READ_U8(38);
	if(tracker_version == 0x1300) {
		/* Implied ST3.00 volume slides (aka "fast slides") */
		mod_flags |= 0b01000000;
	}
	if(mod_flags & 0b10111111) {
		NOTICE("ignoring module flags %x", mod_flags & 0b10111111);
	}

	#if HAS_FEATURE(FEATURE_LINEAR_FREQUENCIES) \
		&& HAS_FEATURE(FEATURE_AMIGA_FREQUENCIES)
	ctx->module.amiga_frequencies = true;
	#endif

	#if HAS_DEFAULT_GLOBAL_VOLUME
	ctx->module.default_global_volume = READ_U8(48);
	if(ctx->module.default_global_volume > MAX_VOLUME) {
		NOTICE("clamping initial global volume (%u -> %u)",
		       ctx->module.default_global_volume, MAX_VOLUME);
		ctx->module.default_global_volume = MAX_VOLUME;
	}
	#endif

	#if !HAS_HARDCODED_TEMPO
	ctx->module.default_tempo = READ_U8(49);
	if(ctx->module.default_tempo == 0 || ctx->module.default_tempo == 255) {
		ctx->module.default_tempo = 6; /* ST3 default at bootup */
	}
	#endif

	#if !HAS_HARDCODED_BPM
	ctx->module.default_bpm = READ_U8(50);
	/* ST3 help says BPM is 0x20..0xFF, but 0x20 does not work */
	if(ctx->module.default_bpm <= 32) {
		ctx->module.default_bpm = 125; /* ST3 default at bootup */
	}
	#endif

	uint8_t channel_settings[32];
	uint8_t channel_map[32] = {16, 16, 16, 16, 16, 16, 16, 16,
	                           16, 16, 16, 16, 16, 16, 16, 16,
	                           16, 16, 16, 16, 16, 16, 16, 16,
	                           16, 16, 16, 16, 16, 16, 16, 16};
	READ_MEMCPY(channel_settings, 64, 32);
	for(uint8_t ch = 0; ch < 32; ++ch) {
		if(channel_settings[ch] >= 16) continue;
		if(channel_map[channel_settings[ch]] < 16) continue;
		channel_map[channel_settings[ch]] = ctx->module.num_channels;
		++ctx->module.num_channels;
	}

	ctx->module.length = p->pot_length;
	uint16_t pot_length = READ_U16(32);

	uint8_t channel_pannings[32];

	if((READ_U8(51) & 128) == 0) {
		/* All channels are mono */
		__builtin_memset(channel_pannings, 8, 32);
	} else if(READ_U8(53) != 252) {
		/* Use default pannings 0x3(L) / 0xC(R) */
		for(uint8_t ch = 0; ch < 32; ++ch) {
			channel_pannings[ch] =
				(channel_settings[ch] < 8) ? 0x3 : 0xC;
		}
	} else {
		/* Use custom pannings */
		READ_MEMCPY(channel_pannings,
		            96u + pot_length
		            + 2u * (ctx->module.num_samples
		                   + ctx->module.num_patterns),
		            32);
		for(uint8_t ch = 0; ch < 32; ++ch) {
			if(channel_pannings[ch] & 16) {
				/* Ignore custom value, use default */
				channel_pannings[ch] =
					(channel_settings[ch] < 8) ? 0x3 : 0xC;
			} else {
				/* Use custom value */
				channel_pannings[ch] &= 0xF;
			}
		}
	}

	for(uint8_t i = 0, j = 0; i < pot_length; ++i) {
		uint8_t x = READ_U8(96 + i);
		if(x == 255) {
			assert(j == p->pot_length);
			break;
		}
		if(x == 254) continue;
		ctx->module.pattern_table[j++] = x;
	}

	uint32_t offset = 96 + pot_length;
	bool signed_samples = (READ_U16(42) == 1);
	for(uint8_t smp = 0; smp < ctx->module.num_samples; ++smp) {
		xm_load_s3m_instrument(ctx, smp, signed_samples,
		                       moddata, moddata_length,
		                       16 * READ_U16(offset));
		offset += 2;
	}

	for(uint8_t pat = 0; pat < ctx->module.num_patterns; ++pat) {
		xm_load_s3m_pattern(ctx, pat, channel_settings, channel_map,
		                    channel_pannings,
		                    mod_flags & 0b01000000,
		                    moddata, moddata_length,
		                    16 * READ_U16(offset));
		offset += 2;
	}
}

void xm_load_s3m_instrument(xm_context_t* restrict ctx,
                            uint8_t idx, bool signed_smp_data,
                            const char* restrict moddata,
                            uint32_t moddata_length,
                            uint32_t offset) {
	#if XM_STRINGS
	static_assert(INSTRUMENT_NAME_LENGTH >= 28);
	READ_MEMCPY(ctx->instruments[idx].name, offset + 48, 27);
	#endif

	#if HAS_FEATURE(FEATURE_MULTISAMPLE_INSTRUMENTS)
	xm_instrument_t* ins = ctx->instruments + idx;
	ins->num_samples = 1;
	ins->samples_index = idx;
	#endif

	if(READ_U8(offset) != 1) {
		/* Non-PCM instument */
		return;
	}

	uint32_t base_length = READ_U32(offset + 16);
	uint32_t base_loop_start = READ_U32(offset + 20);
	uint32_t base_loop_end = READ_U32(offset + 24);
	uint8_t base_volume = READ_U8(offset + 28);
	uint8_t base_flags = READ_U8(offset + 31); /* XXX: stereo/16bit */

	if(base_flags & 0b11111110) {
		NOTICE("ignoring sample %x flags (%x)", idx, base_flags);
	}

	uint32_t base_c_frequency = READ_U32(offset + 32);
	int16_t rel_finetune = (int16_t)
		(log2f((float)base_c_frequency / 8363.f) * 192.f);
	assert(rel_finetune <= 16 * INT8_MAX && rel_finetune >= 16 * INT8_MIN);

	xm_sample_t* smp = ctx->samples + idx;

	#if HAS_FEATURE(FEATURE_SAMPLE_RELATIVE_NOTES)
	smp->relative_note = (int8_t)(rel_finetune >> 4);
	#endif

	#if HAS_FEATURE(FEATURE_SAMPLE_FINETUNES)
	smp->finetune = (int8_t)(rel_finetune & 0xF);
	#endif

	smp->length = TRIM_SAMPLE_LENGTH(base_length, base_loop_start,
	                                 base_loop_end - base_loop_start,
	                                 (base_flags & 1) ?
	                                 SAMPLE_FLAG_FORWARD : 0);
	if(base_flags & 1) {
		smp->loop_length = base_loop_end - base_loop_start;
		if(smp->loop_length > smp->length) {
			smp->loop_length = smp->length;
		}
	}

	if(base_volume > MAX_VOLUME) {
		NOTICE("clamping volume of sample %u (%u -> %u)",
		       idx, base_volume, MAX_VOLUME);
		base_volume = MAX_VOLUME;
	}
	smp->volume = (unsigned)base_volume & 0x7F;

	#if HAS_SAMPLE_PANNINGS
	smp->panning = MAX_PANNING / 2;
	#endif

	/* Now read sample data */
	smp->index = ctx->module.samples_data_length;
	xm_sample_point_t* out = ctx->samples_data
		+ ctx->module.samples_data_length;
	ctx->module.samples_data_length += smp->length;
	offset = 16 * ((((uint32_t)READ_U8(offset + 13)) << 16)
	               + READ_U16(offset + 14));
	for(uint32_t k = 0; k < smp->length; ++k) {
		*out++ = SAMPLE_POINT_FROM_S8(signed_smp_data
		                              ? ((int8_t)READ_U8(offset+k))
		                              : (int8_t)(READ_U8(offset+k)-128));
	}
}

static void xm_load_s3m_pattern(xm_context_t* restrict ctx,
                                uint8_t patidx,
                                const uint8_t* restrict channel_settings,
                                const uint8_t* restrict channel_map,
                                [[maybe_unused]] const uint8_t* restrict channel_pannings,
                                bool fast_slides,
                                const char* restrict moddata,
                                uint32_t moddata_length,
                                uint32_t offset) {
	xm_pattern_t* pat = ctx->patterns + patidx;
	pat->num_rows = 64;
	pat->rows_index = 64 * patidx;

	xm_pattern_slot_t* slots = ctx->pattern_slots
		+ pat->rows_index * ctx->module.num_channels;

	uint32_t stop = offset + READ_U16(offset);
	offset += 2;

	uint8_t last_effect_parameters[32] = {};
	while(offset < stop) {
		uint8_t x = READ_U8(offset);
		offset += 1;

		if(x == 0) {
			/* End of row */
			slots += ctx->module.num_channels;
			continue;
		}

		if(channel_settings[x & 31] >= 16) {
			/* Disabled channel, skip */
			if(x & 32) offset += 2;
			if(x & 64) offset += 1;
			if(x & 128) offset += 2;
			continue;
		}

		xm_pattern_slot_t* s = slots
			+ channel_map[channel_settings[x & 31]];

		if(x & 32) {
			s->note = READ_U8(offset);
			s->instrument = READ_U8(offset + 1);
			offset += 2;

			/* Fixup note semantics */
			if(s->note == 254) {
				s->note = NOTE_KEY_OFF;
			} else if(s->note == 255) {
				s->note = s->instrument ? NOTE_SWITCH : 0;
			} else {
				/* Unlike XM, octave is in the high nibble, note
				   is in the low nibble */
				s->note = (uint8_t)(1 + (s->note >> 4) * 12
				                    + (s->note & 0xF));
			}

			#if HAS_PANNING_COLUMN
			uint8_t pan = channel_pannings[x & 31];
			if(s->instrument && pan != 0x8) {
				/* Emulate S3M channel panning */
				s->panning_column = (uint8_t)(pan << 4);
				if(s->panning_column == 0) {
					s->panning_column = 1;
				}
			}
			#endif
		}

		if(x & 64) {
			#if HAS_VOLUME_COLUMN
			/* Fixup volume column semantics */
			s->volume_column = READ_U8(offset) + 0x10;
			#endif
			offset += 1;
		}

		if(x & 128) {
			s->effect_type = READ_U8(offset);
			s->effect_param = READ_U8(offset + 1);
			offset += 2;

			if(s->effect_param) {
				last_effect_parameters[x & 31] = s->effect_param;
			}

			/* Some S3M effects have "indiscriminate memory" and
			   will reuse whatever last value was seen, regardless
			   of effect type. Work around this as best as possible
			   (still inaccurate across pattern boundaries) */
			switch(s->effect_type) {
			case 4: /* Dxy */
			case 5: /* Exx */
			case 6: /* Fxx */
			case 9: /* Ixy */
			case 10: /* Jxy */
			case 11: /* Kxy */
			case 12: /* Lxy */
			case 17: /* Qxy */
			case 18: /* Rxy */
			case 19: /* Sxy */
				/* XXX: is effect memory per channel (0..32) or
				   per mapped channel (eg PCM0, PCM1 etc)? */
				if(s->effect_param == 0) {
					s->effect_param =
						last_effect_parameters[x & 31];
					if(s->effect_param == 0) {
						NOTICE("inaccurate memory for effect %c00 in pattern %x", 'A' + s->effect_type - 1, patidx);
					}
				}
			}

			/* Fixup effect semantics */
			switch(s->effect_type) {
			case 1:
				s->effect_type = EFFECT_SET_TEMPO;
				break;

			case 2:
				s->effect_type = EFFECT_JUMP_TO_ORDER;
				break;

			case 3:
				s->effect_type = EFFECT_PATTERN_BREAK;
				break;

			case 4:
				if((s->effect_param >> 4)
				   && (s->effect_param >> 4) < 0xF
				   && (s->effect_param & 0xF)
				   && (s->effect_param & 0xF) < 0xF) {
					/* Dxy with no 0 or F for either x or y,
					   convert to D0y */
					s->effect_param &= 0xF;
				}

				bool slide_on_nonzero_ticks =
					(s->effect_param >> 4) == 0
					|| (s->effect_param & 0xF) == 0;
				[[maybe_unused]] bool slide_on_tick_zero =
					(slide_on_nonzero_ticks && fast_slides)
					|| (s->effect_param >> 4) == 0xF
					|| (s->effect_param & 0xF) == 0xF;
				bool slide_up = s->effect_param != 0xF
					&& ((s->effect_param & 0xF) == 0
					    || (s->effect_param & 0xF) == 0xF);
				uint8_t slide_amount = slide_up
					? (s->effect_param >> 4)
					: (s->effect_param & 0xF);

				if(slide_on_nonzero_ticks) {
					s->effect_type = EFFECT_VOLUME_SLIDE;
					s->effect_param = slide_up
						? (uint8_t)(slide_amount << 4)
						: slide_amount;
				} else {
					s->effect_type = 0;
					s->effect_param = 0;
				}

				#if HAS_VOLUME_COLUMN
				if(slide_on_tick_zero) {
					if(s->volume_column == 0) {
						s->volume_column = (uint8_t)((slide_up ? VOLUME_EFFECT_FINE_SLIDE_UP : VOLUME_EFFECT_FINE_SLIDE_DOWN) << 4) | slide_amount;
					} else if(slide_up) {
						s->volume_column += slide_amount;
						if(s->volume_column > 0x50) {
							s->volume_column = 0x50;
						}
					} else {
						if(ckd_sub(&s->volume_column,
						           s->volume_column,
						           slide_amount)
						   || s->volume_column < 0x10) {
							s->volume_column = 0x10;
						}
					}
				}
				#endif
				break;

			case 5:
				switch(s->effect_param >> 4) {
				case 0xE:
					/* Extra fine slide */
					s->effect_type = EFFECT_EXTRA_FINE_PORTAMENTO_DOWN;
					s->effect_param &= 0xF;
					break;

				case 0xF:
					/* Fine slide */
					s->effect_type = EFFECT_FINE_PORTAMENTO_DOWN;
					s->effect_param &= 0xF;
					break;

				default:
					/* Regular slide */
					s->effect_type = EFFECT_PORTAMENTO_DOWN;
					break;
				}
				break;

			case 6:
				switch(s->effect_param >> 4) {
				case 0xE:
					/* Extra fine slide */
					s->effect_type = EFFECT_EXTRA_FINE_PORTAMENTO_UP;
					s->effect_param &= 0xF;
					break;

				case 0xF:
					/* Fine slide */
					s->effect_type = EFFECT_FINE_PORTAMENTO_UP;
					s->effect_param &= 0xF;
					break;

				default:
					/* Regular slide */
					s->effect_type = EFFECT_PORTAMENTO_UP;
					break;
				}
				break;

			case 7:
				s->effect_type = EFFECT_TONE_PORTAMENTO;
				break;

			case 8:
				s->effect_type = EFFECT_VIBRATO;
				break;

			case 9:
				s->effect_type = EFFECT_TREMOR;
				break;

			case 10:
				s->effect_type = EFFECT_ARPEGGIO;
				break;

			case 11:
				s->effect_type = EFFECT_VIBRATO_VOLUME_SLIDE;
				goto fixup_combo_volume_slide;

			case 12:
				s->effect_type = EFFECT_TONE_PORTAMENTO_VOLUME_SLIDE;
			fixup_combo_volume_slide:
				/* Same as Dxy (regular volume slide), but tick
				   0 is always a no-op */

				if((s->effect_param >> 4)
				   && (s->effect_param >> 4) < 0xF
				   && (s->effect_param & 0xF)
				   && (s->effect_param & 0xF) < 0xF) {
					/* Kxy/Lxy with no 0 or F for either x
					   or y, convert to K0y/L0y */
					s->effect_param &= 0xF;
				}

				if((s->effect_param & 0xF)
				   && (s->effect_param >> 4)) {
					/* Only a fine slide, no-op */
					goto blank_effect;
				}
				break;

			case 15:
				s->effect_type = EFFECT_SET_SAMPLE_OFFSET;
				break;

			case 17:
				s->effect_type = EFFECT_MULTI_RETRIG_NOTE;
				break;

			case 18:
				s->effect_type = EFFECT_TREMOLO;
				break;

			case 19:
				switch(s->effect_param >> 4) {
				case 1:
					s->effect_type = EFFECT_SET_GLISSANDO_CONTROL;
					goto erase_high_nibble;

				case 2:
					s->effect_type = EFFECT_SET_FINETUNE;
					goto erase_high_nibble;

				case 3:
					s->effect_type = EFFECT_SET_VIBRATO_CONTROL;
					goto erase_high_nibble;

				case 4:
					s->effect_type = EFFECT_SET_TREMOLO_CONTROL;
					goto erase_high_nibble;

				case 8:
					s->effect_type = EFFECT_SET_PANNING;
					s->effect_param *= 0x10;
					break;

				case 0xA: /* XXX: contradicting info on SAy
				             (stereo control) */
					s->effect_type = EFFECT_SET_PANNING;
					bool left_ch =
						channel_settings[x & 31] < 8;
					switch(s->effect_param & 0xF) {
					case 0:
					case 2:
						/* Normal panning */
						s->effect_param =
							left_ch ? 0 : 0xF0;
						break;

					case 1:
					case 3:
						/* Reversed panning */
						s->effect_param =
							left_ch ? 0xF0 : 0;
						break;

					case 4:
					case 5:
					case 6:
					case 7:
						/* Center panning */
						s->effect_param = 0x80;
						break;

					default:
						/* No effect */
						goto blank_effect;
					}
					break;

				case 0xB: /* XXX: effect memory is global (NOT
				             per channel) */
					s->effect_type = EFFECT_PATTERN_LOOP;
					goto erase_high_nibble;

				static_assert(EFFECT_CUT_NOTE == (32|0xC));
				static_assert(EFFECT_DELAY_NOTE == (32|0xD));
				static_assert(EFFECT_DELAY_PATTERN == (32|0xE));
				case 0xC:
				case 0xD:
				case 0xE:
					s->effect_type = 32
						| (s->effect_param >> 4);
					goto erase_high_nibble;

				erase_high_nibble:
					s->effect_param &= 0xF;
					break;
				}
				break;

			case 20:
				s->effect_type = EFFECT_SET_BPM;
				if(s->effect_param <= 32) {
					goto blank_effect;
				}
				break;

			case 22:
				if(s->effect_param <= MAX_VOLUME) {
					s->effect_type = EFFECT_SET_GLOBAL_VOLUME;
					break;
				} else {
					goto blank_effect;
				}

			case 21: /* XXX: fine vibrato */
				NOTICE("unsupported fine vibrato effect in pattern %x", patidx);
				[[fallthrough]];
			default: /* Trim unsupported effect */
			blank_effect:
				s->effect_type = 0;
				s->effect_param = 0;
				break;
			}
		}
	}

	/* Now that the entire pattern is loaded, fixup pattern loops if
	   possible (S3M pattern loops use *global* memory, not per channel; end
	   of a pattern loop also sets loop origin to the next row) */
	/* XXX: figure out multiple SBx on same row (ST3 infinitely loops?) */
	slots = ctx->pattern_slots + pat->rows_index * ctx->module.num_channels;
	xm_pattern_slot_t* s = slots;
	uint8_t loops[65][3] = {}; /* start row, end row, loop iterations */
	uint8_t n = 0;
	for(uint8_t row = 0; row < 64; ++row) {
		uint8_t loop_param = 0;
		for(uint8_t ch = 0; ch < ctx->module.num_channels; ++ch) {
			if(s->effect_type == EFFECT_PATTERN_LOOP) {
				loop_param = 128 | s->effect_param;
				s->effect_type = 0;
				s->effect_param = 0;
			}
			++s;
		}
		if(loop_param == 128) {
			/* SB0: just mark start of current loop */
			loops[n][0] = row;
		} else if(loop_param & 128) {
			/* SBy, y>0: mark end of current loop and start of next
			 loop */
			loops[n][1] = row;
			loops[n][2] = loop_param & 127;
			loops[++n][0] = row + 1;
		}
	}
	for(uint8_t i = 0; i < 64; ++i) {
		if(loops[i][2] == 0) continue;
		if(loops[i][0] == loops[i][1]) {
			/* Repeat a single row */
			s = slots + loops[i][0] * ctx->module.num_channels;
			uint8_t ch = 0;
			while(ch < ctx->module.num_channels) {
				if(s->effect_type == 0
				   && s->effect_param == 0) {
					s->effect_type = EFFECT_ROW_LOOP;
					s->effect_param = loops[i][2];
					break;
				}
				++s;
				++ch;
			}
			assert(ch < ctx->module.num_channels);
			continue;
		}

		/* Repeat multiple rows */
		assert(loops[i][0] < loops[i][1]);
		assert(loops[i][1] < 64);

		uint8_t ch = 0;
		while(ch < ctx->module.num_channels) {
			xm_pattern_slot_t* s_start = slots
				+ loops[i][0] * ctx->module.num_channels + ch;
			xm_pattern_slot_t* s_end = slots
				+ loops[i][1] * ctx->module.num_channels + ch;
			if(s_start->effect_type == 0
			   && s_start->effect_param == 0
			   && s_end->effect_type == 0
			   && s_end->effect_param == 0) {
				s_start->effect_type = EFFECT_PATTERN_LOOP;
				s_end->effect_type = EFFECT_PATTERN_LOOP;
				s_end->effect_param = loops[i][2];
				break;
			}
			++ch;
		}
		if(ch == ctx->module.num_channels) {
			NOTICE("inaccurate loop fixup in pattern %x"
			       " (no space left)", patidx);
		}
	}
}
