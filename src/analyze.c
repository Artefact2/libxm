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
	+ 42 /* disabled features */
	+ 20 /* panning type */
	+ 1; /* terminating NUL */

/* ----- Static functions ----- */

static void append_char(char*, uint16_t*, char);
static void append_str(char* restrict, uint16_t*, const char* restrict);
static void append_u16(char*, uint16_t*, uint16_t);
static void append_u64(char*, uint16_t*, uint64_t);

static void analyze_note_trigger(xm_context_t*, xm_channel_context_t*, uint64_t*);

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

static void analyze_note_trigger(xm_context_t* ctx, xm_channel_context_t* ch,
                                 uint64_t* used_features) {
	if(ch->current->note == 0 || ch->current->note == NOTE_KEY_OFF) {
		return;
	}

	if(ch->next_instrument == 0
	   || ch->next_instrument > NUM_INSTRUMENTS(&ctx->module)) {
		*used_features |= (uint64_t)1 << FEATURE_INVALID_INSTRUMENTS;
		return;
	}

	xm_sample_t* smp;

	#if HAS_FEATURE(FEATURE_MULTISAMPLE_INSTRUMENTS)
	xm_instrument_t* inst = ctx->instruments + ch->next_instrument - 1;

	if(inst->sample_of_notes[ch->orig_note - 1] >= inst->num_samples) {
		*used_features |= (uint64_t)1 << FEATURE_INVALID_SAMPLES;
		return;
	}
	smp = ctx->samples + inst->samples_index
		+ inst->sample_of_notes[ch->orig_note - 1];

	if(inst->sample_of_notes[ch->orig_note - 1]) {
		*used_features |= (uint64_t)1 << FEATURE_MULTISAMPLE_INSTRUMENTS;
	}

	#else
	smp = ctx->samples + ch->next_instrument - 1;
	#endif

	int16_t note = (int16_t)(ch->orig_note + smp->relative_note);

	if(note <= 0 || note >= 120) {
		*used_features |= (uint64_t)1 << FEATURE_INVALID_NOTES;
	}
}

