/* Author: Romain "Artefact2" Dalmaso <artefact2@gmail.com> */
/* Contributor: Daniel Oaks <daniel@danieloaks.net> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#include "xm_internal.h"

/* ----- Static functions ----- */

static int8_t xm_waveform(uint8_t, uint8_t) __attribute__((warn_unused_result));
static void xm_autovibrato(xm_channel_context_t*) __attribute__((nonnull));
static void xm_vibrato(xm_channel_context_t*) __attribute__((nonnull));
static void xm_tremolo(xm_channel_context_t*) __attribute__((nonnull));
static void xm_multi_retrig_note(xm_context_t*, xm_channel_context_t*) __attribute__((nonnull));
static void xm_arpeggio(xm_context_t*, xm_channel_context_t*) __attribute__((nonnull));
static void xm_tone_portamento(xm_channel_context_t*) __attribute__((nonnull));
static void xm_pitch_slide(xm_channel_context_t*, int16_t) __attribute__((nonnull));
static void xm_param_slide(uint8_t*, uint8_t, uint8_t) __attribute__((nonnull));
static void xm_tick_effects(xm_context_t*, xm_channel_context_t*) __attribute__((nonnull));

static uint8_t xm_envelope_lerp(const xm_envelope_point_t*, const xm_envelope_point_t*, uint16_t) __attribute__((warn_unused_result)) __attribute__((nonnull));
static void xm_tick_envelope(xm_channel_context_t*, const xm_envelope_t*, uint16_t*, uint8_t*) __attribute__((nonnull));
static void xm_tick_envelopes(xm_channel_context_t*) __attribute__((nonnull));

static uint16_t xm_linear_period(int16_t) __attribute__((warn_unused_result));
static uint32_t xm_linear_frequency(xm_channel_context_t*) __attribute__((warn_unused_result)) __attribute__((nonnull));
static uint16_t xm_amiga_period(int16_t) __attribute__((warn_unused_result));
static uint32_t xm_amiga_frequency(xm_channel_context_t*) __attribute__((warn_unused_result)) __attribute__((nonnull));

static uint16_t xm_period(xm_context_t*, int16_t) __attribute__((warn_unused_result)) __attribute__((nonnull));
static uint32_t xm_frequency(xm_context_t*, xm_channel_context_t*) __attribute__((warn_unused_result)) __attribute__((nonnull));

static void xm_handle_pattern_slot(xm_context_t*, xm_channel_context_t*) __attribute__((nonnull));
static void xm_trigger_instrument(xm_context_t*, xm_channel_context_t*) __attribute__((nonnull));
static void xm_trigger_note(xm_context_t*, xm_channel_context_t*) __attribute__((nonnull));
static void xm_cut_note(xm_channel_context_t*) __attribute__((nonnull));
static void xm_key_off(xm_context_t*, xm_channel_context_t*) __attribute__((nonnull));

static void xm_post_pattern_change(xm_context_t*) __attribute__((nonnull));
static void xm_row(xm_context_t*) __attribute__((nonnull));
static void xm_tick(xm_context_t*) __attribute__((nonnull));

static float xm_sample_at(const xm_context_t*, const xm_sample_t*, uint32_t) __attribute__((warn_unused_result)) __attribute__((nonnull));
static float xm_next_of_sample(xm_context_t*, xm_channel_context_t*) __attribute__((warn_unused_result)) __attribute__((nonnull));
static void xm_next_of_channel(xm_context_t*, xm_channel_context_t*, float*, float*) __attribute__((nonnull));
static void xm_sample_unmixed(xm_context_t*, float*) __attribute__((nonnull));
static void xm_sample(xm_context_t*, float*, float*) __attribute__((nonnull));

/* ----- Other oddities ----- */

#define XM_CLAMP_UP1F(vol, limit) do {                                  \
		if((vol) > (limit)) (vol) = (limit); \
	} while(0)
#define XM_CLAMP_UP(vol) XM_CLAMP_UP1F((vol), 1.f)

#define XM_CLAMP_DOWN1F(vol, limit) do {                                \
		if((vol) < (limit)) (vol) = (limit); \
	} while(0)
#define XM_CLAMP_DOWN(vol) XM_CLAMP_DOWN1F((vol), .0f)

#define XM_CLAMP2F(vol, up, down) do {                                  \
		if((vol) > (up)) (vol) = (up); \
		else if((vol) < (down)) (vol) = (down); \
	} while(0)
#define XM_CLAMP(vol) XM_CLAMP2F((vol), 1.f, .0f)

#define XM_LERP(u, v, t) ((u) + (t) * ((v) - (u)))

[[maybe_unused]] static void XM_SLIDE_TOWARDS(float* val,
                                              float goal, float incr) {
	if(*val > goal) {
		*val -= incr;
		XM_CLAMP_DOWN1F(*val, goal);
	} else {
		*val += incr;
		XM_CLAMP_UP1F(*val, goal);
	}
}

static bool HAS_TONE_PORTAMENTO(const xm_pattern_slot_t* s) {
	return s->effect_type == 3 || s->effect_type == 5
		|| s->volume_column >> 4 == 0xF;
}

static bool HAS_VIBRATO(const xm_pattern_slot_t* s) {
	return s->effect_type == 4 || s->effect_type == 6
		|| (s->volume_column >> 4) == 0xB;
}

static bool NOTE_IS_VALID(uint8_t n) {
	return n & ~KEY_OFF_NOTE;
}

static bool NOTE_IS_KEY_OFF(uint8_t n) {
	return n & KEY_OFF_NOTE;
}

/* ----- Function definitions ----- */

static int8_t xm_waveform(uint8_t waveform, uint8_t step) {
	static uint32_t next_rand = 24492;
	static const int8_t sin_lut[] = {
		/* 128*sinf(2πx/64) for x in 0..16 */
		0, 12, 24, 37, 48, 60, 71, 81,
		90, 98, 106, 112, 118, 122, 125, 127,
	};

	step %= 0x40;

	switch(waveform & 3) {

	case 2: /* Square */
		return (step < 0x20) ? INT8_MIN : INT8_MAX;

	case 0: /* Sine */
		uint8_t idx = step & 0x10 ? 0xF - (step & 0xF) : (step & 0xF);
		return (step < 0x20) ? -sin_lut[idx] : sin_lut[idx];

	case 1: /* Ramp down */
		return INT8_MAX - step * 4;

	case 3: /* Random */
		/* Use the POSIX.1-2001 example, just to be deterministic
		 * across different machines */
		next_rand = next_rand * 1103515245 + 12345;
		return (int8_t)((next_rand >> 16) & 0xFF);

	}

	UNREACHABLE();
}

