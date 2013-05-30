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

	/* Initialize most of the fields to 0, 0.f, NULL or false depending on type */
	memset(mempool, 0, bytes_needed);

	ctx = (*ctxp = (xm_context_t*)mempool);
	ctx->allocated_memory = mempool; /* Keep original pointer for free() */
	mempool += sizeof(xm_context_t);

	ctx->rate = rate;
	mempool = xm_load_module(ctx, moddata, mempool);

	ctx->channels = (xm_channel_context_t*)mempool;
	mempool += ctx->module.num_channels * sizeof(xm_channel_context_t);

	ctx->global_volume = 1.f;
	ctx->amplification = .5f; /* Purely empirical value */
	for(uint8_t i = 0; i < ctx->module.num_channels; ++i) {
		xm_channel_context_t* ch = ctx->channels + i;

		ch->ping = true;
		ch->vibrato_waveform = XM_SINE_WAVEFORM;
		ch->vibrato_waveform_retrigger = true;
		ch->tremolo_waveform = XM_SINE_WAVEFORM;
		ch->tremolo_waveform_retrigger = true;

		ch->volume = ch->volume_envelope_volume = ch->fadeout_volume = 1.0f;
		ch->panning = ch->panning_envelope_panning = .5f;
		ch->final_volume_left = .5f;
		ch->final_volume_right = .5f;
	}

	ctx->row_loop_count = (uint8_t*)mempool;
	mempool += MAX_NUM_ROWS * sizeof(uint8_t);

	return 0;
}

void xm_free_context(xm_context_t* context) {
	free(context->allocated_memory);
}

uint32_t xm_get_loop_count(xm_context_t* context) {
	return context->loop_count;
}
