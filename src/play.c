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
static void xm_tone_portamento(const xm_context_t*, xm_channel_context_t*) __attribute__((nonnull));
static void xm_pitch_slide(xm_channel_context_t*, int16_t) __attribute__((nonnull));
static void xm_param_slide(uint8_t*, uint8_t, uint8_t) __attribute__((nonnull));
static void xm_tick_effects(xm_context_t*, xm_channel_context_t*) __attribute__((nonnull));

static uint8_t xm_envelope_lerp(const xm_envelope_point_t* restrict, const xm_envelope_point_t* restrict, uint16_t) __attribute__((warn_unused_result)) __attribute__((nonnull))  __attribute__((const));
static uint8_t xm_tick_envelope(xm_channel_context_t*, const xm_envelope_t*, uint16_t*) __attribute__((nonnull)) __attribute__((warn_unused_result));
static void xm_tick_envelopes(xm_channel_context_t*) __attribute__((nonnull));

static uint16_t xm_linear_period(int16_t) __attribute__((warn_unused_result)) __attribute__((const));
static uint32_t xm_linear_frequency(uint16_t, uint8_t) __attribute__((warn_unused_result)) __attribute__((nonnull))  __attribute__((const));
static uint16_t xm_amiga_period(int16_t) __attribute__((warn_unused_result)) __attribute__((const));
static uint32_t xm_amiga_frequency(uint16_t, uint8_t) __attribute__((warn_unused_result)) __attribute__((nonnull))  __attribute__((const));

static uint16_t xm_period(const xm_context_t*, int16_t) __attribute__((warn_unused_result)) __attribute__((nonnull))  __attribute__((const));
static uint32_t xm_frequency(const xm_context_t*, const xm_channel_context_t*) __attribute__((warn_unused_result)) __attribute__((nonnull))  __attribute__((const));

static void xm_handle_pattern_slot(xm_context_t*, xm_channel_context_t*) __attribute__((nonnull));
static void xm_tone_portamento_target(const xm_context_t*, xm_channel_context_t*) __attribute__((nonnull));
static void xm_trigger_instrument(xm_context_t*, xm_channel_context_t*) __attribute__((nonnull));
static void xm_trigger_note(xm_context_t*, xm_channel_context_t*) __attribute__((nonnull));
static void xm_cut_note(xm_channel_context_t*) __attribute__((nonnull));
static void xm_key_off(xm_context_t*, xm_channel_context_t*) __attribute__((nonnull));

static void xm_post_pattern_change(xm_context_t*) __attribute__((nonnull));
static void xm_row(xm_context_t*) __attribute__((nonnull));
static void xm_tick(xm_context_t*) __attribute__((nonnull));

static float xm_sample_at(const xm_context_t*, const xm_sample_t*, uint32_t) __attribute__((warn_unused_result)) __attribute__((nonnull)) __attribute__((const));
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

__attribute__((const)) __attribute__((nonnull))
static bool HAS_TONE_PORTAMENTO(const xm_pattern_slot_t* s) {
	return s->effect_type == 3 || s->effect_type == 5
		|| (s->volume_column >> 4) == 0xF;
}

__attribute__((const)) __attribute__((nonnull))
static bool HAS_VIBRATO(const xm_pattern_slot_t* s) {
	return s->effect_type == 4 || s->effect_type == 6
		|| (s->volume_column >> 4) == 0xB;
}

__attribute__((const))
static bool NOTE_IS_KEY_OFF(uint8_t n) {
	return n & KEY_OFF_NOTE;
}

__attribute__((nonnull))
static void UPDATE_EFFECT_MEMORY_XY(uint8_t* memory, uint8_t value) {
	if(value & 0x0F) {
		*memory = (*memory & 0xF0) | (value & 0x0F);
	}
	if(value & 0xF0) {
		*memory = (*memory & 0x0F) | (value & 0xF0);
	}
}

/* ----- Function definitions ----- */