static void xm_autovibrato(xm_channel_context_t* ch) {
	xm_instrument_t* instr = ch->instrument;
	if(instr == NULL) return;

	/* Autovibrato, unlike 4xx vibrato, only bends pitch *down*, not down
	   and up. Its full range at depth F seems to be about the same as
	   E24 (=4/16=1/4 semitone). */

	ch->autovibrato_note_offset =
		(xm_waveform(instr->vibrato_type,
		             instr->vibrato_rate * ch->autovibrato_ticks >> 2)
		 - 128) * instr->vibrato_depth / 256;

	if(ch->autovibrato_ticks < instr->vibrato_sweep) {
		ch->autovibrato_note_offset = ch->autovibrato_note_offset
			* ch->autovibrato_ticks / instr->vibrato_sweep;
	}

	ch->autovibrato_ticks++;
}

static void xm_vibrato(xm_channel_context_t* ch) {
	/* Depth 8 == 2 semitones amplitude (-1 then +1) */
	ch->vibrato_offset =
		xm_waveform(ch->vibrato_control_param, ch->vibrato_ticks)
		* (ch->vibrato_param & 0x0F) / 0x10;
	ch->vibrato_ticks += (ch->vibrato_param >> 4);
}

static void xm_tremolo(xm_channel_context_t* ch) {
	/* Additive volume effect based on a waveform. Depth 8 is plus or minus
	   32 volume. Works in the opposite direction of vibrato (ie, ramp down
	   means pitch goes down with vibrato, but volume goes up with
	   tremolo.). Tremolo, like vibrato, is not applied on 1st tick of every
	   row (has no effect on Spd=1). */
	/* Like Txy: Tremor, tremolo effect *persists* after the end of the
	   effect, but is reset after any volume command. */
	ch->volume_offset =
		-xm_waveform(ch->tremolo_control_param, ch->tremolo_ticks)
		* (ch->tremolo_param & 0x0F) * 4 / 128;
	ch->tremolo_ticks += (ch->tremolo_param >> 4);
}

static void xm_multi_retrig_note(xm_context_t* ctx, xm_channel_context_t* ch) {
	static const uint8_t add[] = {
		0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 4, 8, 16, 0, 0
	};
	static const uint8_t sub[] = {
		0, 1, 2, 4, 8, 16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	};
	static const uint8_t mul[] = {
		1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 3, 2
	};
	static const uint8_t div[] = {
		1, 1, 1, 1, 1, 1, 3, 2, 1, 1, 1, 1, 1, 1, 2, 1
	};

	uint8_t y = ch->multi_retrig_param & 0x0F;
	if(y == 0 || ctx->current_tick % y) return;

	xm_trigger_instrument(ctx, ch);

	/* Rxy doesn't affect volume if there's a command in the volume
	   column, or if the instrument has a volume envelope. */
	if(ch->current->volume_column
	   || ch->instrument->volume_envelope.num_points <= MAX_ENVELOPE_POINTS)
		return;

	static_assert(MAX_VOLUME <= (UINT8_MAX / 3));
	uint8_t x = ch->multi_retrig_param >> 4;
	if(ch->volume < sub[x]) ch->volume = sub[x];
	ch->volume = ((ch->volume - sub[x] + add[x]) * mul[x]) / div[x];
	if(ch->volume > MAX_VOLUME) ch->volume = MAX_VOLUME;
}

static void xm_arpeggio(xm_context_t* ctx, xm_channel_context_t* ch) {
	/* Arp effect always resets vibrato offset, even if it only runs for 1
	   tick where the offset is 0 (eg spd=2 001). Tick counter isn't
	   reset. */
	ch->vibrato_offset = 0;

	uint8_t offset = ctx->tempo % 3;
	switch(offset) {
	case 2: /* 0 -> x -> 0 -> y -> x -> … */
		if(ctx->current_tick == 1) {
			ch->arp_note_offset = ch->current->effect_param >> 4;
			return;
		}
		[[fallthrough]];
	case 1: /* 0 -> 0 -> y -> x -> … */
		if(ctx->current_tick == 0) {
			ch->arp_note_offset = 0;
			return;
		}
		break;
	}

	switch((ctx->current_tick - offset) % 3) {
	case 0:
		ch->arp_note_offset = 0;
		break;
	case 1:
		ch->arp_note_offset = ch->current->effect_param & 0x0F;
		break;
	case 2:
		ch->arp_note_offset = ch->current->effect_param >> 4;
		break;
	}
}

static void xm_tone_portamento(xm_channel_context_t* ch) {
	/* 3xx called without a note, wait until we get an actual
	 * target note. */
	if(ch->tone_portamento_target_period == 0) return;

	/* Already done, don't bother updating frequency */
	if(ch->period == ch->tone_portamento_target_period) return;

	uint16_t incr = 4 * ch->tone_portamento_param;
	if(ch->period > ch->tone_portamento_target_period) {
		if(ch->period <= incr) {
			ch->period = 0;
		} else {
			ch->period -= incr;
		}
		if(ch->period < ch->tone_portamento_target_period) {
			ch->period = ch->tone_portamento_target_period;
		}
	}
	if(ch->period < ch->tone_portamento_target_period) {
		ch->period += incr;
		if(ch->period > ch->tone_portamento_target_period) {
			ch->period = ch->tone_portamento_target_period;
		}
	}
}

static void xm_pitch_slide(xm_channel_context_t* ch,
                           int16_t period_offset) {
	if(ch->period < -period_offset) {
		ch->period = 0;
	} else {
		ch->period += period_offset;
	}
	/* XXX: upper bound of period ? */
}

static void xm_param_slide(uint8_t* param, uint8_t rawval, uint8_t max) {
	if(rawval & 0xF0) {
		/* Slide up */
		if(ckd_add(param, *param, rawval >> 4) || *param > max) {
			*param = max;
		}
	} else {
		/* Slide down */
		if(ckd_sub(param, *param, rawval)) {
			*param = 0;
		}
	}
}

static uint8_t xm_envelope_lerp(const xm_envelope_point_t* restrict a,
                                const xm_envelope_point_t* restrict b,
                                uint16_t pos) {
	/* Linear interpolation between two envelope points */
	assert(pos >= a->frame);
	assert(a->frame < b->frame);
	static_assert(MAX_ENVELOPE_VALUE <= UINT8_MAX);
	if(pos >= b->frame) return b->value;
	return (b->value * (pos - a->frame) + a->value * (b->frame - pos))
		/ (b->frame - a->frame);
}

static void xm_post_pattern_change(xm_context_t* ctx) {
	/* Loop if necessary */
	if(ctx->current_table_index >= ctx->module.length) {
		ctx->current_table_index = ctx->module.restart_position;
	}
}

