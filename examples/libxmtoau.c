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

static const unsigned char header[] = {
	'.', 's', 'n', 'd', /* .snd magic */
	0, 0, 0, 28, /* header size */
	255, 255, 255, 255, /* data size (unknown) */
	0, 0, 0, 6, /* encoding (ieee float32) */
	0, 0, 187, 128, /* sample rate (48000) */
	0, 0, 0, 2, /* channels */
	0, 0, 0, 0, /* description string (min 4 bytes) */
};

static void byteswap32(uint32_t* i) {
	/* (optimised into single bswap instruction) */
	*i = (*i << 24)
		| (*i << 8 & 0xFF0000)
		| (*i >> 8 & 0xFF00)
		| (*i >> 24);
}

void _start(void) {
	static float buffer[128];
	xm_context_t* ctx;
	char* data;
	ssize_t datalen;

	maybe_assert_eq(read(0, &datalen, sizeof(datalen)), sizeof(datalen));
	data = malloc(datalen);
	((size_t*)data)[0] = datalen;
	maybe_assert_eq(read(0, data + sizeof(datalen), datalen - (ssize_t)sizeof(datalen)), datalen - (ssize_t)sizeof(datalen));
	xm_create_context_from_libxmize(&ctx, data, 48000);

	maybe_assert_eq(write(STDOUT_FILENO, header, sizeof(header)), (ssize_t)sizeof(header));

	while(!xm_get_loop_count(ctx)) {
		xm_generate_samples(ctx, buffer, sizeof(buffer) / (2 * sizeof(float)));
		if(!XM_BIG_ENDIAN) {
			for(size_t k = 0; k < sizeof(buffer) / sizeof(float); ++k) {
				byteswap32((uint32_t*)&(buffer[k]));
			}
		}
		maybe_assert_eq(write(STDOUT_FILENO, buffer, sizeof(buffer)), (ssize_t)sizeof(buffer));
	}

	exit(0);
}