static int8_t xm_waveform(uint8_t waveform, uint8_t step) {
	step %= 0x40;

	switch(waveform & 3) {

	case 0: /* Sine */
		static const int8_t sin_lut[] = {
			/* 128*sinf(2πx/64) for x in 0..16 */
			0, 12, 24, 37, 48, 60, 71, 81,
			90, 98, 106, 112, 118, 122, 125, 127,
		};
		uint8_t idx = step & 0x10 ? 0xF - (step & 0xF) : (step & 0xF);
		return (step < 0x20) ? -sin_lut[idx] : sin_lut[idx];

	case 2: /* Square */
		return (step < 0x20) ? INT8_MIN : INT8_MAX;

	case 1: /* Ramp down */
		/* Starts at zero, wraps around at the middle */
		return (int8_t)(-step * 4 - 1);

	case 3: /* Ramp up */
		/* Only used by autovibrato, regular E4y/E7y will use a square
		   wave instead (this is set by load.c) */
		return (int8_t)(step * 4);

	}

	assert(0);
}

static void xm_autovibrato(xm_channel_context_t* ch) {
	xm_instrument_t* instr = ch->instrument;
	if(instr == NULL) return;

	/* Autovibrato speed is 4x slower than equivalent 4xx effect (ie,
	   autovibrato_rate of 4 is the same as 41y). */
	/* Autovibrato depth is 8x smaller than equivalent 4xx effect (ie,
	   autovibrato_depth of 8 is the same as 4x1). */
	/* Autovibrato also flips the sign of the waveform. */
	/* Autovibrato is cumulative with regular vibrato and is also a straight
	   period offset for amiga frequencies. */

	/* Depth 16 = 0.5 semitone amplitude (-0.25 then +0.25) */
	/* Scale waveform from -128..127 to -16..15 at depth 16 */
	ch->autovibrato_offset = (int8_t)
		(((int16_t)xm_waveform(instr->vibrato_type,
		                       (uint8_t)(ch->autovibrato_ticks
		                                 * instr->vibrato_rate / 4)))
		 * (-instr->vibrato_depth) / 128);

	if(ch->autovibrato_ticks < instr->vibrato_sweep) {
		ch->autovibrato_offset = (int8_t)
			((int16_t)ch->autovibrato_offset
			* ch->autovibrato_ticks / instr->vibrato_sweep);
	}

	ch->autovibrato_ticks++;
}

static void xm_vibrato(xm_channel_context_t* ch) {
	/* Depth 8 == 2 semitones amplitude (-1 then +1) */
	ch->vibrato_offset = (int8_t)
		((int16_t)xm_waveform(ch->vibrato_control_param,
		                      ch->vibrato_ticks)
		 * (ch->vibrato_param & 0x0F) / 0x10);
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
	ch->volume_offset = (int8_t)
		((int16_t)xm_waveform(ch->tremolo_control_param,
		                       ch->tremolo_ticks)
		* (ch->tremolo_param & 0x0F) * 4 / 128);
	ch->tremolo_ticks += (ch->tremolo_param >> 4);
}

static void xm_multi_retrig_note(xm_context_t* ctx, xm_channel_context_t* ch) {
	/* Seems to work similarly to Txy tremor effect. It uses an increasing
	   counter and also runs on tick 0. */

	UPDATE_EFFECT_MEMORY_XY(&ch->multi_retrig_param,
	                        ch->current->effect_param);

	if(ch->current->volume_column && ctx->current_tick == 0) {
		/* ??? */
		return;
	}
	if(++ch->multi_retrig_ticks < (ch->multi_retrig_param & 0x0F)) {
		return;
	}
	ch->multi_retrig_ticks = 0;
	xm_trigger_note(ctx, ch);

	/* Fixed volume in volume column always has precedence */
	if(ch->current->volume_column >= 0x10
	   && ch->current->volume_column <= 0x50) {
		return;
	}

	static const uint8_t add[] = {
		0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 4, 8, 16, 0, 0,
	};
	static const uint8_t mul[] = {
		1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 3, 2,
	};
	uint8_t x = (uint8_t)(ch->multi_retrig_param >> 4);
	ch->volume += add[x];
	ch->volume -= add[x ^ 8];
	ch->volume *= mul[x];
	ch->volume /= mul[x ^ 8];

	static_assert(MAX_VOLUME + 16 <= UINT8_MAX);
	static_assert(MAX_VOLUME * 3 <= UINT8_MAX - 16);
	if(ch->volume > UINT8_MAX - 16) ch->volume = 0;
	else if(ch->volume > MAX_VOLUME) ch->volume = MAX_VOLUME;
}