[[maybe_unused]] static uint16_t xm_linear_period(int16_t note) {
	assert(7680 - note * 4 > 0);
	assert(7860 - note * 4 < UINT16_MAX);
	return (uint16_t)(7680 - note * 4);
}

[[maybe_unused]] static uint32_t xm_linear_frequency(xm_channel_context_t* ch) {
	uint16_t p = ch->period;
	p -= ch->arp_note_offset * 64;
	p -= ch->vibrato_offset;
	p -= ch->autovibrato_note_offset;
	return (uint32_t)(8363.f * exp2f((4608.f - (float)p) / 768.f));
}

[[maybe_unused]] static uint16_t xm_amiga_period(int16_t note) {
	/* Values obtained via exponential regression over the period tables in
	   modfil10.txt */
	return (uint16_t)
		(32.f * 855.9563438f * exp2f(-0.0832493329f * note / 16.f));
}

[[maybe_unused]] static uint32_t xm_amiga_frequency(xm_channel_context_t* ch) {
	if(ch->period == 0) return 0;
	float p = (float)ch->period
		* exp2f(-0.0832493329f * ((float)ch->arp_note_offset + (float)ch->autovibrato_note_offset / 64.f));
	p -= (float)ch->vibrato_offset;

	/* This is the PAL value. No reason to choose this one over the
	 * NTSC value. */
	return (uint32_t)(4.f * 7093789.2f / (p * 2.f));
}

static uint16_t xm_period([[maybe_unused]] xm_context_t* ctx, int16_t note) {
	#if XM_FREQUENCY_TYPES == 1
	return xm_linear_period(note);
	#elif XM_FREQUENCY_TYPES == 2
	return xm_amiga_period(note);
	#else
	switch(ctx->module.frequency_type) {
	case XM_LINEAR_FREQUENCIES:
		return xm_linear_period(note);
	case XM_AMIGA_FREQUENCIES:
		return xm_amiga_period(note);
	}
	UNREACHABLE();
	#endif
}
static uint32_t xm_frequency([[maybe_unused]] xm_context_t* ctx,
                             xm_channel_context_t* ch) {
	#if XM_FREQUENCY_TYPES == 1
	return xm_linear_frequency(ch);
	#elif XM_FREQUENCY_TYPES == 2
	return xm_amiga_frequency(ch);
	#else
	switch(ctx->module.frequency_type) {
	case XM_LINEAR_FREQUENCIES:
		return xm_linear_frequency(ch);
	case XM_AMIGA_FREQUENCIES:
		return xm_amiga_frequency(ch);
	}
	UNREACHABLE();
	#endif
}

