/* Author: Romain "Artefact2" Dalmaso <artefact2@gmail.com> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#include "xm_internal.h"

/* ----- Static functions ----- */

static void xm_arpeggio(xm_context_t*, xm_channel_context_t*, uint8_t, int16_t);
static void xm_tone_portamento(xm_context_t*, xm_channel_context_t*);
static void xm_pitch_slide(xm_context_t*, xm_channel_context_t*, float);
static void xm_panning_slide(xm_channel_context_t*, uint8_t);
static void xm_volume_slide(xm_channel_context_t*, uint8_t);

static float xm_envelope_lerp(xm_envelope_point_t*, xm_envelope_point_t*, uint16_t);
static void xm_envelope_tick(xm_channel_context_t*, xm_envelope_t*, uint16_t*, float*);

static void xm_post_pattern_change(xm_context_t*);
static void xm_update_step(xm_context_t*, xm_channel_context_t*, float, bool, bool);

static void xm_handle_note_and_instrument(xm_context_t*, xm_channel_context_t*, xm_pattern_slot_t*);
static void xm_trigger_note(xm_context_t*, xm_channel_context_t*);
static void xm_cut_note(xm_channel_context_t*);
static void xm_key_off(xm_channel_context_t*);

static void xm_row(xm_context_t*);
static void xm_tick(xm_context_t*);
static void xm_sample(xm_context_t*, float*, float*);

/* ----- Other oddities ----- */

static const float multi_retrig_add[] = {
	 0.f,  -1.f,  -2.f,  -4.f,  /* 0, 1, 2, 3 */
	-8.f, -16.f,   0.f,   0.f,  /* 4, 5, 6, 7 */
	 0.f,   1.f,   2.f,   4.f,  /* 8, 9, A, B */
	 8.f,  16.f,   0.f,   0.f   /* C, D, E, F */
};

static const float multi_retrig_multiply[] = {
	1.f,   1.f,  1.f,        1.f,  /* 0, 1, 2, 3 */
	1.f,   1.f,   .6666667f,  .5f, /* 4, 5, 6, 7 */
	1.f,   1.f,  1.f,        1.f,  /* 8, 9, A, B */
	1.f,   1.f,  1.5f,       2.f   /* C, D, E, F */
};

#define XM_PERIOD_OF_NOTE(note) (7680.f - (note) * 64.f)
#define XM_LINEAR_FREQUENCY_OF_PERIOD(period) (8363.f * powf(2.f, (4608.f - (period)) / 768.f))

#define HAS_TONE_PORTAMENTO(s) ((s)->effect_type == 3 || (s)->effect_type == 5 || ((s)->volume_column >> 4) == 0xF)

/* ----- Function definitions ----- */

static void xm_arpeggio(xm_context_t* ctx, xm_channel_context_t* ch, uint8_t param, int16_t tick) {
	switch(tick % 3) {
	case 0:
		ch->arp_in_progress = false;
		xm_update_step(ctx, ch, ch->note, true, true);
		break;
	case 2:
		ch->arp_in_progress = true;
		xm_update_step(ctx, ch, ch->note + (param >> 4), true, true);
		break;
	case 1:
		ch->arp_in_progress = true;
		xm_update_step(ctx, ch, ch->note + (param & 0x0F), true, true);
		break;
	}
}

static void xm_tone_portamento(xm_context_t* ctx, xm_channel_context_t* ch) {
	if(ch->period > ch->tone_portamento_target_period) {
		ch->period -= 4.0f * ch->tone_portamento_param;

		if(ch->period < ch->tone_portamento_target_period) {
			ch->period = ch->tone_portamento_target_period;
		}
	} else if(ch->period < ch->tone_portamento_target_period) {
		ch->period += 4.0f * ch->tone_portamento_param;

		if(ch->period > ch->tone_portamento_target_period) {
			ch->period = ch->tone_portamento_target_period;
		}
	} else {
		return;
	}

	xm_update_step(ctx, ch, ch->note, false, true);
}