static void xm_arpeggio(xm_context_t* ctx, xm_channel_context_t* ch) {
	/* Arp effect always resets vibrato offset, even if it only runs for 1
	   tick where the offset is 0 (eg spd=2 001). Tick counter isn't
	   reset. Autovibrato is still applied. */
	ch->vibrato_offset = 0;

	/* This can happen with eg EEy pattern delay */
	if(ctx->current_tick == 0) {
		ch->arp_note_offset = 0;
		return;
	}

	/* Emulate FT2 overflow quirk */
	if(ctx->tempo - ctx->current_tick == 16) {
		ch->arp_note_offset = 0;
		return;
	} else if(ctx->tempo - ctx->current_tick > 16) {
		ch->arp_note_offset = ch->current->effect_param & 0x0F;
		return;
	}

	switch((ctx->tempo - ctx->current_tick) % 3) {
	case 0:
		ch->arp_note_offset = 0;
		break;
	case 1:
		ch->arp_note_offset = ch->current->effect_param >> 4;
		break;
	case 2:
		ch->arp_note_offset = ch->current->effect_param & 0x0F;
		break;
	default:
		assert(0);
	}
}

static void xm_tone_portamento(const xm_context_t* ctx,
                               xm_channel_context_t* ch) {
	/* 3xx called without a note, wait until we get an actual
	 * target note. */
	if(ch->tone_portamento_target_period == 0 || ch->period == 0) return;

	uint16_t incr = 4 * ch->tone_portamento_param;
	int32_t diff = ch->tone_portamento_target_period - ch->period;
	diff = diff > incr ? incr : diff;
	diff = diff < (-incr) ? (-incr) : diff;
	xm_pitch_slide(ch, (int16_t)diff);

	if(ch->glissando_control_param == 0) return;

	if(XM_FREQUENCY_TYPES == 1
	   || (XM_FREQUENCY_TYPES == 3 && (!ctx->module.amiga_frequencies))) {
		/* Round period to nearest semitone. Store rounding error in
		   ch->glissando_control_error. */
		/* With linear frequencies, 1 semitone is 64 period units. */
		uint16_t new_period = (uint16_t)((ch->period + 32) & 0xFFC0);
		ch->glissando_control_error = (int8_t)(ch->period - new_period);
		ch->period = new_period;
	} else {
		/* XXX: implement for amiga frequencies */
	}
}