static void xm_handle_pattern_slot(xm_context_t* ctx, xm_channel_context_t* ch) {
	xm_pattern_slot_t* s = ch->current;

	if(s->instrument) {
		/* Update ch->next_instrument */
		ch->next_instrument = s->instrument;
	}

	if(NOTE_IS_VALID(s->note)) {
		/* Non-zero note, also not key off. Assume note is valid, since
		   invalid notes are deleted in load.c. */

		/* Update ch->sample and ch->instrument */
		xm_instrument_t* next = ctx->instruments
			+ ch->next_instrument - 1;
		if(ch->next_instrument
		   && ch->next_instrument - 1 < ctx->module.num_instruments
		   && next->sample_of_notes[s->note - 1] < next->num_samples) {
			ch->instrument = next;
			ch->sample = ctx->samples
				+ ch->instrument->samples_index
				+ ch->instrument->sample_of_notes[s->note - 1];
		} else {
			ch->instrument = NULL;
			ch->sample = NULL;
			xm_cut_note(ch);
		}
	}

	if(s->instrument) {
		xm_trigger_instrument(ctx, ch);
	}

	if(NOTE_IS_KEY_OFF(s->note)) {
		/* Key Off */
		xm_key_off(ctx, ch);
	} else if(s->note && ch->sample) {
		int16_t note = (int16_t)(s->note + ch->sample->relative_note);
		if(note > 0 && note < 120) {
			/* Yes, the real note number is s->note -1. Try finding
			 * THAT in any of the specs! :-) */
			note = (int16_t)(16 * (note - 1));
			if(HAS_TONE_PORTAMENTO(ch->current)) {
				/* 3xx/Mx ignores E5y, but will reuse whatever
				   finetune was set when initially triggering
				   the note */
				ch->tone_portamento_target_period =
					xm_period(ctx, note + ch->finetune);
			} else {
				/* Handle E5y: Set note fine-tune here; this
				   effect only works in tandem with a note and
				   overrides the finetune value stored in the
				   sample. If we have Mx in the volume column,
				   it does nothing.*/
				ch->finetune = ch->sample->finetune;
				if(s->effect_type == 0x0E
				   && s->effect_param >> 4 == 0x05) {
					ch->finetune = (int8_t)
						((s->effect_param & 0xF)*2-16);
				}
				ch->orig_period =
					xm_period(ctx, note + ch->finetune);
				xm_trigger_note(ctx, ch);
			}
		}
	}

	/* These volume effects always work, even when called with a delay by
	   EDy. */
	switch(s->volume_column >> 4) {

	case 0x5:
		if(s->volume_column > 0x50) break;
		[[fallthrough]];
	case 0x1:
		[[fallthrough]];
	case 0x2:
		[[fallthrough]];
	case 0x3:
		[[fallthrough]];
	case 0x4:
		/* Set volume */
		ch->volume_offset = 0;
		ch->volume = s->volume_column - 0x10;
		break;

	case 0xC: /* Px: Set panning */
		ch->panning = (s->volume_column & 0x0F) * 0x11;
		break;

	}

	if(ctx->current_tick == 0) {
		/* These effects are ONLY applied at tick 0. If a note delay
		   effect (EDy), where y>0, uses this effect in its volume
		   column, it will be ignored. */

		switch(s->volume_column >> 4) {

		case 0x8: /* ▼x: Fine volume slide down */
			ch->volume_offset = 0;
			xm_param_slide(&ch->volume, s->volume_column & 0x0F,
			               MAX_VOLUME);
			break;

		case 0x9: /* ▲x: Fine volume slide up */
			ch->volume_offset = 0;
			xm_param_slide(&ch->volume, s->volume_column << 4,
			               MAX_VOLUME);
			break;

		case 0xA: /* Sx: Set vibrato speed */
			ch->vibrato_param = (ch->vibrato_param & 0x0F)
				| ((s->volume_column & 0x0F) << 4);
			break;

		}
	}

	switch(s->effect_type) {

	case 3: /* 3xx: Tone portamento */
		if(s->effect_param > 0) {
			/* XXX: test me */
			ch->tone_portamento_param = s->effect_param;
		}
		break;

	case 8: /* 8xx: Set panning */
		ch->panning = s->effect_param;
		break;

	case 9: /* 9xx: Sample offset */
		/* 9xx is ignored unless we have a note */
		if(ch->sample == NULL || !NOTE_IS_VALID(s->note))
			break;
		if(s->effect_param > 0) {
			ch->sample_offset_param = s->effect_param;
		}
		ch->sample_position = ch->sample_offset_param * 256;
		if(ch->sample_position >= ch->sample->length) {
			ch->sample = NULL;
		}
		static_assert(256 * SAMPLE_MICROSTEPS * UINT8_MAX <= UINT32_MAX);
		ch->sample_position *= SAMPLE_MICROSTEPS;
		break;

	case 0xB: /* Bxx: Position jump */
		if(s->effect_param < ctx->module.length) {
			ctx->position_jump = true;
			ctx->jump_dest = s->effect_param;
			ctx->jump_row = 0;
		}
		break;

	case 0xC: /* Cxx: Set volume */
		ch->volume_offset = 0;
		ch->volume = s->effect_param > MAX_VOLUME ?
			MAX_VOLUME : s->effect_param;
		break;

	case 0xD: /* Dxx: Pattern break */
		/* Jump after playing this line */
		ctx->pattern_break = true;
		ctx->jump_row = (uint8_t)
			(s->effect_param - 6 * (s->effect_param >> 4));
		break;

	case 0xE: /* EXy: Extended command */
		switch(s->effect_param >> 4) {

		case 1: /* E1y: Fine portamento up */
			if(s->effect_param & 0x0F) {
				ch->fine_portamento_up_param =
					4 * (s->effect_param & 0x0F);
			}
			xm_pitch_slide(ch, -ch->fine_portamento_up_param);
			break;

		case 2: /* E2y: Fine portamento down */
			if(s->effect_param & 0x0F) {
				ch->fine_portamento_down_param =
					4 * (s->effect_param & 0x0F);
			}
			xm_pitch_slide(ch, ch->fine_portamento_down_param);
			break;

		case 4: /* E4y: Set vibrato control */
			ch->vibrato_control_param = s->effect_param;
			break;

		/* E5y: Set note fine-tune is handled in
		   xm_handle_pattern_slot() directly. */

		case 6: /* E6y: Pattern loop */
			if(s->effect_param & 0x0F) {
				if((s->effect_param & 0x0F) == ch->pattern_loop_count) {
					/* Loop is over */
					ch->pattern_loop_count = 0;
					break;
				}

				/* Jump to the beginning of the loop */
				ch->pattern_loop_count++;
				ctx->position_jump = true;
				ctx->jump_row = ch->pattern_loop_origin;
				ctx->jump_dest = ctx->current_table_index;
			} else {
				/* Set loop start point */
				ch->pattern_loop_origin = ctx->current_row;
				/* Replicate FT2 E60 bug */
				ctx->jump_row = ch->pattern_loop_origin;
			}
			break;

		case 7: /* E7y: Set tremolo control */
			ch->tremolo_control_param = s->effect_param;
			break;

		case 0xA: /* EAy: Fine volume slide up */
			if(s->effect_param & 0x0F) {
				ch->fine_volume_slide_up_param =
					s->effect_param << 4;
			}
			ch->volume_offset = 0;
			xm_param_slide(&ch->volume,
			               ch->fine_volume_slide_up_param,
			               MAX_VOLUME);
			break;

		case 0xB: /* EBy: Fine volume slide down */
			if(s->effect_param & 0x0F) {
				ch->fine_volume_slide_down_param =
					s->effect_param & 0x0F;
			}
			ch->volume_offset = 0;
			xm_param_slide(&ch->volume,
			               ch->fine_volume_slide_down_param,
			               MAX_VOLUME);
			break;

		case 0xE: /* EEy: Pattern delay */
			ctx->extra_rows = (ch->current->effect_param & 0x0F);
			break;

		}
		break;

	case 0xF: /* Fxx: Set tempo/BPM */
		if(s->effect_param >= MIN_BPM) {
			ctx->bpm = s->effect_param;
		} else {
			ctx->tempo = s->effect_param;
		}
		break;

	case 16: /* Gxx: Set global volume */
		ctx->global_volume = (s->effect_param > MAX_VOLUME) ?
			MAX_VOLUME : s->effect_param;
		break;

	case 21: /* Lxx: Set envelope position */
		ch->volume_envelope_frame_count = s->effect_param;
		ch->panning_envelope_frame_count = s->effect_param;
		break;

	case 33: /* Xxy: Extra stuff */
		switch(s->effect_param >> 4) {

		case 1: /* X1y: Extra fine portamento up */
			if(s->effect_param & 0x0F) {
				ch->extra_fine_portamento_up_param =
					s->effect_param & 0x0F;
			}
			xm_pitch_slide(ch,
			               -ch->extra_fine_portamento_up_param);
			break;

		case 2: /* X2y: Extra fine portamento down */
			if(s->effect_param & 0x0F) {
				ch->extra_fine_portamento_down_param =
					s->effect_param & 0x0F;
			}
			xm_pitch_slide(ch,
			               ch->extra_fine_portamento_down_param);
			break;

		}
		break;

	}
}

static void xm_trigger_instrument([[maybe_unused]] xm_context_t* ctx,
                                  xm_channel_context_t* ch) {
	if(ch->sample == NULL) return;

	ch->volume = ch->sample->volume;
	ch->panning = ch->sample->panning;

	ch->sustained = true;
	ch->volume_envelope_frame_count = 0;
	ch->panning_envelope_frame_count = 0;
	ch->tremor_ticks = 0;
	ch->autovibrato_ticks = 0;
	ch->autovibrato_note_offset = 0;
	ch->volume_offset = 0;

	if(!(ch->vibrato_control_param & 4)) {
		ch->vibrato_ticks = 0;
	}
	if(!(ch->tremolo_control_param & 4)) {
		ch->tremolo_ticks = 0;
	}

	#if XM_TIMING_FUNCTIONS
	ch->latest_trigger = ctx->generated_samples;
	assert(ch->instrument != NULL);
	ch->instrument->latest_trigger = ctx->generated_samples;
	#endif
}

static void xm_trigger_note([[maybe_unused]] xm_context_t* ctx,
                            xm_channel_context_t* ch) {
	if(ch->sample == NULL) return;
	/* Can be called by eg, key off note with EDy */
	if(NOTE_IS_KEY_OFF(ch->current->note)) return;

	ch->period = ch->orig_period;
	ch->sample_position = 0;
	ch->vibrato_offset = 0;

	/* XXX: is this reset by a note trigger or inst trigger? does it matter
	   since tremor_on touches volume_offest anyway, and it gets reset by an
	   inst trigger?*/
	//ch->tremor_on = false;

	#if XM_TIMING_FUNCTIONS
	ch->latest_trigger = ctx->generated_samples;
	ch->sample->latest_trigger = ctx->generated_samples;
	#endif
}

