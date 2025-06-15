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

#if HAS_VIBRATO
static bool xm_slot_has_vibrato(const xm_pattern_slot_t*) __attribute__((const)) __attribute__((nonnull));
static void xm_vibrato(xm_channel_context_t*) __attribute__((nonnull));
#endif

#if HAS_EFFECT(EFFECT_TREMOLO)
static void xm_tremolo(xm_channel_context_t*) __attribute__((nonnull));
#endif

#if HAS_EFFECT(EFFECT_MULTI_RETRIG_NOTE)
static void xm_multi_retrig_note(xm_context_t*, xm_channel_context_t*) __attribute__((nonnull));
#endif

#if HAS_EFFECT(EFFECT_ARPEGGIO)
static void xm_arpeggio(const xm_context_t*, xm_channel_context_t*) __attribute__((nonnull));
#endif

#if HAS_TONE_PORTAMENTO
static bool xm_slot_has_tone_portamento(const xm_pattern_slot_t*) __attribute__((const)) __attribute((nonnull));
static void xm_tone_portamento(const xm_context_t*, xm_channel_context_t*) __attribute__((nonnull));
static void xm_tone_portamento_target(const xm_context_t*, xm_channel_context_t*) __attribute__((nonnull));
#endif

static void xm_pitch_slide(xm_channel_context_t*, int16_t) __attribute__((nonnull));
static void xm_param_slide(uint8_t*, uint8_t, uint8_t) __attribute__((nonnull));
static void xm_tick_effects(xm_context_t*, xm_channel_context_t*) __attribute__((nonnull));

static uint8_t xm_envelope_lerp(const xm_envelope_point_t* restrict, const xm_envelope_point_t* restrict, uint16_t) __attribute__((warn_unused_result)) __attribute__((nonnull))  __attribute__((const));
static uint8_t xm_tick_envelope(xm_channel_context_t*, const xm_envelope_t*, uint16_t*) __attribute__((nonnull)) __attribute__((warn_unused_result));
static void xm_tick_envelopes(xm_channel_context_t*) __attribute__((nonnull));

static uint16_t xm_linear_period(int16_t) __attribute__((warn_unused_result)) __attribute__((const));
static uint16_t xm_amiga_period(int16_t) __attribute__((warn_unused_result)) __attribute__((const));
static uint16_t xm_period(const xm_context_t*, int16_t) __attribute__((warn_unused_result)) __attribute__((nonnull))  __attribute__((const));

static uint32_t xm_linear_frequency(uint16_t, uint8_t) __attribute__((warn_unused_result)) __attribute__((nonnull))  __attribute__((const));
static uint32_t xm_amiga_frequency(uint16_t, uint8_t) __attribute__((warn_unused_result)) __attribute__((nonnull))  __attribute__((const));
static uint32_t xm_frequency(const xm_context_t*, const xm_channel_context_t*) __attribute__((warn_unused_result)) __attribute__((nonnull))  __attribute__((const));

#if HAS_GLISSANDO_CONTROL
static void xm_round_linear_period_to_semitone(xm_channel_context_t*) __attribute__((nonnull));
static void xm_round_amiga_period_to_semitone(xm_channel_context_t*) __attribute__((nonnull));
static void xm_round_period_to_semitone(const xm_context_t*, xm_channel_context_t*) __attribute__((nonnull));
#endif

static void xm_handle_pattern_slot(xm_context_t*, xm_channel_context_t*) __attribute__((nonnull));
static void xm_trigger_instrument(xm_context_t*, xm_channel_context_t*) __attribute__((nonnull));
static void xm_trigger_note(xm_context_t*, xm_channel_context_t*) __attribute__((nonnull));
static void xm_cut_note(xm_channel_context_t*) __attribute__((nonnull));
static void xm_key_off(xm_channel_context_t*) __attribute__((nonnull));

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

#if XM_RAMPING
static void XM_SLIDE_TOWARDS(float* val, float goal, float incr) {
	if(*val > goal) {
		*val -= incr;
		XM_CLAMP_DOWN1F(*val, goal);
	} else {
		*val += incr;
		XM_CLAMP_UP1F(*val, goal);
	}
}
#endif

__attribute__((const))
static bool NOTE_IS_KEY_OFF(uint8_t n) {
	static_assert(NOTE_KEY_OFF == 128);
	static_assert(MAX_NOTE < 128);
	static_assert(NOTE_RETRIGGER < 128);
	static_assert(NOTE_SWITCH < 128);
	return n & 128;
}

__attribute__((nonnull)) __attribute__((unused))
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

#if HAS_VIBRATO
static void xm_vibrato(xm_channel_context_t* ch) {
	/* Reset glissando control error */
	xm_pitch_slide(ch, 0);

	/* Depth 8 == 2 semitones amplitude (-1 then +1) */
	ch->vibrato_offset = (int8_t)
		((int16_t)xm_waveform(VIBRATO_CONTROL_PARAM(ch),
		                      ch->vibrato_ticks)
		 * (ch->vibrato_param & 0x0F) / 0x10);
	ch->vibrato_ticks += (ch->vibrato_param >> 4);
}

static bool xm_slot_has_vibrato(const xm_pattern_slot_t* s) {
	return (HAS_EFFECT(EFFECT_VIBRATO) && s->effect_type == EFFECT_VIBRATO)
		|| (HAS_EFFECT(EFFECT_VIBRATO_VOLUME_SLIDE)
		    &&s->effect_type == EFFECT_VIBRATO_VOLUME_SLIDE)
		|| (HAS_VOLUME_EFFECT(VOLUME_EFFECT_VIBRATO)
		    && (VOLUME_COLUMN(s) >> 4) == VOLUME_EFFECT_VIBRATO);
}
#endif