static void xm_pitch_slide(xm_channel_context_t* ch,
                           int16_t period_offset) {
	ch->period = (uint16_t)(ch->period + ch->glissando_control_error);
	ch->glissando_control_error = 0;

	/* Clamp period when sliding up (matches FT2 behaviour), wrap around
	   when sliding down (albeit still in a broken way compared to FT2) */
	ch->period = (ch->period + period_offset < 1)
		? 1 : (uint16_t)(ch->period + period_offset);
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
	uint32_t val = (uint32_t)
		(b->value * (uint16_t)(pos - a->frame)
		 + a->value * (uint16_t)(b->frame - pos));
	val /= (uint16_t)(b->frame - a->frame);
	return (uint8_t)val;
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

[[maybe_unused]] static uint32_t xm_linear_frequency(uint16_t period,
                                                     uint8_t arp_note_offset) {
	if(arp_note_offset) {
		/* XXX: test wraparound? */
		period -= (uint16_t)(arp_note_offset * 64);
		/* 1540 is the period of note 95+15/16ths, the maximum
		   FT2 will use for an arpeggio */
		period = period < 1540 ? 1540 : period;
	}
	return (uint32_t)(8363.f * exp2f((4608.f - (float)period) / 768.f));
}

[[maybe_unused]] static uint16_t xm_amiga_period(int16_t note) {
	return (uint16_t)(32.f * 856.f * exp2f((float)note / (-12.f * 16.f)));
}

[[maybe_unused]] static uint32_t xm_amiga_frequency(uint16_t period,
                                                    uint8_t arp_note_offset) {
	float p = (float)period;
	if(arp_note_offset) {
		p *= exp2f((float)arp_note_offset / (-12.f));
		p = p < 107.f ? 107.f : p;
	}

	/* This is the PAL value. No reason to choose this one over the
	 * NTSC value. */
	return (uint32_t)(4.f * 7093789.2f / (p * 2.f));
}

static uint16_t xm_period([[maybe_unused]] const xm_context_t* ctx,
                          int16_t note) {
	#if XM_FREQUENCY_TYPES == 1
	return xm_linear_period(note);
	#elif XM_FREQUENCY_TYPES == 2
	return xm_amiga_period(note);
	#else
	return ctx->module.amiga_frequencies ?
		xm_amiga_period(note) : xm_linear_period(note);
	#endif
}

static uint32_t xm_frequency([[maybe_unused]] const xm_context_t* ctx,
                             const xm_channel_context_t* ch) {
	assert(ch->period > 0);
	/* XXX: test wraparound/overflow */
	uint16_t period = (uint16_t)(ch->period - ch->vibrato_offset
		- ch->autovibrato_offset);

	#if XM_FREQUENCY_TYPES == 1
	return xm_linear_frequency(period, ch->arp_note_offset);
	#elif XM_FREQUENCY_TYPES == 2
	return xm_amiga_frequency(period, ch->arp_note_offset);
	#else
	return ctx->module.amiga_frequencies
		? xm_amiga_frequency(period, ch->arp_note_offset)
		: xm_linear_frequency(period, ch->arp_note_offset);
	#endif
}

static void xm_handle_pattern_slot(xm_context_t* ctx, xm_channel_context_t* ch) {
	xm_pattern_slot_t* s = ch->current;

	if(s->instrument) {
		/* Update ch->next_instrument */
		ch->next_instrument = s->instrument;
	}

	if(!NOTE_IS_KEY_OFF(s->note)) {
		if(s->note) {
			/* Non-zero note, also not key off. Assume note is
			   valid, since invalid notes are deleted in load.c. */

			if(HAS_TONE_PORTAMENTO(ch->current)) {
				xm_tone_portamento_target(ctx, ch);
			} else {
				/* Orig note (used for retriggers) is not
				   updated by tone portas */
				ch->orig_note = s->note;
				xm_trigger_note(ctx, ch);
			}

		} else if(s->effect_type == 0x0E && s->effect_param == 0x90) {
			/* E90 acts like a ghost note */
			xm_trigger_note(ctx, ch);
		}
	}

	if(s->instrument) {
		xm_trigger_instrument(ctx, ch);
	}

	if(NOTE_IS_KEY_OFF(s->note)) {
		/* Key Off */
		xm_key_off(ctx, ch);
	}

	/* These volume effects always work, even when called with a delay by
	   EDy. */
	if(s->volume_column >= 0x10 && s->volume_column <= 0x50) {
		/* Set volume */
		ch->volume_offset = 0;
		ch->volume = s->volume_column - 0x10;
	}
	if(s->volume_column >> 4 == 0xC) {
		/* Px: Set panning */
		ch->panning = s->volume_column << 4;
	}

	/* Set tone portamento memory (even on tick 0) */
	if(s->volume_column >> 4 == 0xF) {
		/* Mx *always* has precedence, even M0 */
		if(s->volume_column & 0x0F) {
			ch->tone_portamento_param = s->volume_column << 4;
		}
	} else if(s->effect_type == 3) {
		if(s->effect_param > 0) {
			ch->tone_portamento_param = s->effect_param;
		}
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
			/* XXX: test me (with note delay, with simultaneous
			   4xy/40y) */
			/* S0 does nothing, but is deleted in load.c */
			UPDATE_EFFECT_MEMORY_XY(&ch->vibrato_param,
			                        s->volume_column << 4);
			break;

		}
	}

	switch(s->effect_type) {

	case 8: /* 8xx: Set panning */
		ch->panning = s->effect_param;
		break;

	case 0xB: /* Bxx: Position jump */
		ctx->position_jump = true;
		ctx->jump_dest = s->effect_param;
		ctx->jump_row = 0;
		break;

	case 0xC: /* Cxx: Set volume */
		ch->volume_offset = 0;
		/* xx > MAX_VOLUME is already clamped in load.c */
		ch->volume = s->effect_param;
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

		case 3: /* E3y: Set glissando control */
			ch->glissando_control_param = s->effect_param & 0x0F;
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
			/* Loop current row y times. Tick effects *are* applied
			   on tick 0 of repeated rows. */
			ctx->extra_rows = (ch->current->effect_param & 0x0F);
			break;

		}
		break;

	case 0xF: /* Fxx: Set tempo/BPM */
		static_assert(MIN_BPM == 0b00100000);
		if(s->effect_param & 0b11100000) {
			ctx->bpm = s->effect_param;
		} else {
			ctx->tempo = s->effect_param;
		}
		break;

	case 16: /* Gxx: Set global volume */
		/* xx > MAX_VOLUME is already clamped in load.c */
		ctx->global_volume = s->effect_param;
		break;

	case 21: /* Lxx: Set envelope position */
		ch->volume_envelope_frame_count = s->effect_param;
		ch->panning_envelope_frame_count = s->effect_param;
		break;

	case 27: /* Rxy: Multi retrig note */
		xm_multi_retrig_note(ctx, ch);
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

		default:
			assert(0);

		}
		break;

	}
}