void xm_analyze(xm_context_t* restrict ctx, char* restrict out) {
	#if XM_DISABLED_FEATURES > 0 || XM_DISABLED_EFFECTS > 0 \
		|| XM_LOOPING_TYPE != 2
	NOTICE("suggested flags will be inaccurate; recompile libxm with"
	       " -DXM_DISABLED_FEATURES=0 -DXM_DISABLED_EFFECTS=0"
	       " -DXM_LOOPING_TYPE=2 to suppress this warning");
	#endif

	uint64_t used_features = AMIGA_FREQUENCIES(&ctx->module)
		? ((uint64_t)1 << FEATURE_AMIGA_FREQUENCIES)
		: ((uint64_t)1 << FEATURE_LINEAR_FREQUENCIES);
	uint64_t used_effects = 0;
	uint16_t used_volume_effects = 0;
	uint16_t off = 0;
	int16_t pannings[4] = { -1, -1, -1, -1 };
	uint8_t panning_type = 0;

	while(XM_LOOPING_TYPE != 0 && LOOP_COUNT(ctx) == 0) {
		xm_tick(ctx);

		xm_channel_context_t* ch = ctx->channels;
		for(uint8_t i = 0; i < ctx->module.num_channels; ++i, ++ch) {
			assert(ch->current->effect_type < 64); /* XXX */
			static_assert(EFFECT_ARPEGGIO == 0);
			if(ch->current->effect_type == 0) {
				/* Do not count "000" as an arpeggio */
				if(ch->current->effect_param) {
					used_effects |= 1;
				}
			} else {
				used_effects |=
					(uint64_t)1 << ch->current->effect_type;
			}
			used_volume_effects |=
				(uint16_t)1 << (VOLUME_COLUMN(ch->current) >> 4);

			analyze_note_trigger(ctx, ch, &used_features);

			if(ch->actual_volume[0] == 0.f
			   && ch->actual_volume[1] == 0.f) {
				continue;
			}

			if(ch->sample == NULL || ch->period == 0) {
				continue;
			}

			if(ch->current->effect_type == EFFECT_ARPEGGIO
			   && ch->current->effect_param > 0
			   && CURRENT_TEMPO(ctx) >= 16) {
				used_features |= (uint64_t)1
					<< FEATURE_ACCURATE_ARPEGGIO_OVERFLOW;
			}

			#if HAS_GLISSANDO_CONTROL
			if(ch->current->effect_type == EFFECT_ARPEGGIO
			   && ch->current->effect_param > 0
			   && ch->glissando_control_error != 0) {
				used_features |= (uint64_t)1
					<< FEATURE_ACCURATE_ARPEGGIO_GLISSANDO;
			}
			#endif

			#if HAS_VIBRATO
			if((ch->current->effect_type == EFFECT_VIBRATO
			    || ch->current->effect_type
			           == EFFECT_VIBRATO_VOLUME_SLIDE
			    || VOLUME_COLUMN(ch->current) >> 4
			           == VOLUME_EFFECT_VIBRATO)
			   && ch->vibrato_param & 0xF) {
				used_features |= (uint64_t)1
					<< (12|(VIBRATO_CONTROL_PARAM(ch) & 3));
			}
			#endif

			#if HAS_EFFECT(EFFECT_TREMOLO)
			if(ch->current->effect_type == EFFECT_TREMOLO
			   && ch->tremolo_param & 0xF) {
				used_features |= (uint64_t)1
					<< (12|(TREMOLO_CONTROL_PARAM(ch) & 3));
			}
			#endif

			if(PING_PONG(ch->sample)) {
				used_features |= (uint64_t)1
					<< FEATURE_PINGPONG_LOOPS;
			}

			if(ch->period == 1) {
				used_features |= (uint64_t)1
					<< FEATURE_CLAMP_PERIODS;
			}

			if(ch->current->note == NOTE_KEY_OFF) {
				used_features |= (uint64_t)1
					<< FEATURE_NOTE_KEY_OFF;
			} else if(ch->current->note == NOTE_SWITCH) {
				used_features |= (uint64_t)1
					<< FEATURE_NOTE_SWITCH;
			}

			#if HAS_FEATURE(FEATURE_VOLUME_ENVELOPES)
			if(ch->instrument->volume_envelope.num_points) {
				used_features |= (uint64_t)1
					<< FEATURE_VOLUME_ENVELOPES;
			}
			#endif

			#if HAS_FEATURE(FEATURE_PANNING_ENVELOPES)
			if(ch->instrument->panning_envelope.num_points) {
				used_features |= (uint64_t)1
					<< FEATURE_PANNING_ENVELOPES;
			}
			#endif

			#if HAS_FADEOUT_VOLUME
			if(ch->instrument->volume_fadeout && !SUSTAINED(ch)) {
				used_features |= (uint64_t)1
					<< FEATURE_FADEOUT_VOLUME;
			}
			#endif

			#if HAS_FEATURE(FEATURE_AUTOVIBRATO)
			if(ch->instrument->vibrato_depth
			   && (ch->instrument->vibrato_rate > 0
			       || ch->instrument->vibrato_type
			               == WAVEFORM_SQUARE)) {
				/* A zero vibrato_rate effectively turns off
				   autovibrato, except for square waveforms */
				used_features |= (uint64_t)1
					<< FEATURE_AUTOVIBRATO;
				used_features |= (uint64_t)1
					<< (12|ch->instrument->vibrato_type);
			}
			#endif

			int16_t panning = (int16_t)
				(xm_get_panning_of_channel(ctx, i+1)
				 * UINT8_MAX);
			if(pannings[i % 4] == -1) {
				pannings[i % 4] = panning;
			} else if(pannings[i % 4] != panning) {
				panning_type = 8;
			}
		}
	}

	if(panning_type == 0
	   && pannings[0] == pannings[3]
	   && pannings[1] == pannings[2]) {
		if(pannings[0] <= pannings[1]) {
			panning_type = (uint8_t)
				(8 * (pannings[1] - pannings[0]) / 256);
			assert(panning_type < 8);
		} else {
			panning_type = 8;
		}
	}

	append_str(out, &off, " -DXM_DISABLED_EFFECTS=0x");
	append_u64(out, &off, (uint64_t)(~used_effects));
	append_str(out, &off, " -DXM_DISABLED_VOLUME_EFFECTS=0x");
	append_u16(out, &off, (uint16_t)(~used_volume_effects));
	append_str(out, &off, " -DXM_DISABLED_FEATURES=0x");
	append_u64(out, &off, (uint64_t)(~used_features));
	append_str(out, &off, " -DXM_PANNING_TYPE=");
	append_char(out, &off, (char)('0' + panning_type));

	if(off < XM_ANALYZE_OUTPUT_SIZE) {
		out[off] = '\0';
	} else {
		out[XM_ANALYZE_OUTPUT_SIZE-1] = '\0';
	}
}
