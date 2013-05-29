/* Author: Romain "Artefact2" Dalmaso <artefact2@gmail.com> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#include "xm_internal.h"

int xm_create_context(xm_context_t** ctxp, char* moddata, uint32_t rate) {
	int ret;
	size_t bytes_needed;
	char* mempool;
	xm_context_t* ctx;
	

	if((ret = xm_check_header_sanity(moddata))) {
		DEBUG("xm_check_header_sanity() returned %i", ret);
		return 1;
	}

	bytes_needed = xm_get_memory_needed_for_context(moddata);

	DEBUG("allocating %i bytes", (int)bytes_needed);
	mempool = malloc(bytes_needed);
	if(mempool == NULL && bytes_needed > 0) {
		/* malloc() failed, trouble ahead */
		DEBUG("call to malloc() failed, returned %p", mempool);
		return 2;
	}

	ctx = (*ctxp = (xm_context_t*)mempool);
	ctx->allocated_memory = mempool; /* Keep original pointer for free() */
	mempool += sizeof(xm_context_t);

	ctx->rate = rate;
	mempool = xm_load_module(ctx, moddata, mempool);

	ctx->channels = (xm_channel_context_t*)mempool;
	mempool += ctx->module.num_channels * sizeof(xm_channel_context_t);

	ctx->global_volume = 1.f;
	ctx->amplification = .5f; /* Purely empirical value */
	ctx->current_table_index = 0;
	ctx->current_row = 0;
	ctx->current_tick = 0;
	ctx->remaining_samples_in_tick = 0;

	ctx->position_jump = false;
	ctx->pattern_break = false;
	ctx->jump_dest = 0;
	ctx->jump_row = 0;

	for(uint8_t i = 0; i < ctx->module.num_channels; ++i) {
		xm_channel_context_t* ch = ctx->channels + i;

		ch->note = 0.f;
		ch->instrument = NULL;
		ch->sample = NULL;
		ch->sample_position = 0.f;
		ch->period = 0.f;
		ch->frequency = 1.f;
		ch->step = 0.f;
		ch->ping = true;

		ch->volume = 1.0f;
		ch->panning = .5f;

		ch->sustained = false;
		ch->fadeout_volume = 1.0f;
		ch->volume_envelope_volume = 1.0f;
		ch->panning_envelope_panning = .5f;
		ch->volume_envelope_frame_count = 0;
		ch->panning_envelope_frame_count = 0;

		ch->current_volume_effect = 0;
		ch->current_effect = 0;
		ch->current_effect_param = 0;

		ch->arp_in_progress = false;
		ch->volume_slide_param = 0;
		ch->fine_volume_slide_param = 0;
		ch->global_volume_slide_param = 0;
		ch->panning_slide_param = 0;
		ch->portamento_up_param = 0;
		ch->portamento_down_param = 0;
		ch->fine_portamento_up_param = 0;
		ch->fine_portamento_down_param = 0;
		ch->extra_fine_portamento_up_param = 0;
		ch->extra_fine_portamento_down_param = 0;
		ch->multi_retrig_param = 0;
		ch->note_delay = false;
		ch->note_delay_param = 0;
		ch->note_delay_note = NULL;

		ch->final_volume_left = .5f;
		ch->final_volume_right = .5f;
	}

	ctx->row_loop_count = (uint8_t*)mempool;
	mempool += MAX_NUM_ROWS * sizeof(uint8_t);
	memset(ctx->row_loop_count, 0, MAX_NUM_ROWS * ctx->module.length);

	return 0;
}

void xm_free_context(xm_context_t* context) {
	free(context->allocated_memory);
}

uint32_t xm_get_loop_count(xm_context_t* context) {
	return context->loop_count;
}
