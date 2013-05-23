/* Author: Romain "Artefact2" Dalmaso <artefact2@gmail.com> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#include "xm_internal.h"

static void xm_update_step(xm_context_t* ctx, xm_channel_context_t* ch, float note) {
	/* XXX Amiga frequencies? */

	float period = 7680.f - note * 64.f;
	float frequency = 8363.f * powf(2.f, (4608.f - period) / 768.f);
	ch->step = frequency / ctx->rate;
}

static void xm_row(xm_context_t* ctx) {
	if(ctx->jump) {
		ctx->jump = false;
		ctx->current_table_index++;
		ctx->current_row = ctx->jump_row;
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
		    ch->instrument = ctx->module.instruments + (s->instrument - 1);
		}

		if(s->note > 0 && s->note < 97) {
			/* Yes, the real note number is s->note -1. Try finding
			 * THAT in any of the specs! :-) */

			xm_instrument_t* instr = ch->instrument;
			if(ch->instrument->num_samples == 0) continue; /* XXX */

			ch->sample = instr->samples + instr->sample_of_notes[s->note - 1];
			xm_sample_t* sample = ch->sample;

		    ch->note = s->note + sample->relative_note + sample->finetune / 128.f - 1.f;
			ch->sample_position = 0.f;
			ch->sustained = true;
			ch->volume = sample->volume;

			xm_update_step(ctx, ch, ch->note);
		} else if(s->note == 97) {
			/* Key Off */
			ch->sustained = false;
		}

		if(s->volume_column > 0) {
			if(s->volume_column >= 0x10 && s->volume_column <= 0x50) {
				ch->volume = (float)(s->volume_column - 0x10) / (float)0x40;
			}
		}

		ch->current_effect = s->effect_type;
		ch->current_effect_param = s->effect_param;

		if(s->effect_type > 0) {
			switch(s->effect_type) {
			case 0xA: /* Axy: Volume slide */
				if(s->effect_param > 0) {
					ch->volume_slide_param = s->effect_param;
				}
				break;

			case 0xC: /* Cxx: Set volume */
				ch->volume = (float)((s->effect_param > 0x40)
									 ? 0x40 : s->effect_param) / (float)0x40;
				break;

			case 0xD: /* Dxx: Pattern break */
				/* Jump after playing this line */
				ctx->jump = true;
				ctx->jump_row = ((s->effect_param & 0xF0) >> 4) * 10 + (s->effect_param & 0x0F);
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

			case 20: /* Kxx: Key off */
				ch->sustained = false;
				break;

			default:
				break;
			}
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

		if(ch->arp_in_progress && (ch->current_effect != 0 || ch->current_effect_param == 0)) {
			/* Arpeggio was interrupted in an "uneven" cycle */
			ch->arp_in_progress = false;
			xm_update_step(ctx, ch, ch->note);
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
								   ((ch->current_effect_param & 0xF0) >> 4));
					break;
				case 2:
					ch->arp_in_progress = true;
					xm_update_step(ctx, ch, ch->note +
								   (ch->current_effect_param & 0x0F));
					break;
				}
			}
			break;

		case 0xA: /* Axy: Volume slide */
			if(ch->volume_slide_param < 0x10) {
				ch->volume -= (float)ch->volume_slide_param / (float)0x40;
				if(ch->volume < 0) ch->volume = 0;
			} else {
				ch->volume += (float)((ch->volume_slide_param & 0xF0) >> 4) / (float)0x40;
				if(ch->volume > 1) ch->volume = 1;
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
		if(ch->note == 0 || ch->instrument == 0) continue;
		if(!ch->sustained) continue;

		if(ch->sample_position < 0) continue; /* Reached end of non-looping sample */

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

		*left += chval * ch->volume;
		*right += chval * ch->volume;
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