static void xm_tone_portamento_target(const xm_context_t* ctx,
                                      xm_channel_context_t* ch) {
	assert(HAS_TONE_PORTAMENTO(ch->current));
	if(ch->sample == NULL) return;

	/* Tone porta uses the relative note of whatever sample we have, even if
	   the target note belongs to another sample with another relative
	   note. */
	int16_t note = (int16_t)(ch->current->note + ch->sample->relative_note);

	/* Invalid notes keep whatever target period was there before. */
	if(note <= 0 || note >= 120) return;

	/* 3xx/Mx ignores E5y, but will reuse whatever finetune was set when
	   initially triggering the note */
	/* XXX: refactor note+finetune logic with xm_trigger_note() */
	ch->tone_portamento_target_period =
		xm_period(ctx, (int16_t)(16 * (note - 1) + ch->finetune));
}

static void xm_trigger_instrument([[maybe_unused]] xm_context_t* ctx,
                                  xm_channel_context_t* ch) {
	if(ch->instrument == NULL || ch->sample == NULL) return;

	ch->volume = ch->sample->volume;
	ch->panning = ch->sample->panning;

	ch->sustained = true;
	ch->volume_envelope_frame_count = 0;
	ch->panning_envelope_frame_count = 0;
	ch->tremor_ticks = 0;
	ch->multi_retrig_ticks = 0;
	ch->autovibrato_ticks = 0;
	ch->volume_offset = 0;

	if(!(ch->vibrato_control_param & 4)) {
		ch->vibrato_ticks = 0;
	}
	if(!(ch->tremolo_control_param & 4)) {
		ch->tremolo_ticks = 0;
	}

	#if XM_TIMING_FUNCTIONS
	ch->latest_trigger = ctx->generated_samples;
	ch->instrument->latest_trigger = ctx->generated_samples;
	#endif
}

static void xm_trigger_note(xm_context_t* ctx, xm_channel_context_t* ch) {
	#if XM_RAMPING
	if(ch->sample && ch->period) {
		static_assert(RAMPING_POINTS <= UINT8_MAX);
		for(uint8_t i = 0; i < RAMPING_POINTS; ++i) {
			ch->end_of_previous_sample[i] =
				xm_next_of_sample(ctx, ch);
		}
	} else {
		__builtin_memset(ch->end_of_previous_sample, 0,
		                 RAMPING_POINTS * sizeof(float));
	}
	ch->frame_count = 0;
	#endif

	/* Update ch->sample and ch->instrument */
	ch->instrument = ctx->instruments + ch->next_instrument - 1;
	if(ch->next_instrument == 0
	   || ch->next_instrument > ctx->module.num_instruments) {
		ch->instrument = NULL;
		ch->sample = NULL;
		xm_cut_note(ch);
		return;
	}

	if(ch->instrument->sample_of_notes[ch->orig_note - 1]
	   >= ch->instrument->num_samples) {
		/* XXX: does it also reset instrument? cut the note? zero the
		   period? */
		ch->sample = NULL;
		return;
	}

	ch->sample = ctx->samples
		+ ch->instrument->samples_index
		+ ch->instrument->sample_of_notes[ch->orig_note - 1];

	/* Update period */
	int16_t note = (int16_t)(ch->orig_note + ch->sample->relative_note);
	if(note <= 0 || note >= 120) {
		ch->period = 0;
		return;
	}

	/* Handle E5y: Set note fine-tune here; this effect only works in tandem
	   with a note and overrides the finetune value stored in the sample. If
	   we have Mx in the volume column, it does nothing. */
	if(ch->current->effect_type == 0xE
	   && (ch->current->effect_param >> 4) == 0x5) {
		ch->finetune = (int8_t)
			((ch->current->effect_param & 0xF) * 2 - 16);
	} else {
		ch->finetune = ch->sample->finetune;
	}
	ch->period = xm_period(ctx, (int16_t)(16 * (note - 1) + ch->finetune));

	/* Handle 9xx: Sample offset here, since it does nothing outside of a
	   note trigger (ie, called on its own without a note). If we have Mx in
	   the volume column, it does nothing. */
	if(ch->current->effect_type == 9) {
		if(ch->current->effect_param > 0) {
			ch->sample_offset_param = ch->current->effect_param;
		}
		ch->sample_position = ch->sample_offset_param * 256;
		if(ch->sample_position >= ch->sample->length) {
			ch->period = 0;
			return;
		}
	} else {
		ch->sample_position = 0;
	}

	ch->sample_position *= SAMPLE_MICROSTEPS;
	ch->glissando_control_error = 0; /* XXX: test me */
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
	   || ch->instrument->volume_envelope.num_points == 0) {
		xm_cut_note(ch);
	}
}