static void xm_cut_note(xm_channel_context_t* ch) {
	/* NB: this is not the same as Key Off */
	ch->volume = 0;
}

static void xm_key_off(xm_context_t* ctx, xm_channel_context_t* ch) {
	/* Key Off */
	ch->sustained = false;

	/* XXX: An immediate key-off (note 97 or K00) doesn't actually cut the
	   note when also triggering an instrument. Find the proper logic around
	   triggers to avoid needing this ugly workaround in the first place. */
	if(ch->current->instrument > 0 && ctx->current_tick == 0) {
		return;
	}

	/* If no volume envelope is used, also cut the note */
	if(ch->instrument == NULL
	   || ch->instrument->volume_envelope.num_points > MAX_ENVELOPE_POINTS) {
		xm_cut_note(ch);
	}
}

static void xm_row(xm_context_t* ctx) {
	if(ctx->position_jump) {
		ctx->current_table_index = ctx->jump_dest;
		ctx->current_row = ctx->jump_row;
		ctx->position_jump = false;
		ctx->pattern_break = false;
		ctx->jump_row = 0;
		xm_post_pattern_change(ctx);
	} else if(ctx->pattern_break) {
		ctx->current_table_index++;
		ctx->current_row = ctx->jump_row;
		ctx->pattern_break = false;
		ctx->jump_row = 0;
		xm_post_pattern_change(ctx);
	}

	xm_pattern_t* cur = ctx->patterns
		+ ctx->module.pattern_table[ctx->current_table_index];
	xm_pattern_slot_t* s = ctx->pattern_slots + ctx->module.num_channels
		* (cur->rows_index + ctx->current_row);
	xm_channel_context_t* ch = ctx->channels;
	bool in_a_loop = false;

	/* Read notes… */
	for(uint8_t i = 0; i < ctx->module.num_channels; ++i, ++ch, ++s) {
		ch->current = s;

		if(s->effect_type != 0xE || s->effect_param >> 4 != 0xD) {
			/* No EDy note delay */
			xm_handle_pattern_slot(ctx, ch);
		} else {
			/* Call xm_handle_pattern_slot() later, in
			   xm_tick_effects() */
			ch->note_delay_param = s->effect_param & 0x0F;
		}

		if(ch->pattern_loop_count > 0) {
			in_a_loop = true;
		}

		if(ch->arp_note_offset) {
			ch->arp_note_offset = 0;
		}

		if(ch->should_reset_vibrato && !HAS_VIBRATO(ch->current)) {
			ch->should_reset_vibrato = false;
			ch->vibrato_offset = 0;
		}
	}

	if(!in_a_loop) {
		/* No E6y loop is in effect (or we are in the first pass) */
		ctx->loop_count = (ctx->row_loop_count[MAX_ROWS_PER_PATTERN * ctx->current_table_index + ctx->current_row]++);
	}

	ctx->current_row++; /* Since this is an uint8, this line can
	                     * increment from 255 to 0, in which case it
	                     * is still necessary to go the next
	                     * pattern. */
	if(!ctx->position_jump && !ctx->pattern_break &&
	   (ctx->current_row >= cur->num_rows || ctx->current_row == 0)) {
		ctx->current_table_index++;
		ctx->current_row = ctx->jump_row; /* This will be 0 most of
		                                   * the time, except when E60
		                                   * is used */
		ctx->jump_row = 0;
		xm_post_pattern_change(ctx);
	}
}

static void xm_tick_envelope(xm_channel_context_t* ch,
                             const xm_envelope_t* env,
                             uint16_t* restrict counter,
                             uint8_t* restrict outval) {
	/* Don't advance envelope position if we are sustaining */
	if(ch->sustained && env->sustain_point <= MAX_ENVELOPE_POINTS &&
	   *counter == env->points[env->sustain_point].frame) {
		*outval = env->points[env->sustain_point].value;
		return;
	}

	if(env->loop_start_point <= MAX_ENVELOPE_POINTS) {
		uint16_t loop_start = env->points[env->loop_start_point].frame;
		uint16_t loop_end = env->points[env->loop_end_point].frame;
		uint16_t loop_length = loop_end - loop_start;

		/* Don't loop if we moved beyond the end point, with eg a
		   Lxx effect */
		if(*counter == loop_end) {
			*counter -= loop_length;
		}
	}

	/* Find points left and right of current envelope position */
	assert(env->num_points >= 2);
	for(uint8_t j = env->num_points - 1; j > 0; --j) {
		if(*counter < env->points[j-1].frame) continue;
		*outval = xm_envelope_lerp(env->points + j - 1,
		                           env->points + j,
		                           *counter);
		(*counter)++;
		return;
	}

	UNREACHABLE();
}

static void xm_tick_envelopes(xm_channel_context_t* ch) {
	xm_instrument_t* inst = ch->instrument;
	if(inst == NULL) return;

	if(!ch->sustained) {
		ch->fadeout_volume =
			(ch->fadeout_volume < inst->volume_fadeout) ?
			0 : ch->fadeout_volume - inst->volume_fadeout;
	} else {
		ch->fadeout_volume = MAX_FADEOUT_VOLUME-1;
	}

	if(inst->volume_envelope.num_points <= MAX_ENVELOPE_POINTS) {
		xm_tick_envelope(ch, &(inst->volume_envelope),
		                 &(ch->volume_envelope_frame_count),
		                 &(ch->volume_envelope_volume));
	} else {
		ch->volume_envelope_volume = MAX_ENVELOPE_VALUE;
	}

	if(inst->panning_envelope.num_points <= MAX_ENVELOPE_POINTS) {
		xm_tick_envelope(ch, &(inst->panning_envelope),
		                 &(ch->panning_envelope_frame_count),
		                 &(ch->panning_envelope_panning));
	} else {
		ch->panning_envelope_panning = MAX_ENVELOPE_VALUE/2;
	}
}

