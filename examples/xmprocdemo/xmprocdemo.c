/* Author: Romain "Artefact2" Dalmaso <artefact2@gmail.com> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#include <xm.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

static xm_context_t* ctx;
static float buffer[256];
int pipe_fd[2]; /* 0 => read end, 1 => write end */

static char libxm_data[] = {
#embed "mus.libxm"
};

static void gen_waveforms(void) {
	size_t i;
	int16_t* buf;
	uint32_t len;

	/* Square, large duty, half volume */
	buf = xm_get_sample_waveform(ctx, 1, 0, &len);
	for(i = 0x40; i < len; ++i) buf[i] = INT16_MAX;

	/* Ramp */
	buf = xm_get_sample_waveform(ctx, 2, 0, &len);
	for(i = 0; i < len; ++i) buf[i] = (int16_t)((UINT16_MAX * i) / len);

	/* Square, small duty */
	buf = xm_get_sample_waveform(ctx, 4, 0, &len);
	for(i = 0; i < 0x30; ++i) buf[i] = INT16_MAX;
	for(; i < len; ++i) buf[i] = INT16_MIN;

	/* XXX: Kick */
	/* XXX: Pad */
	/* XXX: Drum */

	/* Noise */
	buf = xm_get_sample_waveform(ctx, 8, 0, &len);
	unsigned int next = 1;
	for(i = 0; i < len; ++i) {
		next = next * 8127 + 1; /* A very simple linear congruence generator, see rand(3) */
		buf[i] = next >> 16;
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
	       XM_BIG_ENDIAN ? "-fFLOAT_BE" : "-fFLOAT_LE",
	       NULL);
	__builtin_unreachable();
}