static void xm_pitch_slide(xm_context_t* ctx, xm_channel_context_t* ch, float period_offset) {
	ch->period += period_offset;
	if(ch->period < 0) ch->period = 0;
	/* XXX: upper bound of period ? */

	xm_update_step(ctx, ch, ch->note, false, true);
}

static void xm_panning_slide(xm_channel_context_t* ch, uint8_t rawval) {
	float f;

	if((rawval & 0xF0) && (rawval & 0x0F)) {
		/* Illegal state */
		return;
	}

	if(rawval & 0xF0) {
		/* Slide right */
		f = (float)(rawval >> 4) / (float)0xFF;
		ch->panning += f;
		if(ch->panning > 1) ch->panning = 1;
	} else {
		/* Slide left */
		f = (float)(rawval & 0x0F) / (float)0xFF;
		ch->panning -= f;
		if(ch->panning < 0) ch->panning = 0;
	}
}

static void xm_volume_slide(xm_channel_context_t* ch, uint8_t rawval) {
	float f;

	if((rawval & 0xF0) && (rawval & 0x0F)) {
		/* Illegal state */
		return;
	}

	if(rawval & 0xF0) {
		/* Slide up */
		f = (float)(rawval >> 4) / (float)0x40;
		ch->volume += f;
		if(ch->volume > 1) ch->volume = 1;
	} else {
		/* Slide down */
		f = (float)(rawval & 0x0F) / (float)0x40;
		ch->volume -= f;
		if(ch->volume < 0) ch->volume = 0;
	}
}

static float xm_envelope_lerp(xm_envelope_point_t* restrict a, xm_envelope_point_t* restrict b, uint16_t pos) {
	/* Linear interpolation between two envelope points */
	if(pos <= a->frame) return a->value;
	else if(pos >= b->frame) return b->value;
	else {
		float p = (float)(pos - a->frame) / (float)(b->frame - a->frame);
		return a->value * (1 - p) + b->value * p;
	}
}

static void xm_post_pattern_change(xm_context_t* ctx) {
	/* Loop if necessary */
	if(ctx->current_table_index >= ctx->module.length) {
		ctx->current_table_index = ctx->module.restart_position;
	}
}

static void xm_update_step(xm_context_t* ctx, xm_channel_context_t* ch, float note, bool update_period, bool update_frequency) {
	/* XXX Amiga frequencies? */

	if(update_period) {
		ch->period = XM_PERIOD_OF_NOTE(note);
	}

	if(update_frequency) {
		ch->frequency = XM_LINEAR_FREQUENCY_OF_PERIOD(ch->period);
	}

	ch->step = ch->frequency / ctx->rate;
}