static void xm_tick(xm_context_t* ctx) {
	if(ctx->current_tick == 0) {
		xm_row(ctx);
	}

	for(uint8_t i = 0; i < ctx->module.num_channels; ++i) {
		xm_channel_context_t* ch = ctx->channels + i;

		xm_tick_envelopes(ch);
		xm_autovibrato(ch);

		if(ctx->current_tick > 0) {
			xm_tick_effects(ctx, ch);
		}

		if(ch->period) {
			ch->step = xm_frequency(ctx, ch);
		}
		/* Guard against uint32_t overflow */
		static_assert(SAMPLE_MICROSTEPS <= 1 << 12);
		/* For A#9 and +127 finetune, frequency is about 535K */
		assert(ch->step < 1 << 20);
		ch->step *= SAMPLE_MICROSTEPS;
		/* Don't truncate, actually round up or down, precision matters
		   here (rounding lets us use 0.5 instead of 1 in the error
		   formula, see SAMPLE_MICROSTEPS comment) */
		ch->step = ch->step / ctx->rate
			+ ((ch->step % ctx->rate) << 1 > ctx->rate);

		uint8_t panning;
		float volume;

		panning = ch->panning
			+ (ch->panning_envelope_panning - MAX_ENVELOPE_VALUE/2)
			* (MAX_PANNING/2
			   - __builtin_abs(ch->panning - MAX_PANNING/2))
			/ (MAX_ENVELOPE_VALUE/2);

		assert(ch->volume <= MAX_VOLUME);
		assert(ch->volume_offset >= -MAX_VOLUME
		       && ch->volume_offset < MAX_VOLUME);

		static_assert(MAX_VOLUME == 1<<6);
		static_assert(MAX_ENVELOPE_VALUE == 1<<6);
		static_assert(MAX_FADEOUT_VOLUME == 1<<15);

		/* 6 + 6 + 15 - 2 + 6 => 31 bits of range */
		int32_t base = ch->volume + ch->volume_offset;
		if(base < 0) base = 0;
		else if(base > MAX_VOLUME) base = MAX_VOLUME;
		base *= ch->volume_envelope_volume;
		base *= ch->fadeout_volume;
		base /= 4;
		base *= ctx->global_volume;
		volume =  (float)base / (float)(INT32_MAX);
		assert(volume >= 0.f && volume <= 1.f);

#if XM_RAMPING
		/* See https://modarchive.org/forums/index.php?topic=3517.0
		 * and https://github.com/Artefact2/libxm/pull/16 */
		ch->target_volume[0] = volume
			* sqrtf((float)(MAX_PANNING - panning)
			        / (float)MAX_PANNING);
		ch->target_volume[1] = volume
			* sqrtf((float)panning / (float)MAX_PANNING);
#else
		ch->actual_volume[0] = volume
			* sqrtf((float)(MAX_PANNING - panning)
			        / (float)MAX_PANNING);
		ch->actual_volume[1] = volume * sqrtf((float)panning
		                                      / (float)MAX_PANNING);
#endif
	}

	ctx->current_tick++;
	if(ctx->current_tick >= ctx->tempo) {
		if(ctx->extra_rows) {
			/* Only restart after one extra tick, and restart at
			   tick 1 instead (this is important for
			   xm_tick_effects()). */
			if(ctx->current_tick > ctx->tempo) {
				--ctx->extra_rows;
				ctx->current_tick = 1;
			}
		} else {
			ctx->current_tick = 0;
		}
	}

	/* FT2 manual says number of ticks / second = BPM * 0.4 */
	static_assert(_Generic(ctx->remaining_samples_in_tick,
	                       int32_t: true, default: false));
	static_assert(_Generic(ctx->rate, uint16_t: true, default: false));
	static_assert(TICK_SUBSAMPLES % 4 == 0);
	static_assert(10 * (TICK_SUBSAMPLES / 4) * UINT16_MAX <= INT32_MAX);
	int32_t samples_in_tick = ctx->rate;
	samples_in_tick *= 10 * TICK_SUBSAMPLES / 4;
	samples_in_tick /= ctx->bpm;
	ctx->remaining_samples_in_tick += samples_in_tick;
}

/* These effects only do something every tick after the first tick of every row.
   Immediate effects (like Cxx or Fxx) are handled in
   xm_handle_pattern_slot(). */