#if HAS_EFFECT(EFFECT_TREMOLO)
static void xm_tremolo(xm_channel_context_t* ch) {
	/* Additive volume effect based on a waveform. Depth 8 is plus or minus
	   32 volume. Works in the opposite direction of vibrato (ie, ramp down
	   means pitch goes down with vibrato, but volume goes up with
	   tremolo.). Tremolo, like vibrato, is not applied on 1st tick of every
	   row (has no effect on Spd=1). */
	/* Like Txy: Tremor, tremolo effect *persists* after the end of the
	   effect, but is reset after any volume command. */

	uint8_t ticks = ch->tremolo_ticks;
	if(TREMOLO_CONTROL_PARAM(ch) & 1) {
		ticks %= 0x40;
		/* FT2 quirk, ramp waveform with tremolo is weird and is also
		   influenced by vibrato ticks... */
		if(ticks >= 0x20) {
			ticks = 0x20 - ticks;
		}
		if(ch->vibrato_ticks % 0x40 >= 0x20) {
			ticks = 0x20 - ticks;
		}
	}
	ch->volume_offset = (int8_t)
		((int16_t)xm_waveform(TREMOLO_CONTROL_PARAM(ch), ticks)
		* (ch->tremolo_param & 0x0F) * 4 / 128);
	ch->tremolo_ticks += (ch->tremolo_param >> 4);
}
#endif

