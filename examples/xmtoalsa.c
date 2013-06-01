/* Author: Romain "Artefact2" Dalmaso <artefact2@gmail.com> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#include "testprog.h"
#include <alsa/asoundlib.h>

#define FATAL_ALSA_ERR(s, err) do {							\
		fprintf(stderr, s ": %s\n", snd_strerror((err)));	\
		fflush(stderr);										\
		exit(1);											\
	} while(0)

#define CHECK_ALSA_CALL(call) do {							\
		int ret = (call);									\
		if(ret < 0)	{										\
			ret = snd_pcm_recover(device, ret, 0);			\
			if(ret < 0)										\
				FATAL_ALSA_ERR("ALSA internal error", ret);	\
		}													\
	} while(0)

static const size_t buffer_size = 1024; /* Average buffer size, should
										 * be enough even on slow CPUs
										 * and not too laggy */
static const unsigned int channels = 2;
static const unsigned int rate = 48000;

int main(int argc, char** argv) {
	xm_context_t* ctx;
	float buffer[buffer_size];
    void* params;
	snd_pcm_t* device;

	if(argc != 2)
		FATAL("Usage: %s <filename>\n", argv[0]);

	create_context_from_file(&ctx, rate, argv[1]);

	CHECK_ALSA_CALL(snd_pcm_open(&device, "default", SND_PCM_STREAM_PLAYBACK, 0));
	CHECK_ALSA_CALL(snd_pcm_hw_params_malloc((snd_pcm_hw_params_t**)(&params)));
	CHECK_ALSA_CALL(snd_pcm_hw_params_any(device, params));
	CHECK_ALSA_CALL(snd_pcm_hw_params_set_access(device, params, SND_PCM_ACCESS_RW_INTERLEAVED));
	CHECK_ALSA_CALL(snd_pcm_hw_params_set_format(device, params, SND_PCM_FORMAT_FLOAT));
	CHECK_ALSA_CALL(snd_pcm_hw_params_set_rate(device, params, rate, 0));
	CHECK_ALSA_CALL(snd_pcm_hw_params_set_channels(device, params, channels));
	CHECK_ALSA_CALL(snd_pcm_hw_params_set_buffer_size(device, params, buffer_size << 2));
	CHECK_ALSA_CALL(snd_pcm_hw_params_set_period_size(device, params, buffer_size, 0));
	CHECK_ALSA_CALL(snd_pcm_hw_params(device, params));
	snd_pcm_hw_params_free(params);
	CHECK_ALSA_CALL(snd_pcm_sw_params_malloc((snd_pcm_sw_params_t**)(&params)));
	CHECK_ALSA_CALL(snd_pcm_sw_params_current(device, params));
	CHECK_ALSA_CALL(snd_pcm_sw_params_set_start_threshold(device, params, buffer_size));
	CHECK_ALSA_CALL(snd_pcm_sw_params(device, params));
	snd_pcm_sw_params_free(params);
	CHECK_ALSA_CALL(snd_pcm_prepare(device));

	while(xm_get_loop_count(ctx) == 0) {
		xm_generate_samples(ctx, buffer, sizeof(buffer) / (channels * sizeof(float)));
		CHECK_ALSA_CALL(snd_pcm_writei(device, buffer, sizeof(buffer) / (channels * sizeof(float))));
	}

	CHECK_ALSA_CALL(snd_pcm_drop(device));
	CHECK_ALSA_CALL(snd_pcm_close(device));
	xm_free_context(ctx);
	return 0;
}
