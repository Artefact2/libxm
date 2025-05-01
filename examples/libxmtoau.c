/* Author: Romain "Artefact2" Dal Maso <artefact2@gmail.com> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <xm.h>

#ifdef NDEBUG
#define maybe_assert_eq(x, y) (x)
#else
#define maybe_assert_eq(x, y) assert((x) == (y))
#endif

static const unsigned int channels = 2;
static const unsigned int rate = 48000;
static const size_t buffer_size = (1 << 12);

static void puts_uint32_be(uint32_t i) {
	if(!XM_BIG_ENDIAN) {
		/* (optimised into single bswap instruction) */
		i = (i << 24)
			| (i << 8 & 0xFF0000)
			| (i >> 8 & 0xFF00)
			| (i >> 24);
	}
	maybe_assert_eq(write(STDOUT_FILENO, &i, 4), 4);
}

void _start(void) {
	float buffer[buffer_size];
	xm_context_t* ctx;
	char* data;
	ssize_t datalen;

	maybe_assert_eq(read(0, &datalen, sizeof(datalen)), sizeof(datalen));
	data = malloc(datalen);
	((size_t*)data)[0] = datalen;
	maybe_assert_eq(read(0, data + sizeof(datalen), datalen - (ssize_t)sizeof(datalen)), datalen - (ssize_t)sizeof(datalen));
	xm_create_context_from_libxmize(&ctx, data, rate);

	puts_uint32_be(0x2E736E64); /* .snd magic number */
	puts_uint32_be(28); /* Header size */
	puts_uint32_be((uint32_t)(-1)); /* Data size, unknown */
	puts_uint32_be(6); /* Encoding: 32-bit IEEE floating point */
	puts_uint32_be(rate); /* Sample rate */
	puts_uint32_be(channels); /* Number of interleaved channels */
	puts_uint32_be(0); /* Optional text information */

	while(!xm_get_loop_count(ctx)) {
		xm_generate_samples(ctx, buffer, sizeof(buffer) / (channels * sizeof(float)));
		for(size_t k = 0; k < buffer_size; ++k) {
			union {
				float f;
				uint32_t i;
			} u;

			u.f = buffer[k];
			puts_uint32_be(u.i);
		}
	}

	exit(0);
}
