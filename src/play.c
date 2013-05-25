/* Author: Romain "Artefact2" Dalmaso <artefact2@gmail.com> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#include "xm_internal.h"

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

static float xm_envelope_lerp(xm_envelope_point_t* a, xm_envelope_point_t* b, uint16_t pos) {
	/* Linear interpolation between two envelope points */
	if(a->frame == b->frame) return a->value;
	else if(pos >= b->frame) return b->value;
	else {
		float p = (float)(pos - a->frame) / (float)(b->frame - a->frame);
		return a->value * (1 - p) + b->value * p;
	}
}

static void xm_update_step(xm_context_t* ctx, xm_channel_context_t* ch, float note) {
	/* XXX Amiga frequencies? */

	float period = 7680.f - note * 64.f;
	float frequency = 8363.f * powf(2.f, (4608.f - period) / 768.f);
	ch->step = frequency / ctx->rate;
}

static void xm_trigger_note(xm_context_t* ctx, xm_channel_context_t* ch) {
	ch->sample_position = 0.f;
	ch->sustained = true;
	ch->volume = ch->sample->volume;
	ch->fadeout_volume = 1.0f;
	ch->volume_envelope_volume = 1.0f;
	ch->volume_envelope_frame_count = 0;

	xm_update_step(ctx, ch, ch->note);
}

