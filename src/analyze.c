/* Author: Romain "Artefact2" Dalmaso <artefact2@gmail.com> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#include "xm_internal.h"

const uint16_t XM_ANALYZE_OUTPUT_SIZE =
	41 /* disabled effects */
	+ 36 /* disabled volume effects */
	+ 31 /* disabled waveforms */
	+ 30 /* disabled features */
	+ 1; /* terminating NUL */

/* ----- Static functions ----- */

static void append_char(char*, uint16_t*, char);
static void append_str(char* restrict, uint16_t*, const char* restrict);
static void append_u16(char*, uint16_t*, uint16_t);
static void append_u64(char*, uint16_t*, uint64_t);

static void scan_effects(const xm_context_t*, uint64_t*, uint16_t*);
static void scan_control_waveforms(const xm_context_t*, uint16_t*);
static void scan_features(const xm_context_t*, uint16_t*, uint16_t*);

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

static void scan_effects(const xm_context_t* ctx, uint64_t* out_effects,
                             uint16_t* out_volume_effects) {
	*out_effects = 0;
	*out_volume_effects = 0;
	const xm_pattern_slot_t* slot = ctx->pattern_slots;

	for(uint32_t i = ctx->module.num_rows * ctx->module.num_channels;
	    i; --i, ++slot) {
		assert(slot->effect_type < 64); /* XXX */

		static_assert(EFFECT_ARPEGGIO == 0);
		if(slot->effect_type == 0) {
			/* Do not count "000" as an arpeggio */
			if(slot->effect_param) {
				*out_effects |= 1;
			}
		} else {
			*out_effects |= (uint64_t)1 << slot->effect_type;
		}

		*out_volume_effects |= (uint16_t)1 << (VOLUME_COLUMN(slot) >> 4);
	}
}

static void scan_control_waveforms(const xm_context_t* ctx, uint16_t* out) {
	*out = 0;
	bool has_jumps = false;

	for(uint8_t c = 0; c < ctx->module.num_channels; ++c) {
		uint8_t vibrato_control_param = 0;
		uint8_t tremolo_control_param = 0;

		for(uint16_t i = 0; i < ctx->module.length; ++i) {
			const xm_pattern_t* pat =
				ctx->patterns + ctx->module.pattern_table[i];

			for(uint32_t row = 0; row < pat->num_rows; ++row) {
				const xm_pattern_slot_t* slot =
					ctx->pattern_slots
					+ (pat->rows_index
					   * ctx->module.num_channels)
					+ c;

				if(slot->effect_type == EFFECT_JUMP_TO_ORDER
				   || slot->effect_type == EFFECT_PATTERN_BREAK
				   || slot->effect_type == EFFECT_PATTERN_LOOP) {
					has_jumps = true;
				} else if(slot->effect_type
				   == EFFECT_SET_VIBRATO_CONTROL) {
					vibrato_control_param =
						slot->effect_param;
				} else if(slot->effect_type
				   == EFFECT_SET_TREMOLO_CONTROL) {
					tremolo_control_param =
						slot->effect_param;
				} else if(slot->effect_type == EFFECT_TREMOLO) {
					*out |= (uint16_t)1
						<< (tremolo_control_param & 3);
				}

				if(slot->effect_type == EFFECT_VIBRATO
				   || (slot->effect_type
				       == EFFECT_VIBRATO_VOLUME_SLIDE)
				   || (VOLUME_COLUMN(slot) >> 4
				       == VOLUME_EFFECT_VIBRATO)) {
					*out |= (uint16_t)1
						<< (vibrato_control_param & 3);
				}
			}
		}
	}

	if(has_jumps) {
		/* XXX: cannot guarantee ordering of vibrato control/vibrato
		   anymore, assume sine is always used */
		*out |= 1;
	}
}

static void scan_features(const xm_context_t* ctx, uint16_t* out,
                          uint16_t* out_autovibrato_waveforms) {
	*out = AMIGA_FREQUENCIES(&ctx->module) ? (1 << 9) : (1 << 8);
	*out_autovibrato_waveforms = 0;

	const xm_sample_t* smp = ctx->samples;
	for(uint16_t i = ctx->module.num_samples; i; --i, ++smp) {
		if(smp->ping_pong) {
			*out |= 1;
		}
	}

	const xm_pattern_slot_t* slot = ctx->pattern_slots;
	for(uint32_t i = ctx->module.num_rows * ctx->module.num_channels;
	    i; --i, ++slot) {
		if(slot->note == NOTE_KEY_OFF) {
			*out |= 2;
		} else if(slot->note == NOTE_SWITCH) {
			*out |= 4;
		}
	}

	const xm_instrument_t* inst = ctx->instruments;
	for(uint8_t i = ctx->module.num_instruments; i; --i, ++inst) {
		if(inst->volume_envelope.num_points) {
			*out |= 16;
		}

		if(inst->panning_envelope.num_points) {
			*out |= 32;
		}

		if(inst->volume_fadeout) {
			*out |= 64;
		}

		if(inst->vibrato_depth
		   && (inst->vibrato_rate > 0
		       || inst->vibrato_type == WAVEFORM_SQUARE)) {
			/* A zero vibrato_rate effectively turns off
			   autovibrato, except for square waveforms */
			*out |= 128;
			*out_autovibrato_waveforms |=
				(uint16_t)1 << inst->vibrato_type;
		}
	}
}

void xm_analyze(const xm_context_t* ctx, char* out) {
	uint16_t off = 0;

	#if XM_DISABLED_FEATURES > 0
	NOTICE("suggested flags might be inaccurate; "
	       "recompile libxmize with XM_DISABLED_FEATURES=0");
	#endif

	uint64_t used_effects;
	uint16_t used_volume_effects;
	scan_effects(ctx, &used_effects, &used_volume_effects);
	append_str(out, &off, " -DXM_DISABLED_EFFECTS=0x");
	append_u64(out, &off, (uint64_t)(~used_effects));
	append_str(out, &off, " -DXM_DISABLED_VOLUME_EFFECTS=0x");
	append_u16(out, &off, (uint16_t)(~used_volume_effects));

	uint16_t used_features;
	uint16_t used_autovibrato_waveforms;
	uint16_t used_control_waveforms;
	scan_features(ctx, &used_features, &used_autovibrato_waveforms);
	scan_control_waveforms(ctx, &used_control_waveforms);
	append_str(out, &off, " -DXM_DISABLED_WAVEFORMS=0x");
	append_u16(out, &off, (uint16_t)(~(used_autovibrato_waveforms
	                                   | used_control_waveforms)));
	append_str(out, &off, " -DXM_DISABLED_FEATURES=0x");
	append_u16(out, &off, (uint16_t)(~used_features));

	if(off < XM_ANALYZE_OUTPUT_SIZE) {
		out[off] = '\0';
	} else {
		out[XM_ANALYZE_OUTPUT_SIZE-1] = '\0';
	}
}
