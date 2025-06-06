/* Author: Romain "Artefact2" Dal Maso <artefact2@gmail.com> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#include <xm.h>
#include <assert.h>
#include <unistd.h>
#include <stdlib.h>

#ifdef NDEBUG
#define maybe_assert_eq(x, y) (x)
#else
#define maybe_assert_eq(x, y) assert((x) == (y))
#endif

static_assert(sizeof(float) == sizeof(uint32_t));

static const unsigned char header[] = {
	'.', 's', 'n', 'd', /* .snd magic */
	0, 0, 0, 28, /* header size */
	255, 255, 255, 255, /* data size (unknown) */
	0, 0, 0, 6, /* encoding (float32be) */
	0, 0, 172, 68, /* sample rate (44100) */
	0, 0, 0, 2, /* channels */
	0, 0, 0, 0, /* description string (min 4 bytes) */
};

[[maybe_unused]] static void byteswap32(uint32_t* i) {
	/* (optimised into single bswap instruction) */
	*i = (*i << 24)
		| (*i << 8 & 0xFF0000)
		| (*i >> 8 & 0xFF00)
		| (*i >> 24);
}

__attribute__((noreturn))
int ENTRY(void) {
	static float buffer[60];
	/* Don't do this in real programs. We are just assuming a realistically
	   high upper bound for stdin, and also that the kernel only actually
	   allocates memory on first access (Linux does) to save a few bytes of
	   boilerplate. */
	char* buf = malloc(128*1024*1024);
	xm_context_t* ctx;
	uint32_t buf_idx = 0;
	ssize_t r_ret;

	do {
		buf_idx += (uint32_t)(r_ret = read(STDIN_FILENO,
		                                   buf + buf_idx,
		                                   sizeof(buffer)));
	} while(r_ret > 0);

	ctx = xm_create_context_from_libxm(buf, 44100);
	maybe_assert_eq(write(STDOUT_FILENO, header, sizeof(header)),
	                (ssize_t)sizeof(header));

	while(!xm_get_loop_count(ctx)) {
		xm_generate_samples(ctx, buffer, sizeof(buffer)
		                    / (2 * sizeof(float)));
		#if XM_LITTLE_ENDIAN
		for(size_t k = sizeof(buffer) / sizeof(float); k; k--) {
			byteswap32((uint32_t*)(buffer
			                       + sizeof(buffer) / sizeof(float)
			                       - k));
		}
		#endif
		maybe_assert_eq(write(STDOUT_FILENO, buffer, sizeof(buffer)),
		                (ssize_t)sizeof(buffer));
	}

	exit(0);
}
