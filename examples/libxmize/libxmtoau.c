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

static const unsigned char header[] = {
	'.', 's', 'n', 'd', /* .snd magic */
	0, 0, 0, 28, /* header size */
	255, 255, 255, 255, /* data size (unknown) */
	0, 0, 0, 6, /* encoding (float32be) */
	0, 0, 187, 128, /* sample rate (48000) */
	0, 0, 0, 2, /* channels */
	0, 0, 0, 0, /* description string (min 4 bytes) */
};

#if XM_LITTLE_ENDIAN
static void byteswap32(uint32_t* i) {
	/* (optimised into single bswap instruction) */
	*i = (*i << 24)
		| (*i << 8 & 0xFF0000)
		| (*i >> 8 & 0xFF00)
		| (*i >> 24);
}
#endif

int ENTRY(void) {
	static float buffer[128];
	xm_context_t* ctx;
	char* stdin_data = NULL;
	uint32_t stdin_alloc_length = 0, stdin_data_idx = 0;
	ssize_t r_ret;

	do {
		if(stdin_data_idx == stdin_alloc_length) {
			stdin_alloc_length += 1048576;
			stdin_data = realloc(stdin_data, stdin_alloc_length);
		}
		r_ret = read(STDIN_FILENO,
		             stdin_data + stdin_data_idx,
		             stdin_alloc_length - stdin_data_idx);
		stdin_data_idx += r_ret;
	} while(r_ret > 0);

	ctx = xm_create_context_from_libxm(stdin_data, 48000);

	maybe_assert_eq(write(STDOUT_FILENO, header, sizeof(header)), (ssize_t)sizeof(header));

	while(!xm_get_loop_count(ctx)) {
		xm_generate_samples(ctx, buffer, sizeof(buffer) / (2 * sizeof(float)));
		#if XM_LITTLE_ENDIAN
		for(size_t k = 0; k < sizeof(buffer) / sizeof(float); ++k) {
			byteswap32((uint32_t*)&(buffer[k]));
		}
		#endif
		maybe_assert_eq(write(STDOUT_FILENO, buffer, sizeof(buffer)), (ssize_t)sizeof(buffer));
	}

	exit(0);
}