static void xm_row(xm_context_t* ctx) {
	if(ctx->position_jump || ctx->pattern_break) {
		if(ctx->position_jump) {
			ctx->current_table_index = ctx->jump_dest;
		} else {
			ctx->current_table_index++;
		}

		ctx->current_row = ctx->jump_row;
		ctx->position_jump = false;
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
		}

		if(ch->pattern_loop_count > 0) {
			in_a_loop = true;
		}

		ch->arp_note_offset = 0;

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

static uint8_t xm_tick_envelope(xm_channel_context_t* ch,
                                const xm_envelope_t* env,
                                uint16_t* counter) {
	assert(env->num_points >= 2);
	assert(env->loop_start_point < env->num_points);
	assert(env->loop_end_point < env->num_points);

	/* Only loop if we are exactly at loop_end. Don't loop if we went past
	   it, with eg a Lxx effect. Don't loop if sustain_point == loop_end and
	   the note is not sustained (FT2 quirk). */
	if(*counter == env->points[env->loop_end_point].frame
	   && (ch->sustained || env->sustain_point != env->loop_end_point)) {
		*counter = env->points[env->loop_start_point].frame;
	}

	/* Don't advance envelope position if we are sustaining */
	if((ch->sustained & !(env->sustain_point & 128))
	   && *counter == env->points[env->sustain_point].frame) {
		return env->points[env->sustain_point].value;
	}

	/* Find points left and right of current envelope position */
	for(uint8_t j = env->num_points - 1; j > 0; --j) {
		if(*counter < env->points[j-1].frame) continue;
		return xm_envelope_lerp(env->points + j - 1, env->points + j,
		                        (*counter)++);
	}

	assert(0);
}

static void xm_tick_envelopes(xm_channel_context_t* ch) {
	xm_instrument_t* inst = ch->instrument;
	if(inst == NULL) return;

	xm_autovibrato(ch);

	if(!ch->sustained) {
		ch->fadeout_volume =
			(ch->fadeout_volume < inst->volume_fadeout) ?
			0 : ch->fadeout_volume - inst->volume_fadeout;
	} else {
		ch->fadeout_volume = MAX_FADEOUT_VOLUME-1;
	}

	ch->volume_envelope_volume =
		inst->volume_envelope.num_points
		? xm_tick_envelope(ch, &(inst->volume_envelope),
		                   &(ch->volume_envelope_frame_count))
		: MAX_ENVELOPE_VALUE;

	ch->panning_envelope_panning =
		inst->panning_envelope.num_points
		? xm_tick_envelope(ch, &(inst->panning_envelope),
		                   &(ch->panning_envelope_frame_count))
		: MAX_ENVELOPE_VALUE / 2;
}

static void xm_tick(xm_context_t* ctx) {
	if(ctx->current_tick >= ctx->tempo) {
		ctx->current_tick = 0;
		ctx->extra_rows_done++;
	}

	/* Are we in the first tick of a new row? (Ie, not tick 0 of a repeated
	   row with EDy) */
	if(ctx->current_tick == 0
	   && (!ctx->extra_rows || ctx->extra_rows_done > ctx->extra_rows)) {
		ctx->extra_rows = 0;
		ctx->extra_rows_done = 0;
		xm_row(ctx);
	}

	/* Process effects of the entire row *before* moving on with the math,
	   as some values like global volume can still change later in the
	   row. */
	for(uint8_t i = 0; i < ctx->module.num_channels; ++i) {
		xm_channel_context_t* ch = ctx->channels + i;

		xm_tick_envelopes(ch);

		if(ctx->current_tick || ctx->extra_rows_done) {
			xm_tick_effects(ctx, ch);
		}
	}

	for(uint8_t i = 0; i < ctx->module.num_channels; ++i) {
		xm_channel_context_t* ch = ctx->channels + i;
		if(!ch->period) continue;

		/* Don't truncate, actually round up or down, precision matters
		   here (rounding lets us use 0.5 instead of 1 in the error
		   formula, see SAMPLE_MICROSTEPS comment) */
		ch->step = (uint32_t)
			(((uint64_t)xm_frequency(ctx, ch) * SAMPLE_MICROSTEPS
			  + ctx->rate / 2)
			 / ctx->rate);

		uint8_t panning = (uint8_t)
			(ch->panning
			 + (ch->panning_envelope_panning
			    - MAX_ENVELOPE_VALUE / 2)
			 * (MAX_PANNING/2
			    - __builtin_abs(ch->panning - MAX_PANNING / 2))
			 / (MAX_ENVELOPE_VALUE / 2));

		assert(ch->volume <= MAX_VOLUME);
		assert(ch->volume_offset >= -MAX_VOLUME
		       && ch->volume_offset <= MAX_VOLUME);

		static_assert(MAX_VOLUME == 1<<6);
		static_assert(MAX_ENVELOPE_VALUE == 1<<6);
		static_assert(MAX_FADEOUT_VOLUME == 1<<15);

		/* 6 + 6 + 15 - 2 + 6 => 31 bits of range */
		int32_t base = ch->volume - ch->volume_offset;
		if(base < 0) base = 0;
		else if(base > MAX_VOLUME) base = MAX_VOLUME;
		base *= ch->volume_envelope_volume;
		base *= ch->fadeout_volume;
		base /= 4;
		base *= ctx->global_volume;
		float volume =  (float)base / (float)(INT32_MAX);
		assert(volume >= 0.f && volume <= 1.f);

		#if XM_RAMPING
		float* out = ch->target_volume;
		#else
		float* out = ch->actual_volume;
		#endif

		/* See https://modarchive.org/forums/index.php?topic=3517.0
		 * and https://github.com/Artefact2/libxm/pull/16 */
		out[0] = volume * sqrtf((float)(MAX_PANNING - panning)
		                        / (float)MAX_PANNING);
		out[1] = volume * sqrtf((float)panning
		                        / (float)MAX_PANNING);
	}

	ctx->current_tick++;

	/* FT2 manual says number of ticks / second = BPM * 0.4 */
	static_assert(_Generic(ctx->remaining_samples_in_tick,
	                       uint32_t: true, default: false));
	static_assert(_Generic(ctx->rate, uint16_t: true, default: false));
	static_assert(TICK_SUBSAMPLES % 4 == 0);
	static_assert(10 * (TICK_SUBSAMPLES / 4) * UINT16_MAX <= UINT32_MAX);
	uint32_t samples_in_tick = ctx->rate;
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
		UPDATE_EFFECT_MEMORY_XY(&ch->vibrato_param,
		                        ch->current->volume_column & 0x0F);
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
		xm_tone_portamento(ctx, ch);
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
		[[fallthrough]];
	case 5: /* 5xx: Tone portamento + Volume slide */
		xm_tone_portamento(ctx, ch);
		if(ch->current->effect_type == 5) {
			goto volume_slide;
		} else {
			break;
		}

	case 4: /* 4xy: Vibrato */
		UPDATE_EFFECT_MEMORY_XY(&ch->vibrato_param,
		                        ch->current->effect_param);
		[[fallthrough]];
	case 6: /* 6xy: Vibrato + Volume slide */
		ch->should_reset_vibrato = true;
		xm_vibrato(ch);
		if(ch->current->effect_type == 6) {
			goto volume_slide;
		} else {
			break;
		}

	case 7: /* 7xy: Tremolo */
		UPDATE_EFFECT_MEMORY_XY(&ch->tremolo_param,
		                        ch->current->effect_param);
		xm_tremolo(ch);
		break;

	volume_slide:
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
			if((ch->current->effect_param & 0x0F) == 0
			   || (ctx->current_tick
			       % (ch->current->effect_param & 0x0F))) break;
			/* XXX: refactor this, this is suspiciously similar to
			   EDy, there is a simpler big picture  */
			ch->volume_envelope_frame_count = 0;
			ch->panning_envelope_frame_count = 0;
			ch->sustained = true;
			xm_trigger_note(ctx, ch);
			xm_tick_envelopes(ch);
			break;

		case 0xC: /* ECy: Note cut */
			/* XXX: test this effect */
			if(ctx->current_tick
			   != (ch->current->effect_param & 0x0F)) break;
			xm_cut_note(ch);
			break;

		case 0xD: /* EDy: Note delay */
			if(ctx->current_tick !=
			   (ch->current->effect_param & 0x0F)) {
				break;
			}
			xm_handle_pattern_slot(ctx, ch);
			/* EDy (y>0) has a weird trigger mechanism, where it
			   will reset sample position and period (except if we
			   have a keyoff), and it will reset envelopes and
			   sustain status but keep volume/panning (so it's not a
			   true instrument trigger) */
			ch->volume_envelope_frame_count = 0;
			ch->panning_envelope_frame_count = 0;
			ch->sustained = true;
			if(!NOTE_IS_KEY_OFF(ch->current->note)) {
				xm_trigger_note(ctx, ch);
			}
			xm_tick_envelopes(ch);
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
		if(ctx->current_tick != ch->current->effect_param) break;
		xm_key_off(ctx, ch);
		break;

	case 25: /* Pxy: Panning slide */
		if(ch->current->effect_param > 0) {
			ch->panning_slide_param = ch->current->effect_param;
		}
		xm_param_slide(&ch->panning, ch->panning_slide_param,
		               MAX_PANNING-1);
		break;

	case 27: /* Rxy: Multi retrig note */
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
		if(ch->tremor_ticks-- == 0) {
			ch->tremor_on = !ch->tremor_on;
			if(ch->tremor_on) {
				ch->tremor_ticks = ch->tremor_param >> 4;
			} else {
				ch->tremor_ticks = ch->tremor_param & 0xF;
			}
		}
		ch->volume_offset = ch->tremor_on ? 0 : MAX_VOLUME;
		break;
	}
}