static void xm_tick_effects(xm_context_t* ctx, xm_channel_context_t* ch) {
	switch(ch->current->volume_column >> 4) {

	case 0x6: /* -x: Volume slide down */
		ch->volume_offset = 0;
		xm_param_slide(&ch->volume, ch->current->volume_column & 0x0F,
		               MAX_VOLUME);
		break;

	case 0x7: /* +x: Volume slide up */
		ch->volume_offset = 0;
		xm_param_slide(&ch->volume, ch->current->volume_column << 4,
		               MAX_VOLUME);
		break;

	case 0xB: /* Vx: Vibrato */
		if(ch->current->volume_column & 0x0F) {
			ch->vibrato_param = (ch->vibrato_param & 0xF0)
				| (ch->current->volume_column & 0x0F);
		}
		/* This vibrato *does not* reset pitch when the command
		   is discontinued */
		ch->should_reset_vibrato = false;
		xm_vibrato(ch);
		break;

	case 0xD: /* ◀x: Panning slide left */
		xm_param_slide(&ch->panning, ch->current->volume_column & 0x0F,
		               MAX_PANNING-1);
		break;

	case 0xE: /* ▶x: Panning slide right */
		xm_param_slide(&ch->panning, ch->current->volume_column << 4,
		               MAX_PANNING-1);
		break;

	case 0xF: /* Mx: Tone portamento */
		if(ch->current->volume_column & 0x0F) {
			ch->tone_portamento_param =
				(ch->current->volume_column & 0x0F) * 0x11;
		}
		xm_tone_portamento(ch);
		break;

	}

	switch(ch->current->effect_type) {

	case 0: /* 0xy: Arpeggio */
		/* Technically not necessary, since 000 arpeggio will do
		   nothing, this is a performance optimisation. */
		if(ch->current->effect_param == 0) break;
		xm_arpeggio(ctx, ch);
		break;

	case 1: /* 1xx: Portamento up */
		if(ch->current->effect_param > 0) {
			ch->portamento_up_param = ch->current->effect_param;
		}
		xm_pitch_slide(ch, -4 * ch->portamento_up_param);
		break;

	case 2: /* 2xx: Portamento down */
		if(ch->current->effect_param > 0) {
			ch->portamento_down_param = ch->current->effect_param;
		}
		xm_pitch_slide(ch, 4 * ch->portamento_down_param);
		break;

	case 3: /* 3xx: Tone portamento */
		xm_tone_portamento(ch);
		break;

	case 4: /* 4xy: Vibrato */
		if(ch->current->effect_param & 0x0F) {
			/* Set vibrato depth */
			ch->vibrato_param = (ch->vibrato_param & 0xF0)
				| (ch->current->effect_param & 0x0F);
		}
		if(ch->current->effect_param >> 4) {
			/* Set vibrato speed */
			ch->vibrato_param = (ch->current->effect_param & 0xF0)
				| (ch->vibrato_param & 0x0F);
		}
		ch->should_reset_vibrato = true;
		xm_vibrato(ch);
		break;

	case 5: /* 5xy: Tone portamento + Volume slide */
		if(ch->current->effect_param > 0) {
			ch->volume_slide_param = ch->current->effect_param;
		}
		ch->volume_offset = 0;
		xm_tone_portamento(ch);
		xm_param_slide(&ch->volume, ch->volume_slide_param, MAX_VOLUME);
		break;

	case 6: /* 6xy: Vibrato + Volume slide */
		if(ch->current->effect_param > 0) {
			ch->volume_slide_param = ch->current->effect_param;
		}
		ch->volume_offset = 0;
		ch->should_reset_vibrato = true;
		xm_vibrato(ch);
		xm_param_slide(&ch->volume, ch->volume_slide_param, MAX_VOLUME);
		break;

	case 7: /* 7xy: Tremolo */
		if(ch->current->effect_param & 0x0F) {
			/* Set tremolo depth */
			ch->tremolo_param = (ch->tremolo_param & 0xF0)
				| (ch->current->effect_param & 0x0F);
		}
		if(ch->current->effect_param >> 4) {
			/* Set tremolo speed */
			ch->tremolo_param = (ch->current->effect_param & 0xF0)
				| (ch->tremolo_param & 0x0F);
		}
		xm_tremolo(ch);
		break;

	case 0xA: /* Axy: Volume slide */
		if(ch->current->effect_param > 0) {
			ch->volume_slide_param = ch->current->effect_param;
		}
		ch->volume_offset = 0;
		xm_param_slide(&ch->volume, ch->volume_slide_param, MAX_VOLUME);
		break;

	case 0xE: /* EXy: Extended command */
		switch(ch->current->effect_param >> 4) {

		case 0x9: /* E9y: Retrigger note */
			/* XXX: what does E90 do? test this effect */
			if(!(ch->current->effect_param & 0x0F)) break;
			if(ctx->current_tick
			   % (ch->current->effect_param & 0x0F)) break;
			xm_trigger_note(ctx, ch);
			xm_tick_envelopes(ch);
			break;

		case 0xC: /* ECy: Note cut */
			/* XXX: test this effect */
			if((ch->current->effect_param & 0x0F) == ctx->current_tick) {
				xm_cut_note(ch);
			}
			break;

		case 0xD: /* EDy: Note delay */
			if(ch->note_delay_param == ctx->current_tick) {
				xm_handle_pattern_slot(ctx, ch);
				/* EDy (y>0) has a weird trigger mechanism,
				   where it will reset sample position and
				   period (except if we have a keyoff), and it
				   will reset envelopes and sustain status but
				   keep volume/panning (so it's not a true
				   instrument trigger) */
				ch->volume_envelope_frame_count = 0;
				ch->panning_envelope_frame_count = 0;
				ch->sustained = true;
				xm_trigger_note(ctx, ch);
				xm_tick_envelopes(ch);
			}
			break;

		}
		break;

	case 17: /* Hxy: Global volume slide */
		if(ch->current->effect_param > 0) {
			ch->global_volume_slide_param = ch->current->effect_param;
		}
		xm_param_slide(&ctx->global_volume,
		               ch->global_volume_slide_param, MAX_VOLUME);
		break;

	case 20: /* Kxx: Key off (as tick effect) */
		if(ctx->current_tick == ch->current->effect_param) {
			xm_key_off(ctx, ch);
		}
		break;

	case 25: /* Pxy: Panning slide */
		if(ch->current->effect_param > 0) {
			ch->panning_slide_param = ch->current->effect_param;
		}
		xm_param_slide(&ch->panning, ch->panning_slide_param,
		               MAX_PANNING-1);
		break;

	case 27: /* Rxy: Multi retrig note */
		if(ch->current->effect_param > 0) {
			if((ch->current->effect_param >> 4) == 0) {
				/* Keep previous x value */
				ch->multi_retrig_param = (ch->multi_retrig_param & 0xF0) | (ch->current->effect_param & 0x0F);
			} else {
				ch->multi_retrig_param = ch->current->effect_param;
			}
		}
		xm_multi_retrig_note(ctx, ch);
		break;

	case 29: /* Txy: Tremor */
		/* (x+1) ticks on, then (y+1) ticks off */
		/* Effect is not the same every row: it keeps an internal tick
		   counter and updates to the parameter only matters at the end
		   of an on or off cycle */
		/* If tremor ends with "off" volume, volume stays off, but *any*
		   volume effect restores the volume (with the volume effect
		   applied). */
		/* It works exactly like 7xy: Tremolo with a square waveform,
		   and xy param defines the period and duty cycle. */
		/* Tremor x and y params do not appear to be separately kept in
		   memory, unlike Rxy */
		if(ch->current->effect_param > 0) {
			ch->tremor_param = ch->current->effect_param;
		}
		if(ch->tremor_ticks == 0) {
			ch->tremor_on = !ch->tremor_on;
			if(ch->tremor_on) {
				ch->tremor_ticks = (ch->tremor_param >> 4) + 1;
			} else {
				ch->tremor_ticks = (ch->tremor_param & 0xF) + 1;
			}
		}
		ch->volume_offset = ch->tremor_on ? 0 : -MAX_VOLUME;
		ch->tremor_ticks--;
		break;
	}
}

static float xm_sample_at(const xm_context_t* ctx,
                          const xm_sample_t* sample, uint32_t k) {
	assert(sample != NULL);
	assert(k < sample->length);
	assert(sample->index + k < ctx->module.samples_data_length);
	return _Generic((xm_sample_point_t){},
	                int8_t: (float)ctx->samples_data[sample->index + k] / (float)INT8_MAX,
	                int16_t: (float)ctx->samples_data[sample->index + k] / (float)INT16_MAX,
	                float: ctx->samples_data[sample->index + k]);
}