static void xm_handle_note_and_instrument(xm_context_t* ctx, xm_channel_context_t* ch,
										  xm_pattern_slot_t* s) {
	if(s->instrument > 0) {
		if(HAS_TONE_PORTAMENTO(s) && ch->instrument != NULL && ch->sample != NULL) {
			/* Tone portamento in effect, unclear stuff happens */
			float old_sample_pos, old_period;
			old_sample_pos = ch->sample_position;
			old_period = ch->period;
			if(ch->note == 0) ch->note = 1;
			xm_trigger_note(ctx, ch);
			ch->period = old_period;
			ch->sample_position = old_sample_pos;
			xm_update_step(ctx, ch, ch->note, false, true);
		} else if(s->instrument > ctx->module.num_instruments) {
			/* Invalid instrument, Cut current note */
			xm_cut_note(ch);
			ch->instrument = NULL;
			ch->sample = NULL;
		} else {
			ch->instrument = ctx->module.instruments + (s->instrument - 1);
			if(s->note == 0 && ch->sample != NULL) {
				/* Ghost instrument, trigger note */
				/* Sample position is kept, but envelopes are reset */
				float old_sample_pos = ch->sample_position;
				xm_trigger_note(ctx, ch);
				ch->sample_position = old_sample_pos;
			}
		}
	}

	if(s->note > 0 && s->note < 97) {
		/* Yes, the real note number is s->note -1. Try finding
		 * THAT in any of the specs! :-) */

		xm_instrument_t* instr = ch->instrument;

		if(HAS_TONE_PORTAMENTO(s) && instr != NULL && ch->sample != NULL) {
			/* Tone portamento in effect */
			ch->note = s->note + ch->sample->relative_note + ch->sample->finetune / 128.f - 1.f;
			ch->tone_portamento_target_period = XM_PERIOD_OF_NOTE(ch->note);
		} else if(instr == NULL || ch->instrument->num_samples == 0) {
			/* Bad instrument */
			xm_cut_note(ch);
		} else {
			if(instr->sample_of_notes[s->note - 1] < instr->num_samples) {
				ch->sample = instr->samples + instr->sample_of_notes[s->note - 1];
				ch->note = s->note + ch->sample->relative_note
					+ ch->sample->finetune / 128.f - 1.f;
				if(s->instrument > 0) {
					xm_trigger_note(ctx, ch);
				} else {
					/* Ghost note: keep old volume */
					float old_volume = ch->volume;
					xm_trigger_note(ctx, ch);
					ch->volume = old_volume;
				}
			} else {
				/* Bad sample */
				xm_cut_note(ch);
			}
		}
	} else if(s->note == 97) {
		/* Key Off */
		xm_key_off(ch);
	}

	ch->current_volume_effect = s->volume_column;
	ch->current_effect = s->effect_type;
	ch->current_effect_param = s->effect_param;

	switch(s->volume_column >> 4) {

	case 0x5:
		if(s->volume_column > 0x50) break;
	case 0x1:
	case 0x2:
	case 0x3:
	case 0x4:
		/* Set volume */
		ch->volume = (float)(s->volume_column - 0x10) / (float)0x40;
		break;

	case 0x8: /* Fine volume slide down */
		xm_volume_slide(ch, s->volume_column & 0x0F);
		break;

	case 0x9: /* Fine volume slide up */
		xm_volume_slide(ch, s->volume_column << 4);
		break;

	case 0xC: /* Set panning */
		ch->panning = (float)(s->volume_column - 0xC0) / (float)0xF;
		break;

	case 0xF: /* Tone portamento */
		if(s->volume_column & 0x0F) {
			ch->tone_portamento_param = s->volume_column;
		}
		break;

	default:
		break;

	}

	switch(s->effect_type) {

	case 1: /* 1xx: Portamento up */
		if(s->effect_param > 0) {
			ch->portamento_up_param = s->effect_param;
		}
		break;

	case 2: /* 2xx: Portamento down */
		if(s->effect_param > 0) {
			ch->portamento_down_param = s->effect_param;
		}
		break;

	case 3: /* 3xx: Tone portamento */
		if(s->effect_param > 0) {
			ch->tone_portamento_param = s->effect_param;
		}
		break;

	case 5: /* 5xy: Tone portamento + Volume slide */
		if(s->effect_param > 0) {
			ch->volume_slide_param = s->effect_param;
		}
		break;

	case 6: /* 6xy: Vibrato + Volume slide */
		if(s->effect_param > 0) {
			ch->volume_slide_param = s->effect_param;
		}
		break;

	case 8: /* 8xx: Set panning */
		ch->panning = (float)s->effect_param / (float)0xFF;
		break;

	case 0xA: /* Axy: Volume slide */
		if(s->effect_param > 0) {
			ch->volume_slide_param = s->effect_param;
		}
		break;

	case 0xB: /* Bxx: Position jump */
		if(s->effect_param < ctx->module.length) {
			ctx->position_jump = true;
			ctx->jump_dest = s->effect_param;
		}
		break;

	case 0xC: /* Cxx: Set volume */
		ch->volume = (float)((s->effect_param > 0x40)
							 ? 0x40 : s->effect_param) / (float)0x40;
		break;

	case 0xD: /* Dxx: Pattern break */
		/* Jump after playing this line */
		ctx->pattern_break = true;
		ctx->jump_row = (s->effect_param >> 4) * 10 + (s->effect_param & 0x0F);
		break;

	case 0xE: /* EXy: Extended command */
		switch(s->effect_param >> 4) {

		case 1: /* E1y: Fine portamento up */
			if(s->effect_param & 0x0F) {
				ch->fine_portamento_up_param = s->effect_param & 0x0F;
			}
			xm_pitch_slide(ctx, ch, -4.f * ch->fine_portamento_up_param);
			break;

		case 2: /* E2y: Fine portamento down */
			if(s->effect_param & 0x0F) {
				ch->fine_portamento_down_param = s->effect_param & 0x0F;
			}
			xm_pitch_slide(ctx, ch, 4.f * ch->fine_portamento_down_param);
			break;

		case 0xA: /* EAy: Fine volume slide up */
			if(s->effect_param & 0x0F) {
				ch->fine_volume_slide_param = s->effect_param & 0x0F;
			}
			xm_volume_slide(ch, ch->fine_volume_slide_param << 4);
			break;

		case 0xB: /* EBy: Fine volume slide down */
			if(s->effect_param & 0x0F) {
				ch->fine_volume_slide_param = s->effect_param & 0x0F;
			}
			xm_volume_slide(ch, ch->fine_volume_slide_param);
			break;

		case 0xD: /* EDy: Note delay */
			/* Already taken care of */
			break;

		default:
			break;

		}
		break;

	case 0xF: /* Fxx: Set tempo/BPM */
		if(s->effect_param > 0) {
			if(s->effect_param <= 0x1F) {
				ctx->tempo = s->effect_param;
			} else {
				ctx->bpm = s->effect_param;
			}
		}
		break;

	case 16: /* Gxx: Set global volume */
		ctx->global_volume = (float)((s->effect_param > 0x40)
									 ? 0x40 : s->effect_param) / (float)0x40;
		break;

	case 17: /* Hxy: Global volume slide */
		if(s->effect_param > 0) {
			ch->global_volume_slide_param = s->effect_param;
		}
		break;

	case 21: /* Lxx: Set envelope position */
		ch->volume_envelope_frame_count = s->effect_param;
		ch->panning_envelope_frame_count = s->effect_param;
		break;

	case 25: /* Pxy: Panning slide */
		if(s->effect_param > 0) {
			ch->panning_slide_param = s->effect_param;
		}
		break;

	case 27: /* Rxy: Multi retrig note */
		if(s->effect_param > 0) {
			if((s->effect_param >> 4) == 0) {
				/* Keep previous x value */
				ch->multi_retrig_param = (ch->multi_retrig_param & 0xF0) | (s->effect_param & 0x0F);
			} else {
				ch->multi_retrig_param = s->effect_param;
			}
		}
		break;

	case 33: /* Xxy: Extra stuff */
		switch(s->effect_param >> 4) {

		case 1: /* X1y: Extra fine portamento up */
			if(s->effect_param & 0x0F) {
				ch->extra_fine_portamento_up_param = s->effect_param & 0x0F;
			}
			xm_pitch_slide(ctx, ch, -1.0f * ch->extra_fine_portamento_up_param);
			break;

		case 2: /* X2y: Extra fine portamento down */
			if(s->effect_param & 0x0F) {
				ch->extra_fine_portamento_down_param = s->effect_param & 0x0F;
			}
			xm_pitch_slide(ctx, ch, ch->extra_fine_portamento_down_param);
			break;

		default:
			break;

		}
		break;

	default:
		break;

	}
}

