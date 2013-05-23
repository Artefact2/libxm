/* Author: Romain "Artefact2" Dalmaso <artefact2@gmail.com> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#include "xm_internal.h"

int xm_create_context(xm_context_t** ctx, char* moddata, uint32_t rate) {
	int ret;
	size_t bytes_needed;
	char* mempool;
	

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

	*ctx = (xm_context_t*)mempool;
	(*ctx)->allocated_memory = mempool; /* Keep original pointer for free() */
	mempool += sizeof(xm_context_t);

	(*ctx)->rate = rate;
	mempool = xm_load_module(*ctx, moddata, mempool);

	(*ctx)->channels = (xm_channel_context_t*)mempool;
	mempool += (*ctx)->module.num_channels * sizeof(xm_channel_context_t);

	(*ctx)->global_volume = 1.f;
	(*ctx)->current_table_index = 0;
	(*ctx)->current_row = 0;
	(*ctx)->current_tick = 0;
	(*ctx)->remaining_samples_in_tick = 0;
	(*ctx)->jump = false;
	(*ctx)->jump_to = 0;
	(*ctx)->jump_row = 0;

	for(uint8_t i = 0; i < (*ctx)->module.num_channels; ++i) {
		(*ctx)->channels[i].note = 0.f;
		(*ctx)->channels[i].instrument = NULL;
		(*ctx)->channels[i].sample = NULL;
		(*ctx)->channels[i].sample_position = 0.f;
		(*ctx)->channels[i].step = 0.f;
		(*ctx)->channels[i].sustained = false;
		(*ctx)->channels[i].fadeout_volume = 1.0f;
		(*ctx)->channels[i].volume_envelope_volume = 1.0f;
		(*ctx)->channels[i].volume_envelope_frame_count = 0;
		(*ctx)->channels[i].current_effect = 0;
		(*ctx)->channels[i].current_effect_param = 0;
		(*ctx)->channels[i].arp_in_progress = false;
		(*ctx)->channels[i].volume_slide_param = 0;
	}

	return 0;
}

void xm_free_context(xm_context_t* context) {
	free(context->allocated_memory);
}

uint32_t xm_get_loop_count(xm_context_t* context) {
	return context->loop_count;
}