static void xm_cut_note(xm_channel_context_t* ch) {
	/* NB: this is not the same as Key Off */
	ch->note = .0f;
	ch->instrument = NULL;
	ch->sample = NULL;
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
	if(ctx->jump) {
		ctx->current_table_index = ctx->jump_to;
		ctx->current_row = ctx->jump_row;
		ctx->jump = false;
	}

	/* Loop if necessary */
	if(ctx->current_table_index >= ctx->module.length) {
		ctx->current_table_index = ctx->module.restart_position;
		ctx->loop_count++;
	}

	xm_pattern_t* cur = ctx->module.patterns + ctx->module.pattern_table[ctx->current_table_index];

	/* Read notes… */
	for(uint8_t i = 0; i < ctx->module.num_channels; ++i) {
		xm_pattern_slot_t* s = cur->slots + ctx->current_row * ctx->module.num_channels + i;
		xm_channel_context_t* ch = ctx->channels + i;

		if(s->instrument > 0) {
			if(s->instrument - 1 > ctx->module.num_instruments) {
				/* Invalid instrument, Cut current note */
				xm_cut_note(ch);
			} else {
				ch->instrument = ctx->module.instruments + (s->instrument - 1);
				if(s->note == 0 && ch->sample != NULL) {
					/* Ghost instrument, trigger note */
					xm_trigger_note(ctx, ch);
				}
			}
		}

		if(s->note > 0 && s->note < 97) {
			/* Yes, the real note number is s->note -1. Try finding
			 * THAT in any of the specs! :-) */

			xm_instrument_t* instr = ch->instrument;
			if(instr == NULL || ch->instrument->num_samples == 0) {
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
		default:
			break;
		}

		switch(s->effect_type) {

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

		case 0xA: /* Axy: Volume slide */
			if(s->effect_param > 0) {
				ch->volume_slide_param = s->effect_param;
			}
			break;

		case 0xB: /* Bxx: Position jump */
			if(s->effect_param < ctx->module.length) {
				ctx->jump = true;
				ctx->jump_to = s->effect_param;
				ctx->jump_row = 0;
			}
			break;

		case 0xC: /* Cxx: Set volume */
			ch->volume = (float)((s->effect_param > 0x40)
								 ? 0x40 : s->effect_param) / (float)0x40;
			break;

		case 0xD: /* Dxx: Pattern break */
			/* Jump after playing this line */
			ctx->jump = true;
			ctx->jump_to = ctx->current_table_index + 1;
			ctx->jump_row = (s->effect_param >> 4) * 10 + (s->effect_param & 0x0F);
			break;

		case 0xE: /* EXy: Extended command */
			switch(s->effect_param >> 4) {

			case 0xA: /* EAy: Fine volume slide up */
				ch->fine_volume_slide_param = s->effect_param & 0x0F;
				xm_volume_slide(ch, ch->volume_slide_param << 4);
				break;

			case 0xB: /* EBy: Fine volume slide down */
				ch->fine_volume_slide_param = s->effect_param & 0x0F;
				xm_volume_slide(ch, ch->volume_slide_param);
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

		case 20: /* Kxx: Key off */
			xm_key_off(ch);
			break;

		default:
			break;

		}
	}

	ctx->current_row++; /* Since this is an uint8, this line can
						 * increment from 255 to 0, in which case it
						 * is still necessary to go the next
						 * pattern. */
	if(!ctx->jump && (ctx->current_row >= cur->num_rows || ctx->current_row == 0)) {
		ctx->current_table_index++;
		ctx->current_row = 0;
	}
}

static void xm_tick(xm_context_t* ctx) {
	if(ctx->current_tick == 0) {
		xm_row(ctx);
	}

	for(uint8_t i = 0; i < ctx->module.num_channels; ++i) {
		xm_channel_context_t* ch = ctx->channels + i;

		if(ch->instrument != NULL && ch->instrument->volume_envelope.enabled) {
			xm_envelope_t* env = &(ch->instrument->volume_envelope);

			if(!ch->sustained) {
				ch->fadeout_volume -= (float)ch->instrument->volume_fadeout / 65536.f;
				if(ch->fadeout_volume < 0) ch->fadeout_volume = 0;
			}

			if(env->num_points < 2) {
				/* Don't really know what to do… */
				if(env->num_points == 1) {
					/* XXX I am pulling this out of my ass */
					ch->volume_envelope_volume = (float)env->points[0].value / (float)0x40;
					if(ch->volume_envelope_volume > 1) {
						ch->volume_envelope_volume = 1;
					}
				}
			} else {
				uint8_t j;

				if(env->loop_enabled) {
					uint16_t loop_start = env->points[env->loop_start_point].frame;
					uint16_t loop_end = env->points[env->loop_end_point].frame;
					uint16_t loop_length = loop_end - loop_start;

					if(ch->volume_envelope_frame_count > loop_end) {
						ch->volume_envelope_frame_count -= loop_length;
					}
				}

				for(j = 0; j < (env->num_points - 1); ++j) {
					if(env->points[j].frame <= ch->volume_envelope_frame_count &&
					   env->points[j+1].frame >= ch->volume_envelope_frame_count) {
						break;
					}
				}

				ch->volume_envelope_volume = 
					xm_envelope_lerp(env->points + j, env->points + j + 1,
									 ch->volume_envelope_frame_count) / (float)0x40;

				if(ch->volume_envelope_volume > 1) {
					ch->volume_envelope_volume = 1;
				}

				/* Make sure it is safe to increment frame count */
				if(!ch->sustained || !env->sustain_enabled ||
				   ch->volume_envelope_frame_count < env->points[env->sustain_point].frame) {
					ch->volume_envelope_frame_count++;
				}
			}
		}

		if(ch->arp_in_progress && (ch->current_effect != 0 || ch->current_effect_param == 0)) {
			/* Arpeggio was interrupted in an "uneven" cycle */
			ch->arp_in_progress = false;
			xm_update_step(ctx, ch, ch->note);
		}

		switch(ch->current_volume_effect >> 4) {

		case 0x6: /* Volume slide down */
			xm_volume_slide(ch, ch->current_volume_effect & 0x0F);
			break;

		case 0x7: /* Volume slide up */
			xm_volume_slide(ch, ch->current_volume_effect << 4);
			break;

		default:
			break;

		}

		switch(ch->current_effect) {

		case 0: /* 0xy: Arpeggio */
			if(ch->current_effect_param > 0) {
				switch(ctx->current_tick % 3) {
				case 0:
					ch->arp_in_progress = false;
					xm_update_step(ctx, ch, ch->note);
					break;
				case 1:
					ch->arp_in_progress = true;
					xm_update_step(ctx, ch, ch->note +
								   (ch->current_effect_param >> 4));
					break;
				case 2:
					ch->arp_in_progress = true;
					xm_update_step(ctx, ch, ch->note +
								   (ch->current_effect_param & 0x0F));
					break;
				}
			}
			break;

		case 5: /* 5xy: Tone portamento + Volume slide */
			xm_volume_slide(ch, ch->volume_slide_param);
			break;

		case 6: /* 6xy: Vibrato + Volume slide */
			xm_volume_slide(ch, ch->volume_slide_param);
			break;

		case 0xA: /* Axy: Volume slide */
			xm_volume_slide(ch, ch->volume_slide_param);
			break;

		case 0xE: /* EXy: Extended command */
			switch(ch->current_effect_param >> 4) {

			case 0xC: /* ECy: Note cut (misleading name, should be mute) */
				if((ch->current_effect_param & 0x0F) == ctx->current_tick) {
					ch->volume = .0f;
				}
				break;

			default:
				break;

			}
			break;

		case 17: /* Hxy: Global volume slide */
			if((ch->global_volume_slide_param & 0xF0) &&
			   (ch->global_volume_slide_param & 0x0F)) {
				/* Illegal state */
				break;
			}
			if(ch->global_volume_slide_param & 0xF0) {
				/* Global slide up */
				float f = (ch->global_volume_slide_param >> 4) / (float)0x40;
				ctx->global_volume += f;
				if(ctx->global_volume > 1) ctx->global_volume = 1;
			} else {
				/* Global slide down */
				float f = (ch->global_volume_slide_param & 0x0F) / (float)0x40;
				ctx->global_volume -= f;
				if(ctx->global_volume < 0) ctx->global_volume = 0;
			}
			break;

		default:
			break;

		}
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
		xm_channel_context_t* ch = ctx->channels + i;
		if(ch->note == 0 ||
		   ch->instrument == NULL ||
		   ch->sample == NULL ||
		   ch->sample_position < 0) {
			continue;
		}

		float chval = ch->sample->data[(size_t)(ch->sample_position)];

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

		*left += chval * ch->volume * ch->fadeout_volume * ch->volume_envelope_volume;
		*right += chval * ch->volume * ch->fadeout_volume * ch->volume_envelope_volume;
	}

	*left *= ctx->global_volume / ctx->module.num_channels;
	*right *= ctx->global_volume / ctx->module.num_channels;

	if(*left > 1 || *left < -1) {
		DEBUG("mix overflow %f", *left);
	}
}

void xm_generate_samples(xm_context_t* ctx, float* output, size_t numsamples) {
	for(size_t i = 0; i < numsamples; i++) {
		xm_sample(ctx, output + (2 * i), output + (2 * i + 1));
	}
}
