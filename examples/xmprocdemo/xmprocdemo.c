/* Author: Romain "Artefact2" Dalmaso <artefact2@gmail.com> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#include <xm.h>
#include <stddef.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

static xm_context_t* ctx;
static float buffer[256];
int pipe_fd[2]; /* 0 => read end, 1 => write end */

static alignas(max_align_t) char libxm_data[] = {
#embed "mus.libxm"
};

static void gen_waveforms(void) {
	size_t i;
	xm_sample_point_t* buf;
	uint32_t len;

	const xm_sample_point_t MAX = _Generic((xm_sample_point_t){},
	                                       int8_t: INT8_MAX,
	                                       int16_t: INT16_MAX,
	                                       float: 1.f);
	const xm_sample_point_t MIN = _Generic((xm_sample_point_t){},
	                                       int8_t: INT8_MIN,
	                                       int16_t: INT16_MIN,
	                                       float: -1.f);

	/* Square, large duty, half volume */
	buf = xm_get_sample_waveform(ctx, 1, 0, &len);
	for(i = 0x40; i < len; ++i) buf[i] = MAX;

	/* Square, small duty */
	buf = xm_get_sample_waveform(ctx, 4, 0, &len);
	for(i = 0; i < 0x30; ++i) buf[i] = MAX;
	for(; i < len; ++i) buf[i] = MIN;

	/* Ramp */
	buf = xm_get_sample_waveform(ctx, 2, 0, &len);
	for(i = 0; i < len; ++i)
		buf[i] = _Generic((xm_sample_point_t){},
		                  int8_t: (INT8_MIN + (2*INT8_MAX*i) / len),
		                  int16_t: (INT16_MIN + (2*INT16_MAX*i) / len),
		                  float: -1.f + 2.f*(float)i / (float)len);

	/* XXX: Kick */
	/* XXX: Pad */
	/* XXX: Drum */

	/* Noise (simple linear congruence generator) */
	buf = xm_get_sample_waveform(ctx, 8, 0, &len);
	uint32_t next = 1;
	for(i = 0; i < len; ++i) {
		next = next * 214013 + 2531011;
		buf[i] = _Generic((xm_sample_point_t){},
		                  int8_t: next >> 16,
		                  int16_t: next >> 16,
		                  float: -1.f + (float)(next >> 16) / (float)INT16_MAX);
	}
}

// XXX: pipewire requires environment. figure out a way to get it within
// _start() and use -nostartfiles?
int main() {
	pipe(pipe_fd);

	if(fork()) {
		/* parent */
		ctx = xm_create_context_from_libxm(libxm_data, 48000);
		gen_waveforms();

		xm_set_max_loop_count(ctx, 1);
		while(xm_get_loop_count(ctx) == 0) {
			xm_generate_samples(ctx, buffer, sizeof(buffer) / (2 * sizeof(float)));
			write(pipe_fd[1], buffer, sizeof(buffer));
		}

		kill(0, SIGKILL);
		__builtin_unreachable();
	}

	/* child  */
	dup2(pipe_fd[0], STDIN_FILENO);
	execlp("aplay", "aplay", "-traw", "-fdat",
	       LITTLE_ENDIAN ? "-fFLOAT_LE" : "-fFLOAT_BE",
	       NULL);
	__builtin_unreachable();
}