static void xm_trigger_note(xm_context_t* ctx, xm_channel_context_t* ch) {
	ch->sample_position = 0.f;
	ch->sustained = true;
	ch->volume = ch->sample->volume;
	ch->panning = ch->sample->panning;
	ch->fadeout_volume = 1.0f;
	ch->volume_envelope_volume = 1.0f;
	ch->panning_envelope_panning = .5f;
	ch->volume_envelope_frame_count = 0;
	ch->panning_envelope_frame_count = 0;

	xm_update_step(ctx, ch, ch->note, true, true);
}

static void xm_cut_note(xm_channel_context_t* ch) {
	/* NB: this is not the same as Key Off */
	ch->note = .0f;
}

static void xm_key_off(xm_channel_context_t* ch) {
	/* Key Off */
	ch->sustained = false;

	/* If no volume envelope is used, also cut the note */
	if(ch->instrument == NULL || !ch->instrument->volume_envelope.enabled) {
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

	xm_pattern_t* cur = ctx->module.patterns + ctx->module.pattern_table[ctx->current_table_index];

	ctx->loop_count = (ctx->row_loop_count[MAX_NUM_ROWS * ctx->current_table_index + ctx->current_row]++);

	/* Read notes… */
	for(uint8_t i = 0; i < ctx->module.num_channels; ++i) {
		xm_pattern_slot_t* s = cur->slots + ctx->current_row * ctx->module.num_channels + i;
		xm_channel_context_t* ch = ctx->channels + i;

		if(s->effect_type != 0xE || s->effect_param >> 4 != 0xD) {
			xm_handle_note_and_instrument(ctx, ch, s);
		} else {
			ch->note_delay = true;
			ch->note_delay_param = s->effect_param & 0x0F;
			ch->note_delay_note = s;
		}
	}

	ctx->current_row++; /* Since this is an uint8, this line can
						 * increment from 255 to 0, in which case it
						 * is still necessary to go the next
						 * pattern. */
	if(!ctx->position_jump && !ctx->pattern_break &&
	   (ctx->current_row >= cur->num_rows || ctx->current_row == 0)) {
		ctx->current_table_index++;
		ctx->current_row = 0;
		xm_post_pattern_change(ctx);
	}
}

static void xm_envelope_tick(xm_channel_context_t* ch,
							 xm_envelope_t* env,
							 uint16_t* counter,
							 float* outval) {
	if(env->num_points < 2) {
		/* Don't really know what to do… */
		if(env->num_points == 1) {
			/* XXX I am pulling this out of my ass */
			*outval = (float)env->points[0].value / (float)0x40;
			if(*outval > 1) {
				*outval = 1;
			}
		}

		return;
	} else {
		uint8_t j;

		if(env->loop_enabled) {
			uint16_t loop_start = env->points[env->loop_start_point].frame;
			uint16_t loop_end = env->points[env->loop_end_point].frame;
			uint16_t loop_length = loop_end - loop_start;

			if(*counter >= loop_end) {
				*counter -= loop_length;
			}
		}

		for(j = 0; j < (env->num_points - 2); ++j) {
			if(env->points[j].frame <= *counter &&
			   env->points[j+1].frame >= *counter) {
				break;
			}
		}

		*outval = xm_envelope_lerp(env->points + j, env->points + j + 1, *counter) / (float)0x40;
		if(*outval > 1) {
			*outval = 1;
		}

		/* Make sure it is safe to increment frame count */
		if(!ch->sustained || !env->sustain_enabled ||
		   *counter < env->points[env->sustain_point].frame) {
			(*counter)++;
		}
	}
}

static void xm_tick(xm_context_t* ctx) {
	if(ctx->current_tick == 0) {
		xm_row(ctx);
	}

	for(uint8_t i = 0; i < ctx->module.num_channels; ++i) {
		xm_channel_context_t* ch = ctx->channels + i;

		/* Note delay check must happen here, before the envelopes are updated */
		if(ch->note_delay == true && ch->note_delay_param == ctx->current_tick) {
			ch->note_delay = false;
			xm_handle_note_and_instrument(ctx, ch, ch->note_delay_note);
		}

		if(ch->instrument != NULL) {
			if(ch->instrument->volume_envelope.enabled) {
				if(!ch->sustained) {
					ch->fadeout_volume -= (float)ch->instrument->volume_fadeout / 65536.f;
					if(ch->fadeout_volume < 0) ch->fadeout_volume = 0;
				}

				xm_envelope_tick(ch,
								 &(ch->instrument->volume_envelope),
								 &(ch->volume_envelope_frame_count),
								 &(ch->volume_envelope_volume));
			}

			if(ch->instrument->panning_envelope.enabled) {
				xm_envelope_tick(ch,
								 &(ch->instrument->panning_envelope),
								 &(ch->panning_envelope_frame_count),
								 &(ch->panning_envelope_panning));
			}
		}

		if(ch->arp_in_progress && (ch->current_effect != 0 || ch->current_effect_param == 0)) {
			/* Arpeggio was interrupted in an "uneven" cycle */
			ch->arp_in_progress = false;
			xm_update_step(ctx, ch, ch->note, true, true);
		}

		switch(ch->current_volume_effect >> 4) {

		case 0x6: /* Volume slide down */
			if(ctx->current_tick == 0) break;
			xm_volume_slide(ch, ch->current_volume_effect & 0x0F);
			break;

		case 0x7: /* Volume slide up */
			if(ctx->current_tick == 0) break;
			xm_volume_slide(ch, ch->current_volume_effect << 4);
			break;

		case 0xD: /* Panning slide left */
			if(ctx->current_tick == 0) break;
			xm_panning_slide(ch, ch->current_volume_effect & 0x0F);
			break;

		case 0xE: /* Panning slide right */
			if(ctx->current_tick == 0) break;
			xm_panning_slide(ch, ch->current_volume_effect << 4);
			break;

		case 0xF: /* Tone portamento */
			if(ctx->current_tick == 0) break;
			xm_tone_portamento(ctx, ch);
			break;

		default:
			break;

		}

		switch(ch->current_effect) {

		case 0: /* 0xy: Arpeggio */
			if(ch->current_effect_param > 0) {
				char arp_offset = ctx->tempo % 3;
				switch(arp_offset) {
				case 2: /* 0 -> x -> 0 -> y -> x -> … */
					if(ctx->current_tick == 1) {
						ch->arp_in_progress = true;
						xm_update_step(ctx, ch, ch->note +
									   (ch->current_effect_param >> 4), true, true);
						break;
					}
					/* No break here, this is intended */
				case 1: /* 0 -> 0 -> y -> x -> … */
					if(ctx->current_tick == 0) {
						xm_update_step(ctx, ch, ch->note, true, true);
						break;
					}
					/* No break here, this is intended */
				case 0: /* 0 -> y -> x -> … */
					xm_arpeggio(ctx, ch, ch->current_effect_param, ctx->current_tick - arp_offset);
				default:
					break;
				}
			}
			break;

		case 1: /* 1xx: Portamento up */
			if(ctx->current_tick == 0) break;
			/* Don't ask about the 4.f coefficient. I found mention of
			 * it nowhere. Found by ear™. */
			xm_pitch_slide(ctx, ch, -4.f * ch->portamento_up_param);
			break;

		case 2: /* 2xx: Portamento down */
			if(ctx->current_tick == 0) break;
			xm_pitch_slide(ctx, ch, 4.f * ch->portamento_down_param);
			break;

		case 3: /* 3xx: Tone portamento */
			if(ctx->current_tick == 0) break;
			xm_tone_portamento(ctx, ch);
			break;

		case 5: /* 5xy: Tone portamento + Volume slide */
			if(ctx->current_tick == 0) break;
			xm_tone_portamento(ctx, ch);
			xm_volume_slide(ch, ch->volume_slide_param);
			break;

		case 6: /* 6xy: Vibrato + Volume slide */
			if(ctx->current_tick == 0) break;
			xm_volume_slide(ch, ch->volume_slide_param);
			break;

		case 0xA: /* Axy: Volume slide */
			if(ctx->current_tick == 0) break;
			xm_volume_slide(ch, ch->volume_slide_param);
			break;

		case 0xE: /* EXy: Extended command */
			switch(ch->current_effect_param >> 4) {

			case 0xC: /* ECy: Note cut (misleading name, should be mute) */
				if((ch->current_effect_param & 0x0F) == ctx->current_tick) {
					ch->volume = .0f;
				}
				break;

			case 0xD: /* EDy: Note delay */
				/* Already taken care of */
				break;

			default:
				break;

			}
			break;

		case 17: /* Hxy: Global volume slide */
			if(ctx->current_tick == 0) break;
			if((ch->global_volume_slide_param & 0xF0) &&
			   (ch->global_volume_slide_param & 0x0F)) {
				/* Illegal state */
				break;
			}
			if(ch->global_volume_slide_param & 0xF0) {
				/* Global slide up */
				float f = (float)(ch->global_volume_slide_param >> 4) / (float)0x40;
				ctx->global_volume += f;
				if(ctx->global_volume > 1) ctx->global_volume = 1;
			} else {
				/* Global slide down */
				float f = (float)(ch->global_volume_slide_param & 0x0F) / (float)0x40;
				ctx->global_volume -= f;
				if(ctx->global_volume < 0) ctx->global_volume = 0;
			}
			break;

		case 20: /* Kxx: Key off */
			/* Most documentations will tell you the parameter has no
			 * use. Don't be fooled. */
			if(ctx->current_tick == ch->current_effect_param) {
				xm_key_off(ch);
			}
			break;

		case 25: /* Pxy: Panning slide */
			if(ctx->current_tick == 0) break;
			xm_panning_slide(ch, ch->panning_slide_param);
			break;

		case 27: /* Rxy: Multi retrig note */
			if(ctx->current_tick == 0) break;
			if(((ch->multi_retrig_param) & 0x0F) == 0) break;
			if((ctx->current_tick % (ch->multi_retrig_param & 0x0F)) == 0) {
				float v = ch->volume * multi_retrig_multiply[ch->multi_retrig_param >> 4]
					+ multi_retrig_add[ch->multi_retrig_param >> 4];
				if(v < 0) v = 0;
				else if(v > 1) v = 1;
				xm_trigger_note(ctx, ch);
				ch->volume = v;
			}
			break;

		default:
			break;

		}

		float fpanning = ch->panning +
			(ch->panning_envelope_panning - .5f) * (.5f - fabsf(ch->panning - .5f)) * 2.0f;

		ch->final_volume_left = ch->final_volume_right =
			ch->volume * ch->fadeout_volume * ch->volume_envelope_volume;
		ch->final_volume_left *= (1.0f - fpanning);
		ch->final_volume_right *= fpanning;
	}

	ctx->current_tick++;
	if(ctx->current_tick >= ctx->tempo) {
		ctx->current_tick = 0;
	}

	/* FT2 manual says number of ticks / second = BPM * 0.4 */
	ctx->remaining_samples_in_tick += (float)ctx->rate / ((float)ctx->bpm * 0.4f);
}

static void xm_sample(xm_context_t* ctx, float* left, float* right) {
	if(ctx->remaining_samples_in_tick <= 0) {
		xm_tick(ctx);
	}
	ctx->remaining_samples_in_tick--;

	*left = 0.f;
	*right = 0.f;

	for(uint8_t i = 0; i < ctx->module.num_channels; ++i) {
		float chval;
		xm_channel_context_t* ch = ctx->channels + i;

		if(ch->note == 0 ||
		   ch->instrument == NULL ||
		   ch->sample == NULL ||
		   ch->sample_position < 0) {
			continue;
		}

		chval = ch->sample->data[(size_t)(ch->sample_position)];
		ch->sample_position += ch->step;

		switch(ch->sample->loop_type) {
		case XM_NO_LOOP:
		    if(ch->sample_position >= ch->sample->length) {
				ch->sample_position = -1;
			}
			break;
		case XM_FORWARD_LOOP:
		case XM_PING_PONG_LOOP: /* TODO */
			while(ch->sample_position >= ch->sample->loop_start + ch->sample->loop_length) {
				ch->sample_position -= ch->sample->loop_length;
			}
			break;
		}

		*left += chval * ch->final_volume_left;
		*right += chval * ch->final_volume_right;
	}

	const float fgvol = ctx->global_volume * ctx->amplification;
	*left *= fgvol;
	*right *= fgvol;

	if(XM_DEBUG && (*left > 1 || *left < -1)) {
		DEBUG("clipping will occur, final sample value is %f", *left);
	}
	if(XM_DEBUG && (*right > 1 || *right < -1)) {
		DEBUG("clipping will occur, final sample value is %f", *right);
	}
}

void xm_generate_samples(xm_context_t* ctx, float* output, size_t numsamples) {
	for(size_t i = 0; i < numsamples; i++) {
		xm_sample(ctx, output + (2 * i), output + (2 * i + 1));
	}
}
