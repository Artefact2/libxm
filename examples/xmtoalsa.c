/* Author: Romain "Artefact2" Dalmaso <artefact2@gmail.com> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#include <xm.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <alsa/asoundlib.h>

const size_t buffer_size = 1024;
const unsigned int channels = 2;
const unsigned int rate = 48000;

int main(int argc, char** argv) {
	int fd, ret;
	off_t size;
	void* data;
	xm_context_t* ctx;
	float buffer[buffer_size];
    snd_pcm_hw_params_t* params;
	snd_pcm_t* device;

	if(argc != 2) {
		fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
		return 1;
	}

	fd = open(argv[1], O_RDONLY);
	size = lseek(fd, 0, SEEK_END);
	data = mmap(0, size, PROT_READ, MAP_SHARED, fd, 0);

	if((ret = xm_create_context(&ctx, data, rate)) != 0) {
		fprintf(stderr, "xm_create_context() failed and returned %i\n", ret);
		return 1;
	}

	munmap(data, size); /* Data no longer needed, all necessary data was copied in the context */

	snd_pcm_open(&device, "default", SND_PCM_STREAM_PLAYBACK, 0);
	snd_pcm_hw_params_malloc(&params);
	snd_pcm_hw_params_any(device, params);
	snd_pcm_hw_params_set_access(device, params, SND_PCM_ACCESS_RW_INTERLEAVED);
	snd_pcm_hw_params_set_format(device, params, SND_PCM_FORMAT_FLOAT_LE);
	snd_pcm_hw_params_set_rate(device, params, rate, 0);
	snd_pcm_hw_params_set_channels(device, params, channels);
	snd_pcm_hw_params_set_buffer_size(device, params, buffer_size * 4);
	snd_pcm_hw_params_set_period_size(device, params, buffer_size, 0);
	snd_pcm_hw_params(device, params);
	snd_pcm_hw_params_free(params);
	snd_pcm_prepare(device);

	while(xm_get_loop_count(ctx) == 0) {
		xm_generate_samples(ctx, buffer, sizeof(buffer) / (channels * sizeof(float)));
		snd_pcm_writei(device, buffer, sizeof(buffer) / (channels * sizeof(float)));
	}

	snd_pcm_drop(device);
	snd_pcm_close(device);
	xm_free_context(ctx);
	return 0;
}