static float xm_sample_at(const xm_context_t* ctx,
                          const xm_sample_t* sample, uint32_t k) {
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
		/* Smoothly transition between old sample and silence */
		if(ch->frame_count >= RAMPING_POINTS) return 0.f;
		return XM_LERP(ch->end_of_previous_sample[ch->frame_count], .0f,
		               (float)ch->frame_count / (float)RAMPING_POINTS);
		#else
		return 0.f;
		#endif
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
	if(ckd_sub(&ctx->remaining_samples_in_tick,
	           ctx->remaining_samples_in_tick, TICK_SUBSAMPLES)) {
		xm_tick(ctx);
	}

	for(uint8_t i = 0; i < ctx->module.num_channels; ++i, out_lr += 2) {
		__builtin_memset(out_lr, 0, 2 * sizeof(float));
		xm_next_of_channel(ctx, ctx->channels + i,
		                   out_lr, out_lr + 1);

		assert(out_lr[0] <= 1.f);
		assert(out_lr[0] >= -1.f);
		assert(out_lr[1] <= 1.f);
		assert(out_lr[1] >= -1.f);
	}
}

static void xm_sample(xm_context_t* ctx, float* out_left, float* out_right) {
	if(ckd_sub(&ctx->remaining_samples_in_tick,
	           ctx->remaining_samples_in_tick, TICK_SUBSAMPLES)) {
		xm_tick(ctx);
	}

	for(uint8_t i = 0; i < ctx->module.num_channels; ++i) {
		xm_next_of_channel(ctx, ctx->channels + i, out_left, out_right);
	}

	assert(*out_left <= ctx->module.num_channels);
	assert(*out_left >= -ctx->module.num_channels);
	assert(*out_right <= ctx->module.num_channels);
	assert(*out_right >= -ctx->module.num_channels);
}

void xm_generate_samples(xm_context_t* ctx,
                         float* output,
                         uint16_t numsamples) {
	#if XM_TIMING_FUNCTIONS
	ctx->generated_samples += numsamples;
	#endif
	for(uint16_t i = 0; i < numsamples; i++, output += 2) {
		__builtin_memset(output, 0, 2 * sizeof(float));
		xm_sample(ctx, output, output + 1);
	}
}

void xm_generate_samples_noninterleaved(xm_context_t* ctx,
                                        float* out_left, float* out_right,
                                        uint16_t numsamples) {
	#if XM_TIMING_FUNCTIONS
	ctx->generated_samples += numsamples;
	#endif
	for(uint16_t i = 0; i < numsamples; ++i) {
		*out_left = 0.f;
		*out_right = 0.f;
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