#if HAS_EFFECT(EFFECT_MULTI_RETRIG_NOTE)
static void xm_multi_retrig_note(xm_context_t* ctx, xm_channel_context_t* ch) {
	/* Seems to work similarly to Txy tremor effect. It uses an increasing
	   counter and also runs on tick 0. */

	UPDATE_EFFECT_MEMORY_XY(&ch->multi_retrig_param,
	                        ch->current->effect_param);

	if(VOLUME_COLUMN(ch->current) && ctx->current_tick == 0) {
		/* ??? */
		return;
	}
	if(++ch->multi_retrig_ticks < (ch->multi_retrig_param & 0x0F)) {
		return;
	}
	ch->multi_retrig_ticks = 0;
	xm_trigger_note(ctx, ch);

	/* Fixed volume in volume column always has precedence */
	if((HAS_VOLUME_EFFECT(1) || HAS_VOLUME_EFFECT(2)
	    || HAS_VOLUME_EFFECT(3) || HAS_VOLUME_EFFECT(4)
	    || HAS_VOLUME_EFFECT(5))
	   && VOLUME_COLUMN(ch->current) >= 0x10
	   && VOLUME_COLUMN(ch->current) <= 0x50) {
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
#endif

#if HAS_EFFECT(EFFECT_ARPEGGIO)
static void xm_arpeggio(const xm_context_t* ctx, xm_channel_context_t* ch) {
	uint8_t t = ctx->tempo - ctx->current_tick;

	if(ctx->current_tick == 0 /* This can happen with EEy */
	   || t == 16 /* FT2 overflow quirk */
	   || (t < 16 && t % 3 == 0) /* Normal case */) {
		ch->arp_note_offset = 0;
		return;
	}

	ch->should_reset_arpeggio = true;
	xm_round_period_to_semitone(ctx, ch);

	if(t > 16 /* FT2 overflow quirk */
	   || t % 3 == 2 /* Normal case */) {
		ch->arp_note_offset = ch->current->effect_param & 0x0F;
		return;
	}

	ch->arp_note_offset = ch->current->effect_param >> 4;
}
#endif

#if HAS_TONE_PORTAMENTO
static void xm_tone_portamento([[maybe_unused]] const xm_context_t* ctx,
                               xm_channel_context_t* ch) {
	/* 3xx called without a note, wait until we get an actual
	 * target note. */
	if(ch->tone_portamento_target_period == 0 || ch->period == 0) return;

	uint16_t incr = 4 * ch->tone_portamento_param;
	int32_t diff = ch->tone_portamento_target_period - ch->period;
	diff = diff > incr ? incr : diff;
	diff = diff < (-incr) ? (-incr) : diff;
	xm_pitch_slide(ch, (int16_t)diff);

	#if HAS_GLISSANDO_CONTROL
	if(ch->glissando_control_param) {
		xm_round_period_to_semitone(ctx, ch);
	}
	#endif
}

static bool xm_slot_has_tone_portamento(const xm_pattern_slot_t* s) {
	return (HAS_EFFECT(EFFECT_TONE_PORTAMENTO)
	        && s->effect_type == EFFECT_TONE_PORTAMENTO)
		|| (HAS_EFFECT(EFFECT_TONE_PORTAMENTO_VOLUME_SLIDE)
		    && s->effect_type == EFFECT_TONE_PORTAMENTO_VOLUME_SLIDE)
		|| (HAS_VOLUME_EFFECT(VOLUME_EFFECT_TONE_PORTAMENTO)
		    && (VOLUME_COLUMN(s) >> 4) == VOLUME_EFFECT_TONE_PORTAMENTO);
}

static void xm_tone_portamento_target(const xm_context_t* ctx,
                                      xm_channel_context_t* ch) {
	assert(xm_slot_has_tone_portamento(ch->current));
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
#endif

static void xm_pitch_slide(xm_channel_context_t* ch,
                           int16_t period_offset) {
	/* All pitch slides seem to reset the glissando error, and also cancel
	   any lingering vibrato effect */
	#if HAS_GLISSANDO_CONTROL
	ch->period = (uint16_t)(ch->period + ch->glissando_control_error);
	ch->glissando_control_error = 0;
	#endif

	#if HAS_VIBRATO
	ch->vibrato_offset = 0;
	#endif

	/* Clamp period when sliding up (matches FT2 behaviour), wrap around
	   when sliding down (albeit still in a broken way compared to FT2) */
	ch->period = (ch->period + period_offset < 1)
		? 1 : (uint16_t)(ch->period + period_offset);
}

static void xm_param_slide(uint8_t* param, uint8_t rawval, uint8_t max) {
	/* In FT2, sliding up has precendence for "illegal" slides, eg A1F. */
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
static uint16_t xm_linear_period(int16_t note) {
	assert(7680 - note * 4 > 0);
	assert(7860 - note * 4 < UINT16_MAX);
	return (uint16_t)(7680 - note * 4);
}

static uint32_t xm_linear_frequency(uint16_t period, uint8_t arp_note_offset) {
	if(HAS_EFFECT(EFFECT_ARPEGGIO) && arp_note_offset) {
		/* XXX: test wraparound? */
		period -= (uint16_t)(arp_note_offset * 64);
		/* 1540 is the period of note 95+15/16ths, the maximum
		   FT2 will use for an arpeggio */
		period = period < 1540 ? 1540 : period;
	}
	return (uint32_t)(8363.f * exp2f((4608.f - (float)period) / 768.f));
}

static uint16_t xm_amiga_period(int16_t note) {
	return (uint16_t)(32.f * 856.f * exp2f((float)note / (-12.f * 16.f)));
}

static uint32_t xm_amiga_frequency(uint16_t period, uint8_t arp_note_offset) {
	float p = (float)period;
	if(HAS_EFFECT(EFFECT_ARPEGGIO) && arp_note_offset) {
		p *= exp2f((float)arp_note_offset / (-12.f));
		p = p < 107.f ? 107.f : p;
	}

	/* This is the PAL value. No reason to choose this one over the
	 * NTSC value. */
	return (uint32_t)(4.f * 7093789.2f / (p * 2.f));
}

static uint16_t xm_period([[maybe_unused]] const xm_context_t* ctx,
                          int16_t note) {
	return AMIGA_FREQUENCIES(&ctx->module)
		? xm_amiga_period(note)
		: xm_linear_period(note);
}

static uint32_t xm_frequency([[maybe_unused]] const xm_context_t* ctx,
                             const xm_channel_context_t* ch) {
	assert(ch->period > 0);
	/* XXX: test wraparound/overflow */
	uint16_t period = (uint16_t)(ch->period - VIBRATO_OFFSET(ch)
		- ch->autovibrato_offset);

	return AMIGA_FREQUENCIES(&ctx->module)
		? xm_amiga_frequency(period, ARP_NOTE_OFFSET(ch))
		: xm_linear_frequency(period, ARP_NOTE_OFFSET(ch));
}

#if HAS_GLISSANDO_CONTROL
static void xm_round_linear_period_to_semitone(xm_channel_context_t* ch) {
	/* With linear frequencies, 1 semitone is 64 period units and 16
	   finetune units. */
	uint16_t new_period = (uint16_t)
		(((ch->period + ch->finetune * 4 + 32) & 0xFFC0)
		 - ch->finetune * 4);
	ch->glissando_control_error = (int8_t)(ch->period - new_period);
	ch->period = new_period;
}

static void xm_round_amiga_period_to_semitone([[maybe_unused]] xm_channel_context_t* ch) {
	/* XXX: todo */
}

/* Round period to nearest semitone. Store rounding error in
   ch->glissando_control_error. */
static void xm_round_period_to_semitone([[maybe_unused]] const xm_context_t* ctx,
                                        xm_channel_context_t* ch) {
	/* Reset glissando control error */
	xm_pitch_slide(ch, 0);

	if(AMIGA_FREQUENCIES(&ctx->module)) {
		TRACE();
		xm_round_amiga_period_to_semitone(ch);
	} else {
		xm_round_linear_period_to_semitone(ch);
	}
}
#endif

static void xm_handle_pattern_slot(xm_context_t* ctx, xm_channel_context_t* ch) {
	xm_pattern_slot_t* s = ch->current;

	if(s->instrument) {
		/* Always update next_instrument, even with a key-off note. */
		ch->next_instrument = s->instrument;
	}

	if(!NOTE_IS_KEY_OFF(s->note)) {
		if(s->note) {
			if(s->note <= MAX_NOTE) {
				#if HAS_TONE_PORTAMENTO
				if(xm_slot_has_tone_portamento(ch->current)) {
					/* Orig note (used for retriggers) is
					   not updated by tone portas */
					xm_tone_portamento_target(ctx, ch);
				} else
				#endif
				{
					ch->orig_note = s->note;
					xm_trigger_note(ctx, ch);
				}
			} else {
				xm_trigger_note(ctx, ch);
			}
		}
	} else {
		xm_key_off(ch);
	}

	if(s->instrument) {
		if(ch->sample) {
			ch->volume = ch->sample->volume;
			ch->panning = ch->sample->panning;
		}
		if(!NOTE_IS_KEY_OFF(s->note)) {
			xm_trigger_instrument(ctx, ch);
		}
	}

	/* These volume effects always work, even when called with a delay by
	   EDy. */
	if((HAS_VOLUME_EFFECT(1) || HAS_VOLUME_EFFECT(2)
	    || HAS_VOLUME_EFFECT(3) || HAS_VOLUME_EFFECT(4)
	    || HAS_VOLUME_EFFECT(5))
	   && VOLUME_COLUMN(s) >= 0x10 && VOLUME_COLUMN(s) <= 0x50) {
		/* Set volume */
		RESET_VOLUME_OFFSET(ch);
		ch->volume = (uint8_t)(VOLUME_COLUMN(s) - 0x10);
	}
	if(HAS_VOLUME_EFFECT(VOLUME_EFFECT_SET_PANNING)
	   && VOLUME_COLUMN(s) >> 4 == VOLUME_EFFECT_SET_PANNING) {
		ch->panning = VOLUME_COLUMN(s) << 4;
	}

	#if HAS_TONE_PORTAMENTO
	/* Set tone portamento memory (even on tick 0) */
	if(HAS_VOLUME_EFFECT(VOLUME_EFFECT_TONE_PORTAMENTO)
	   && VOLUME_COLUMN(s) >> 4 == VOLUME_EFFECT_TONE_PORTAMENTO) {
		/* Mx *always* has precedence, even M0 */
		if(VOLUME_COLUMN(s) & 0x0F) {
			ch->tone_portamento_param = VOLUME_COLUMN(s) << 4;
		}
	} else if(HAS_EFFECT(EFFECT_TONE_PORTAMENTO)
	          && s->effect_type == EFFECT_TONE_PORTAMENTO) {
		if(s->effect_param > 0) {
			ch->tone_portamento_param = s->effect_param;
		}
	}
	#endif

	if(ctx->current_tick == 0) {
		/* These effects are ONLY applied at tick 0. If a note delay
		   effect (EDy), where y>0, uses this effect in its volume
		   column, it will be ignored. */

		switch(VOLUME_COLUMN(s) >> 4) {

		#if HAS_VOLUME_EFFECT(VOLUME_EFFECT_FINE_SLIDE_DOWN)
		case VOLUME_EFFECT_FINE_SLIDE_DOWN:
			RESET_VOLUME_OFFSET(ch);
			xm_param_slide(&ch->volume, VOLUME_COLUMN(s) & 0x0F,
			               MAX_VOLUME);
			break;
		#endif

		#if HAS_VOLUME_EFFECT(VOLUME_EFFECT_FINE_SLIDE_UP)
		case VOLUME_EFFECT_FINE_SLIDE_UP:
			RESET_VOLUME_OFFSET(ch);
			xm_param_slide(&ch->volume, VOLUME_COLUMN(s) << 4,
			               MAX_VOLUME);
			break;
		#endif

		#if HAS_VOLUME_EFFECT(VOLUME_EFFECT_VIBRATO_SPEED)
		case VOLUME_EFFECT_VIBRATO_SPEED:
			/* XXX: test me (with note delay, with simultaneous
			   4xy/40y) */
			/* S0 does nothing, but is deleted in load.c */
			UPDATE_EFFECT_MEMORY_XY(&ch->vibrato_param,
			                        VOLUME_COLUMN(s) << 4);
			break;
		#endif

		}
	}

	switch(s->effect_type) {

	#if HAS_EFFECT(EFFECT_SET_VOLUME)
	case EFFECT_SET_VOLUME:
		RESET_VOLUME_OFFSET(ch);
		/* xx > MAX_VOLUME is already clamped in load.c */
		ch->volume = s->effect_param;
		break;
	#endif

	#if HAS_EFFECT(EFFECT_FINE_VOLUME_SLIDE_UP)
	case EFFECT_FINE_VOLUME_SLIDE_UP:
		if(s->effect_param) {
			ch->fine_volume_slide_up_param = s->effect_param << 4;
		}
		RESET_VOLUME_OFFSET(ch);
		xm_param_slide(&ch->volume,
		               ch->fine_volume_slide_up_param,
		               MAX_VOLUME);
		break;
	#endif

	#if HAS_EFFECT(EFFECT_FINE_VOLUME_SLIDE_DOWN)
	case EFFECT_FINE_VOLUME_SLIDE_DOWN:
		if(s->effect_param) {
			ch->fine_volume_slide_down_param = s->effect_param;
		}
		RESET_VOLUME_OFFSET(ch);
		xm_param_slide(&ch->volume,
		               ch->fine_volume_slide_down_param,
		               MAX_VOLUME);
		break;
	#endif

	#if HAS_EFFECT(EFFECT_SET_PANNING)
	case EFFECT_SET_PANNING:
		ch->panning = s->effect_param;
		break;
	#endif

	#if HAS_EFFECT(EFFECT_JUMP_TO_ORDER)
	case EFFECT_JUMP_TO_ORDER:
		ctx->position_jump = true;
		ctx->jump_dest = s->effect_param;
		ctx->jump_row = 0;
		break;
	#endif

	#if HAS_EFFECT(EFFECT_PATTERN_BREAK)
	case EFFECT_PATTERN_BREAK:
		/* Jump after playing this line */
		ctx->pattern_break = true;
		ctx->jump_row = (uint8_t)
			(s->effect_param - 6 * (s->effect_param >> 4));
		break;
	#endif

	#if HAS_EFFECT(EFFECT_SET_TEMPO)
	case EFFECT_SET_TEMPO:
		ctx->tempo = s->effect_param;
		break;
	#endif

	#if HAS_EFFECT(EFFECT_SET_BPM)
	case EFFECT_SET_BPM:
		ctx->bpm = s->effect_param;
		break;
	#endif

	#if HAS_EFFECT(EFFECT_SET_GLOBAL_VOLUME)
	case EFFECT_SET_GLOBAL_VOLUME:
		/* xx > MAX_VOLUME is already clamped in load.c */
		ctx->global_volume = s->effect_param;
		break;
	#endif

	#if HAS_EFFECT(EFFECT_SET_ENVELOPE_POSITION)
	case EFFECT_SET_ENVELOPE_POSITION:
		ch->volume_envelope_frame_count = s->effect_param;
		ch->panning_envelope_frame_count = s->effect_param;
		break;
	#endif

	#if HAS_EFFECT(EFFECT_MULTI_RETRIG_NOTE)
	case EFFECT_MULTI_RETRIG_NOTE:
		xm_multi_retrig_note(ctx, ch);
		break;
	#endif

	#if HAS_EFFECT(EFFECT_EXTRA_FINE_PORTAMENTO_UP)
	case EFFECT_EXTRA_FINE_PORTAMENTO_UP:
		if(s->effect_param & 0x0F) {
			ch->extra_fine_portamento_up_param = s->effect_param;
		}
		xm_pitch_slide(ch, -ch->extra_fine_portamento_up_param);
		break;
	#endif

	#if HAS_EFFECT(EFFECT_EXTRA_FINE_PORTAMENTO_DOWN)
	case EFFECT_EXTRA_FINE_PORTAMENTO_DOWN:
		if(s->effect_param) {
			ch->extra_fine_portamento_down_param = s->effect_param;
		}
		xm_pitch_slide(ch, ch->extra_fine_portamento_down_param);
		break;
	#endif

	#if HAS_EFFECT(EFFECT_FINE_PORTAMENTO_UP)
	case EFFECT_FINE_PORTAMENTO_UP:
		if(s->effect_param) {
			ch->fine_portamento_up_param = 4 * s->effect_param;
		}
		xm_pitch_slide(ch, -ch->fine_portamento_up_param);
		break;
	#endif

	#if HAS_EFFECT(EFFECT_FINE_PORTAMENTO_DOWN)
	case EFFECT_FINE_PORTAMENTO_DOWN:
		if(s->effect_param) {
			ch->fine_portamento_down_param = 4 * s->effect_param;
		}
		xm_pitch_slide(ch, ch->fine_portamento_down_param);
		break;
	#endif

	#if HAS_EFFECT(EFFECT_SET_GLISSANDO_CONTROL)
	case EFFECT_SET_GLISSANDO_CONTROL:
		ch->glissando_control_param = s->effect_param;
		break;
	#endif

	#if HAS_VIBRATO && HAS_EFFECT(EFFECT_SET_VIBRATO_CONTROL)
	case EFFECT_SET_VIBRATO_CONTROL:
		ch->vibrato_control_param = s->effect_param;
		break;
	#endif

	#if HAS_EFFECT(EFFECT_TREMOLO) && HAS_EFFECT(EFFECT_SET_TREMOLO_CONTROL)
	case EFFECT_SET_TREMOLO_CONTROL:
		ch->tremolo_control_param = s->effect_param;
		break;
	#endif

	#if HAS_EFFECT(EFFECT_PATTERN_LOOP)
	case EFFECT_PATTERN_LOOP:
		if(s->effect_param) {
			if(s->effect_param == ch->pattern_loop_count) {
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
	#endif

	#if HAS_EFFECT(EFFECT_DELAY_PATTERN)
	case EFFECT_DELAY_PATTERN:
		/* Loop current row y times. Tick effects *are* applied
		   on tick 0 of repeated rows. */
		ctx->extra_rows = ch->current->effect_param;
		break;
	#endif

	}
}

static void xm_trigger_instrument([[maybe_unused]] xm_context_t* ctx,
                                  xm_channel_context_t* ch) {
	ch->sustained = true;
	ch->volume_envelope_frame_count = 0;
	ch->panning_envelope_frame_count = 0;

	#if HAS_EFFECT(EFFECT_MULTI_RETRIG_NOTE)
	ch->multi_retrig_ticks = 0;
	#endif

	#if HAS_EFFECT(EFFECT_TREMOR)
	ch->tremor_ticks = 0;
	#endif

	ch->autovibrato_ticks = 0;
	RESET_VOLUME_OFFSET(ch);

	#if HAS_VIBRATO
	if(!(VIBRATO_CONTROL_PARAM(ch) & 4)) {
		ch->vibrato_ticks = 0;
	}
	#endif

	#if HAS_EFFECT(EFFECT_TREMOLO)
	if(!(TREMOLO_CONTROL_PARAM(ch) & 4)) {
		ch->tremolo_ticks = 0;
	}
	#endif

	#if XM_TIMING_FUNCTIONS
	ch->latest_trigger = ctx->generated_samples;
	if(ch->instrument) {
		ch->instrument->latest_trigger = ctx->generated_samples;
	}
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

	if(ch->current->note == NOTE_SWITCH) {
		return;
	}

	/* Update period */
	int16_t note = (int16_t)(ch->orig_note + ch->sample->relative_note);
	if(note <= 0 || note >= 120) {
		ch->period = 0;
		return;
	}

	/* Handle E5y: Set note fine-tune here; this effect only works in tandem
	   with a note and overrides the finetune value stored in the sample. If
	   we have Mx in the volume column, it does nothing. */
	if(HAS_EFFECT(EFFECT_SET_FINETUNE)
	   && ch->current->effect_type == EFFECT_SET_FINETUNE) {
		ch->finetune = (int8_t)
			(ch->current->effect_param * 2 - 16);
	} else {
		ch->finetune = ch->sample->finetune;
	}
	ch->period = xm_period(ctx, (int16_t)(16 * (note - 1) + ch->finetune));

	/* Handle 9xx: Sample offset here, since it does nothing outside of a
	   note trigger (ie, called on its own without a note). If we have Mx in
	   the volume column, it does nothing. */
	if(HAS_EFFECT(EFFECT_SET_SAMPLE_OFFSET)
	   && ch->current->effect_type == EFFECT_SET_SAMPLE_OFFSET) {
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

	#if HAS_GLISSANDO_CONTROL
	ch->glissando_control_error = 0;
	#endif

	#if HAS_VIBRATO
	ch->vibrato_offset = 0;
	#endif

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

static void xm_key_off(xm_channel_context_t* ch) {
	/* Key Off */
	ch->sustained = false;

	/* If no volume envelope is used, also cut the note */
	if(ch->instrument == NULL
	   || ch->instrument->volume_envelope.num_points == 0) {
		xm_cut_note(ch);
	}
}

static void xm_row(xm_context_t* ctx) {
	if(POSITION_JUMP(ctx) || PATTERN_BREAK(ctx)) {
		#if HAS_POSITION_JUMP
		if(POSITION_JUMP(ctx)) {
			ctx->current_table_index = ctx->jump_dest;
		} else
		#endif
		ctx->current_table_index++;

		#if HAS_EFFECT(EFFECT_PATTERN_BREAK)
		ctx->pattern_break = false;
		#endif

		#if HAS_POSITION_JUMP
		ctx->position_jump = false;
		#endif

		#if HAS_JUMP_ROW
		ctx->current_row = ctx->jump_row;
		ctx->jump_row = 0;
		#endif

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

		if(!HAS_EFFECT(EFFECT_DELAY_NOTE)
		   || s->effect_type != EFFECT_DELAY_NOTE) {
			xm_handle_pattern_slot(ctx, ch);
		}

		#if HAS_EFFECT(EFFECT_PATTERN_LOOP)
		if(ch->pattern_loop_count > 0) {
			in_a_loop = true;
		}
		#endif

		#if HAS_EFFECT(EFFECT_ARPEGGIO)
		if(ch->should_reset_arpeggio) {
			/* Reset glissando control error */
			xm_pitch_slide(ch, 0);
			ch->should_reset_arpeggio = false;
			ch->arp_note_offset = 0;
		}
		#endif

		#if HAS_VIBRATO
		if(SHOULD_RESET_VIBRATO(ch)
		   && !xm_slot_has_vibrato(ch->current)) {
			ch->vibrato_offset = 0;
		}
		#endif
	}

	if(!in_a_loop) {
		/* No E6y loop is in effect (or we are in the first pass) */
		ctx->loop_count = (ctx->row_loop_count[MAX_ROWS_PER_PATTERN * ctx->current_table_index + ctx->current_row]++);
	}

	ctx->current_row++; /* Since this is an uint8, this line can
	                     * increment from 255 to 0, in which case it
	                     * is still necessary to go the next
	                     * pattern. */
	if(!POSITION_JUMP(ctx) && !PATTERN_BREAK(ctx) &&
	   (ctx->current_row >= cur->num_rows || ctx->current_row == 0)) {
		ctx->current_table_index++;

		#if HAS_JUMP_ROW
		/* This will be 0 most of the time, except when E60 is used */
		ctx->current_row = ctx->jump_row;
		ctx->jump_row = 0;
		#else
		ctx->current_row = 0;
		#endif

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
	#if HAS_EFFECT(EFFECT_DELAY_PATTERN)
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
	#else
	if(ctx->current_tick == 0 || ctx->current_tick >= ctx->tempo) {
		ctx->current_tick = 0;
		xm_row(ctx);
	}
	#endif

	for(uint8_t i = 0; i < ctx->module.num_channels; ++i) {
		xm_channel_context_t* ch = ctx->channels + i;

		xm_tick_envelopes(ch);

		if(ctx->current_tick || EXTRA_ROWS_DONE(ctx)) {
			xm_tick_effects(ctx, ch);
		}

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
		assert(VOLUME_OFFSET(ch) >= -MAX_VOLUME
		       && VOLUME_OFFSET(ch) <= MAX_VOLUME);

		static_assert(MAX_VOLUME == 1<<6);
		static_assert(MAX_ENVELOPE_VALUE == 1<<6);
		static_assert(MAX_FADEOUT_VOLUME == 1<<15);

		/* 6 + 6 + 15 - 2 + 6 => 31 bits of range */
		int32_t base = ch->volume - VOLUME_OFFSET(ch);
		if(base < 0) base = 0;
		else if(base > MAX_VOLUME) base = MAX_VOLUME;
		base *= ch->volume_envelope_volume;
		base *= ch->fadeout_volume;
		base /= 4;
		base *= GLOBAL_VOLUME(ctx);
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
	switch(VOLUME_COLUMN(ch->current) >> 4) {

	#if HAS_VOLUME_EFFECT(VOLUME_EFFECT_SLIDE_DOWN)
	case VOLUME_EFFECT_SLIDE_DOWN:
		RESET_VOLUME_OFFSET(ch);
		xm_param_slide(&ch->volume, VOLUME_COLUMN(ch->current) & 0x0F,
		               MAX_VOLUME);
		break;
	#endif

	#if HAS_VOLUME_EFFECT(VOLUME_EFFECT_SLIDE_UP)
	case VOLUME_EFFECT_SLIDE_UP:
		RESET_VOLUME_OFFSET(ch);
		xm_param_slide(&ch->volume, VOLUME_COLUMN(ch->current) << 4,
		               MAX_VOLUME);
		break;
	#endif

	#if HAS_VOLUME_EFFECT(VOLUME_EFFECT_VIBRATO)
	case VOLUME_EFFECT_VIBRATO:
		UPDATE_EFFECT_MEMORY_XY(&ch->vibrato_param,
		                        VOLUME_COLUMN(ch->current) & 0x0F);
		/* This vibrato *does not* reset pitch when the command
		   is discontinued */
		#if HAS_VIBRATO_RESET
		ch->should_reset_vibrato = false;
		#endif
		xm_vibrato(ch);
		break;
	#endif

	#if HAS_VOLUME_EFFECT(VOLUME_EFFECT_PANNING_SLIDE_LEFT)
	case VOLUME_EFFECT_PANNING_SLIDE_LEFT:
		xm_param_slide(&ch->panning, VOLUME_COLUMN(ch->current) & 0x0F,
		               MAX_PANNING-1);
		break;
	#endif

	#if HAS_VOLUME_EFFECT(VOLUME_EFFECT_PANNING_SLIDE_RIGHT)
	case VOLUME_EFFECT_PANNING_SLIDE_RIGHT:
		xm_param_slide(&ch->panning, VOLUME_COLUMN(ch->current) << 4,
		               MAX_PANNING-1);
		break;
	#endif

	#if HAS_VOLUME_EFFECT(VOLUME_EFFECT_TONE_PORTAMENTO)
	case VOLUME_EFFECT_TONE_PORTAMENTO:
		xm_tone_portamento(ctx, ch);
		break;
	#endif

	}

	switch(ch->current->effect_type) {

	#if HAS_EFFECT(EFFECT_ARPEGGIO)
	case EFFECT_ARPEGGIO:
		if(ch->current->effect_param == 0) break;
		xm_arpeggio(ctx, ch);
		break;
	#endif

	#if HAS_EFFECT(EFFECT_PORTAMENTO_UP)
	case EFFECT_PORTAMENTO_UP:
		if(ch->current->effect_param > 0) {
			ch->portamento_up_param = ch->current->effect_param;
		}
		xm_pitch_slide(ch, -4 * ch->portamento_up_param);
		break;
	#endif

	#if HAS_EFFECT(EFFECT_PORTAMENTO_DOWN)
	case EFFECT_PORTAMENTO_DOWN:
		if(ch->current->effect_param > 0) {
			ch->portamento_down_param = ch->current->effect_param;
		}
		xm_pitch_slide(ch, 4 * ch->portamento_down_param);
		break;
	#endif

	/* XXX: is there a better way to do this? */
	#if HAS_EFFECT(TONE_PORTAMENTO) \
		&& HAS_EFFECT(EFFECT_TONE_PORTAMENTO_VOLUME_SLIDE)
	case EFFECT_TONE_PORTAMENTO:
		[[fallthrough]];
	case EFFECT_TONE_PORTAMENTO_VOLUME_SLIDE:
		xm_tone_portamento(ctx, ch);
		if(ch->current->effect_type
		   == EFFECT_TONE_PORTAMENTO_VOLUME_SLIDE) {
			goto volume_slide;
		} else {
			break;
		}
	#elif HAS_EFFECT(EFFECT_TONE_PORTAMENTO_VOLUME_SLIDE)
	case TONE_PORTAMENTO_VOLUME_SLIDE:
		xm_tone_portamento(ctx, ch);
		goto volume_slide;
	#elif HAS_EFFECT(EFFECT_TONE_PORTAMENTO)
	case EFFECT_TONE_PORTAMENTO:
		xm_tone_portamento(ctx, ch);
		break;
	#endif

	#if HAS_EFFECT(EFFECT_VIBRATO) && HAS_EFFECT(EFFECT_VIBRATO_VOLUME_SLIDE)
	case EFFECT_VIBRATO:
		UPDATE_EFFECT_MEMORY_XY(&ch->vibrato_param,
		                        ch->current->effect_param);
		[[fallthrough]];
	case EFFECT_VIBRATO_VOLUME_SLIDE:
		#if HAS_VIBRATO_RESET
		ch->should_reset_vibrato = true;
		#endif
		xm_vibrato(ch);
		if(ch->current->effect_type == EFFECT_VIBRATO_VOLUME_SLIDE) {
			goto volume_slide;
		} else {
			break;
		}
	#elif HAS_EFFECT(EFFECT_VIBRATO_VOLUME_SLIDE)
	case EFFECT_VIBRATO_VOLUME_SLIDE:
		#if HAS_VIBRATO_RESET
		ch->should_reset_vibrato = true;
		#endif
		xm_vibrato(ch);
		goto volume_slide;
	#elif HAS_EFFECT(EFFECT_VIBRATO)
	case EFFECT_VIBRATO:
		#if HAS_VIBRATO_RESET
		ch->should_reset_vibrato = true;
		#endif
		xm_vibrato(ch);
		break;
	#endif

	#if HAS_VOLUME_SLIDE
	#if HAS_EFFECT(EFFECT_VIBRATO_VOLUME_SLIDE) \
		|| HAS_EFFECT(EFFECT_TONE_PORTAMENTO_VOLUME_SLIDE)
	volume_slide:
	#endif
	#if HAS_EFFECT(EFFECT_VOLUME_SLIDE)
	case EFFECT_VOLUME_SLIDE:
	#endif
		if(ch->current->effect_param > 0) {
			ch->volume_slide_param = ch->current->effect_param;
		}
		RESET_VOLUME_OFFSET(ch);
		xm_param_slide(&ch->volume, ch->volume_slide_param, MAX_VOLUME);
		break;
	#endif

	#if HAS_EFFECT(EFFECT_TREMOLO)
	case EFFECT_TREMOLO:
		UPDATE_EFFECT_MEMORY_XY(&ch->tremolo_param,
		                        ch->current->effect_param);
		xm_tremolo(ch);
		break;
	#endif

	#if HAS_EFFECT(EFFECT_GLOBAL_VOLUME_SLIDE)
	case EFFECT_GLOBAL_VOLUME_SLIDE:
		if(ch->current->effect_param > 0) {
			ch->global_volume_slide_param = ch->current->effect_param;
		}
		xm_param_slide(&ctx->global_volume,
		               ch->global_volume_slide_param, MAX_VOLUME);
		break;
	#endif

	#if HAS_EFFECT(EFFECT_KEY_OFF)
	case EFFECT_KEY_OFF:
		if(ctx->current_tick != ch->current->effect_param) break;
		xm_key_off(ch);
		break;
	#endif

	#if HAS_EFFECT(EFFECT_PANNING_SLIDE)
	case EFFECT_PANNING_SLIDE:
		if(ch->current->effect_param > 0) {
			ch->panning_slide_param = ch->current->effect_param;
		}
		xm_param_slide(&ch->panning, ch->panning_slide_param,
		               MAX_PANNING-1);
		break;
	#endif

	#if HAS_EFFECT(EFFECT_MULTI_RETRIG_NOTE)
	case EFFECT_MULTI_RETRIG_NOTE:
		xm_multi_retrig_note(ctx, ch);
		break;
	#endif

	#if HAS_EFFECT(EFFECT_TREMOR)
	case EFFECT_TREMOR:
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
	#endif

	#if HAS_EFFECT(EFFECT_RETRIGGER_NOTE)
	case EFFECT_RETRIGGER_NOTE:
		assert(ch->current->effect_param > 0);
		if(ctx->current_tick % ch->current->effect_param) break;
		xm_trigger_instrument(ctx, ch);
		xm_trigger_note(ctx, ch);
		xm_tick_envelopes(ch);
		break;
	#endif

	#if HAS_EFFECT(EFFECT_CUT_NOTE)
	case EFFECT_CUT_NOTE:
		if(ctx->current_tick != ch->current->effect_param) break;
		xm_cut_note(ch);
		break;
	#endif

	#if HAS_EFFECT(EFFECT_DELAY_NOTE)
	case EFFECT_DELAY_NOTE:
		if(ctx->current_tick != ch->current->effect_param) {
			break;
		}
		xm_handle_pattern_slot(ctx, ch);
		xm_trigger_instrument(ctx, ch);
		if(!NOTE_IS_KEY_OFF(ch->current->note)) {
			xm_trigger_note(ctx, ch);
		}
		xm_tick_envelopes(ch);
		break;
	#endif

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
	const xm_sample_t* smp = ch->sample;
	assert(smp == NULL || smp->loop_length <= smp->length);

	/* Zero-length samples are also handled here, since loop_length will
	   always be zero for these */
	if(smp == NULL
	   || (smp->loop_length == 0
	       && ch->sample_position >= smp->length * SAMPLE_MICROSTEPS)) {
		#if XM_RAMPING
		/* Smoothly transition between old sample and silence */
		if(ch->frame_count >= RAMPING_POINTS) return 0.f;
		return XM_LERP(ch->end_of_previous_sample[ch->frame_count], .0f,
		               (float)ch->frame_count / (float)RAMPING_POINTS);
		#else
		return 0.f;
		#endif
	}

	if(smp->loop_length
	   && ch->sample_position >= smp->length * SAMPLE_MICROSTEPS) {
		/* Remove extra loops. For ping-pong logic, the loop length is
		   doubled, and the second half is the reverse of the looped
		   part. */
		uint32_t off = (uint32_t)
			((smp->length - smp->loop_length) * SAMPLE_MICROSTEPS);
		ch->sample_position -= off;
		ch->sample_position %= smp->ping_pong
			? smp->loop_length * SAMPLE_MICROSTEPS * 2
			: smp->loop_length * SAMPLE_MICROSTEPS;
		ch->sample_position += off;
	}

	uint32_t a = ch->sample_position / SAMPLE_MICROSTEPS;
	uint32_t b;

	#if XM_LINEAR_INTERPOLATION
	const float t = (float)
		(ch->sample_position % SAMPLE_MICROSTEPS)
		/ (float)SAMPLE_MICROSTEPS;
	#endif

	/* Find the next sample point (for linear interpolation only) and also
	   apply ping-pong logic */
	if(smp->loop_length == 0) {
		b = (a+1 < ch->sample->length) ? (a+1) : a;
	} else if(!smp->ping_pong) {
		b = (a+1 == smp->length) ?
			smp->length - smp->loop_length : (a+1);
	} else {
		if(a < smp->length) {
			/* In the first half of the loop, go forwards */
			b = (a+1 == smp->length) ? a : (a+1);
		} else {
			/* In the second half of the loop, go backwards */
			/* loop_end -> loop_end - 1 */
			/* loop_end + 1 -> loop_end - 2 */
			/* etc. */
			/* loop_end + loop_length - 1 -> loop_start */
			a = smp->length * 2 - 1 - a;
			b = (a == smp->length - smp->loop_length) ? a : (a-1);
			assert(a >= smp->length - smp->loop_length);
			assert(b >= smp->length - smp->loop_length);
		}
	}

	assert(a < smp->length);
	assert(b < smp->length);
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

	ch->sample_position += ch->step;
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
