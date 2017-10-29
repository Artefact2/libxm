/* Author: Romain "Artefact2" Dalmaso <artefact2@gmail.com> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#include <xm.h>
#include <alsa/asoundlib.h>
#include "mus.h"

static xm_context_t* ctx;
static snd_pcm_t* device;
static snd_pcm_hw_params_t* params;
static float buffer[4096];

static void gen_waveforms(void) {
	size_t i;
	int8_t* buf;
	size_t len;
	uint8_t bits;

	/* Square, large duty, half volume */
	buf = xm_get_sample_waveform(ctx, 1, 0, &len, &bits);
	for(i = 0x40; i < len; ++i) buf[i] = 127;

	/* Ramp */
	buf = xm_get_sample_waveform(ctx, 2, 0, &len, &bits);
	for(i = 0; i < len; ++i) buf[i] = (256 * i) / len - 128;

	/* Square, small duty */
	buf = xm_get_sample_waveform(ctx, 4, 0, &len, &bits);
	for(i = 0; i < 0x80; ++i) buf[i] = -128;
	for(; i < 0xB0; ++i) buf[i] = 127;
	for(; i < len; ++i) buf[i] = -128;

	/* XXX: Kick */
	/* XXX: Pad */
	/* XXX: Drum */

	/* Noise */
	buf = xm_get_sample_waveform(ctx, 8, 0, &len, &bits);
	unsigned int next = 1;
	for(i = 0; i < len; ++i) {
		next = next * 8127 + 1; /* A very simple linear congruence generator, see rand(3) */
		buf[i] = next >> 16 & 0xFF;
	}
}

int main(void) {
	unsigned int rate = 48000;

	snd_pcm_open(&device, "default", SND_PCM_STREAM_PLAYBACK, 0);
	snd_pcm_hw_params_malloc(&params);
	snd_pcm_hw_params_any(device, params);
	snd_pcm_hw_params_set_access(device, params, SND_PCM_ACCESS_RW_INTERLEAVED);
	snd_pcm_hw_params_set_format(device, params, SND_PCM_FORMAT_FLOAT);
	snd_pcm_hw_params_set_channels(device, params, 2);
	snd_pcm_hw_params_set_rate_near(device, params, &rate, NULL);
	snd_pcm_hw_params(device, params);

	xm_create_context_from_libxmize(&ctx, mus, rate);
	gen_waveforms();

	snd_pcm_prepare(device);

	while(1) {
		xm_generate_samples(ctx, buffer, 2048);
		snd_pcm_writei(device, buffer, 2048);
	}

	return 0;
}
