/* Author: Romain "Artefact2" Dalmaso <artefact2@gmail.com> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#include "xm_internal.h"

const uint16_t XM_ANALYZE_OUTPUT_SIZE = 22 + 41 + 36 + 31 + 31 + 1;

/* ----- Static functions ----- */

static void append_char(char*, uint16_t*, char);
static void append_str(char* restrict, uint16_t*, const char* restrict);
static void append_u16(char*, uint16_t*, uint16_t);
static void append_u64(char*, uint16_t*, uint64_t);

/* ----- Function definitions ----- */

static void append_char(char* dest, uint16_t* dest_offset, char x) {
	if(*dest_offset >= XM_ANALYZE_OUTPUT_SIZE) return;
	dest[*dest_offset] = x;
	*dest_offset += 1;
}

static void append_str(char* restrict dest, uint16_t* dest_offset,
                   const char* restrict s) {
	while(*s) {
		append_char(dest, dest_offset, *s++);
	}
}

static void append_u16(char* dest, uint16_t* dest_offset, uint16_t x) {
	static const char* digits = "0123456789ABCDEF";
	append_char(dest, dest_offset, digits[(x >> 12) & 0xF]);
	append_char(dest, dest_offset, digits[(x >> 8) & 0xF]);
	append_char(dest, dest_offset, digits[(x >> 4) & 0xF]);
	append_char(dest, dest_offset, digits[x & 0xF]);
}

static void append_u64(char* dest, uint16_t* dest_offset, uint64_t x) {
	append_u16(dest, dest_offset, (uint16_t)((x >> 48) & 0xFFFF));
	append_u16(dest, dest_offset, (uint16_t)((x >> 32) & 0xFFFF));
	append_u16(dest, dest_offset, (uint16_t)((x >> 16) & 0xFFFF));
	append_u16(dest, dest_offset, (uint16_t)(x & 0xFFFF));
}

void xm_analyze(const xm_context_t* ctx, char* out) {
	uint16_t off = 0;

	append_str(out, &off, "-DXM_FREQUENCY_TYPES=");
	append_str(out, &off, AMIGA_FREQUENCIES(&ctx->module) ? "2" : "1");

	uint64_t used_effects = 0;
	uint16_t used_volume_effects = 0;
	uint16_t used_waveforms = 0;
	uint16_t used_envelopes = 0;

	const xm_pattern_slot_t* slot = ctx->pattern_slots;
	for(uint32_t i = ctx->module.num_rows * ctx->module.num_channels;
	    i; --i, ++slot) {
		assert(slot->effect_type < 64); /* XXX */

		static_assert(EFFECT_ARPEGGIO == 0);
		if(slot->effect_type == 0) {
			/* Do not count "000" as an arpeggio */
			if(slot->effect_param) {
				used_effects |= 1;
			}
		} else {
			used_effects |= (uint64_t)1 << slot->effect_type;
		}

		used_volume_effects |= (uint16_t)1 << (VOLUME_COLUMN(slot) >> 4);

		if(slot->effect_type == EFFECT_SET_VIBRATO_CONTROL
		   || slot->effect_type == EFFECT_SET_TREMOLO_CONTROL) {
			used_waveforms |=
				(uint16_t)1 << (slot->effect_param & 3);
		}
	}

	for(uint8_t i = 0; i < ctx->module.num_instruments; ++i) {
		xm_instrument_t* inst = ctx->instruments + i;

		if(inst->volume_envelope.num_points) {
			used_envelopes |= 1;
		}

		if(inst->panning_envelope.num_points) {
			used_envelopes |= 2;
		}

		if(inst->volume_fadeout) {
			used_envelopes |= 4;
		}

		if(inst->vibrato_depth) {
			if(inst->vibrato_rate > 0
			   || inst->vibrato_type == WAVEFORM_SQUARE) {
				used_envelopes |= 8;
				used_waveforms |=
					(uint16_t)1 << inst->vibrato_type;
			}
		}
	}

	append_str(out, &off, " -DXM_DISABLED_EFFECTS=0x");
	append_u64(out, &off, (uint64_t)(~used_effects));

	append_str(out, &off, " -DXM_DISABLED_VOLUME_EFFECTS=0x");
	append_u16(out, &off, (uint16_t)(~used_volume_effects));

	append_str(out, &off, " -DXM_DISABLED_ENVELOPES=0x");
	append_u16(out, &off, (uint16_t)(~used_envelopes));

	append_str(out, &off, " -DXM_DISABLED_WAVEFORMS=0x");
	append_u16(out, &off, (uint16_t)(~used_waveforms));

	if(off < XM_ANALYZE_OUTPUT_SIZE) {
		out[off] = '\0';
	} else {
		out[XM_ANALYZE_OUTPUT_SIZE-1] = '\0';
	}
}