/* XXX: rename me or merge with xm_next_of_channel */
static float xm_next_of_sample(xm_context_t* ctx, xm_channel_context_t* ch) {
	if(ch->sample == NULL) {
		#if XM_RAMPING
		if(ch->frame_count < RAMPING_POINTS) {
			return XM_LERP(ch->end_of_previous_sample[ch->frame_count], .0f,
			               (float)ch->frame_count / (float)RAMPING_POINTS);
		}
		#endif
		return .0f;
	}

	/* XXX: maybe do something about 0 length samples in load.c? */
	if(ch->sample->length == 0) {
		return .0f;
	}

	xm_sample_t* smp = ch->sample;
	uint32_t a = ch->sample_position / SAMPLE_MICROSTEPS;
	[[maybe_unused]] float t = (float)(ch->sample_position % SAMPLE_MICROSTEPS) / (float)SAMPLE_MICROSTEPS;
	[[maybe_unused]] uint32_t b;
	ch->sample_position += ch->step;

	if(ch->sample->loop_length == 0) {
		if((ch->sample_position / SAMPLE_MICROSTEPS)
		   >= ch->sample->length) {
			ch->sample = NULL;
			b = a;
		} else {
			b = (a+1 < ch->sample->length) ? (a+1) : a;
		}
	} else if(!ch->sample->ping_pong) {
		/* If length=6, loop_length=4 */
		/* 0 1 (2 3 4 5) (2 3 4 5) (2 3 4 5) ... */
		assert(ch->sample->loop_length > 0);
		while((ch->sample_position / SAMPLE_MICROSTEPS)
		      >= ch->sample->length) {
			ch->sample_position -= ch->sample->loop_length
				* SAMPLE_MICROSTEPS;
		}
		b = (a+1 == ch->sample->length) ?
			ch->sample->length - ch->sample->loop_length : (a+1);
		assert(a < ch->sample->length);
		assert(b < ch->sample->length);
	} else {
		/* If length=6, loop_length=4 */
		/* 0 1 (2 3 4 5 5 4 3 2) (2 3 4 5 5 4 3 2) ... */
		assert(ch->sample->loop_length > 0);
		while((ch->sample_position / SAMPLE_MICROSTEPS) >=
		      ch->sample->length + ch->sample->loop_length) {
			/* This will not overflow, loop_length size is checked
			   in load.c */
			ch->sample_position -= ch->sample->loop_length
				* 2 * SAMPLE_MICROSTEPS;
		}

		if(a < ch->sample->length) {
			/* In the first half of the loop, go forwards */
			b = (a+1 == ch->sample->length) ? a : (a+1);
		} else {
			/* In the second half of the loop, go backwards */
			/* loop_end -> loop_end - 1 */
			/* loop_end + 1 -> loop_end - 2 */
			/* etc. */
			/* loop_end + loop_length - 1 -> loop_start */
			a = ch->sample->length * 2 - 1 - a;
			b = (a == ch->sample->length - ch->sample->loop_length) ?
				a : (a-1);
			assert(a >= ch->sample->length
			       - ch->sample->loop_length);
			assert(b >= ch->sample->length
			       - ch->sample->loop_length);
		}

		assert(a < ch->sample->length);
		assert(b < ch->sample->length);
	}

	float u = (float)xm_sample_at(ctx, smp, a);

	#if XM_LINEAR_INTERPOLATION
	/* u = sample_at(a), v = sample_at(b), t = lerp factor (0..1) */
	u = XM_LERP(u, (float)xm_sample_at(ctx, smp, b), t);
	#endif

	#if XM_RAMPING
	if(ch->frame_count < RAMPING_POINTS) {
		/* Smoothly transition between old and new sample. */
		return XM_LERP(ch->end_of_previous_sample[ch->frame_count], u,
		               (float)ch->frame_count / (float)RAMPING_POINTS);
	}
	#endif

	return u;
}

static void xm_next_of_channel(xm_context_t* ctx, xm_channel_context_t* ch,
                               float* out_left, float* out_right) {
	const float fval = xm_next_of_sample(ctx, ch) * AMPLIFICATION;

	if(ch->muted || (ch->instrument != NULL && ch->instrument->muted)
	   || (ctx->max_loop_count > 0
	       && ctx->loop_count >= ctx->max_loop_count)) {
		return;
	}

	*out_left += fval * ch->actual_volume[0];
	*out_right += fval * ch->actual_volume[1];

	#if XM_RAMPING
	ch->frame_count++;
	XM_SLIDE_TOWARDS(&(ch->actual_volume[0]),
	                 ch->target_volume[0], RAMPING_VOLUME_RAMP);
	XM_SLIDE_TOWARDS(&(ch->actual_volume[1]),
	                 ch->target_volume[1], RAMPING_VOLUME_RAMP);
	#endif
}

static void xm_sample_unmixed(xm_context_t* ctx, float* out_lr) {
	if(ctx->remaining_samples_in_tick <= 0) {
		xm_tick(ctx);
	}
	ctx->remaining_samples_in_tick -= TICK_SUBSAMPLES;

	for(uint8_t i = 0; i < ctx->module.num_channels; ++i) {
		out_lr[2*i] = 0.f;
		out_lr[2*i+1] = 0.f;

		xm_next_of_channel(ctx, ctx->channels + i,
		                   out_lr + 2*i, out_lr + 2*i+1);

		assert(out_lr[2*i] <= 1.f);
		assert(out_lr[2*i] >= -1.f);
		assert(out_lr[2*i+1] <= 1.f);
		assert(out_lr[2*i+1] >= -1.f);

		#if XM_DEFENSIVE
		XM_CLAMP2F(out_lr[2*i], 1.f, -1.f);
		XM_CLAMP2F(out_lr[2*i+1], 1.f, -1.f);
		#endif
	}
}

static void xm_sample(xm_context_t* ctx, float* out_left, float* out_right) {
	if(ctx->remaining_samples_in_tick <= 0) {
		xm_tick(ctx);
	}
	ctx->remaining_samples_in_tick -= TICK_SUBSAMPLES;

	*out_left = 0.f;
	*out_right = 0.f;

	for(uint8_t i = 0; i < ctx->module.num_channels; ++i) {
		xm_next_of_channel(ctx, ctx->channels + i, out_left, out_right);
	}

	assert(*out_left <= ctx->module.num_channels);
	assert(*out_left >= -ctx->module.num_channels);
	assert(*out_right <= ctx->module.num_channels);
	assert(*out_right >= -ctx->module.num_channels);

	#if XM_DEFENSIVE
	XM_CLAMP2F(*out_left, 1.f, -1.f);
	XM_CLAMP2F(*out_right, 1.f, -1.f);
	#endif
}

void xm_generate_samples(xm_context_t* ctx,
                         float* output,
                         uint16_t numsamples) {
	#if XM_TIMING_FUNCTIONS
	ctx->generated_samples += numsamples;
	#endif
	for(uint16_t i = 0; i < numsamples; i++, output += 2) {
		xm_sample(ctx, output, output + 1);
	}
}

void xm_generate_samples_noninterleaved(xm_context_t* ctx,
                                        float* out_left,
                                        float* out_right,
                                        uint16_t numsamples) {
	#if XM_TIMING_FUNCTIONS
	ctx->generated_samples += numsamples;
	#endif
	for(uint16_t i = 0; i < numsamples; ++i) {
		xm_sample(ctx, out_left++, out_right++);
	}
}

void xm_generate_samples_unmixed(xm_context_t* ctx,
                                 float* out,
                                 uint16_t numsamples) {
	#if XM_TIMING_FUNCTIONS
	ctx->generated_samples += numsamples;
	#endif
	for(uint16_t i = 0; i < numsamples;
	    ++i, out += ctx->module.num_channels * 2) {
		xm_sample_unmixed(ctx, out);
	}
}
