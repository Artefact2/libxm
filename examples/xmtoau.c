/* Author: Romain "Artefact2" Dalmaso <artefact2@gmail.com> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#include "testprog.h"
#include <string.h>

static const unsigned int channels = 2;
static const unsigned int rate = 48000;
static const size_t buffer_size = (1 << 8); /* Use a small buffer to
											 * stop shortly after loop
											 * count gets changed */

void puts_uint32_be(uint32_t i) {
	char* c = (char*)(&i);

#if XM_BIG_ENDIAN
	putchar(c[0]);
	putchar(c[1]);
	putchar(c[2]);
	putchar(c[3]);
#else
	putchar(c[3]);
	putchar(c[2]);
	putchar(c[1]);
	putchar(c[0]);
#endif
}

void usage(char* progname) {
	FATAL("Usage: %s [--solo-channel X] [--solo-instrument Y] [--loops 1] <filename>\n", progname);
}

int main(int argc, char** argv) {
	xm_context_t* ctx;
	float buffer[buffer_size];
	uint16_t solochn = 0, soloinstr = 0;
	unsigned long loops = 1;

	for(size_t i = 1; i < argc; ++i) {
		if(!strcmp(argv[i], "--solo-channel")) {
			if(argc == i+1) FATAL("%s: expected argument after %s\n", argv[0], argv[i]);
			solochn = strtol(argv[i+1], NULL, 0);
			++i;
			continue;
		}
		if(!strcmp(argv[i], "--solo-instrument")) {
			if(argc == i+1) FATAL("%s: expected argument after %s\n", argv[0], argv[i]);
			soloinstr = strtol(argv[i+1], NULL, 0);
			++i;
			continue;
		}
		if(!strcmp(argv[i], "--loops")) {
			if(argc == i+1) FATAL("%s: expected argument after %s\n", argv[0], argv[i]);
			loops = strtol(argv[i+1], NULL, 0);
			++i;
			continue;
		}

		if(argc == i+1) break;
		FATAL("%s: unexpected argument %s\n", argv[0], argv[i]);
	}

	if(argc == 1 || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
		usage(argv[0]);
	}

	create_context_from_file(&ctx, rate, argv[argc - 1]);
	if(ctx == NULL) exit(1);
	
	xm_set_max_loop_count(ctx, loops);

	if(solochn > 0) {
		uint16_t nc = xm_get_number_of_channels(ctx);
		for(uint16_t i = 1; i <= nc; ++i) {
			xm_mute_channel(ctx, i, i != solochn);
		}
	}

	if(soloinstr > 0) {
		uint16_t ni = xm_get_number_of_instruments(ctx);
		for(uint16_t i = 1; i <= ni; ++i) {
			xm_mute_instrument(ctx, i, i != soloinstr);
		}
	}

	puts_uint32_be(0x2E736E64); /* .snd magic number */
	puts_uint32_be(28); /* Header size */
	puts_uint32_be((uint32_t)(-1)); /* Data size, unknown */
	puts_uint32_be(6); /* Encoding: 32-bit IEEE floating point */
	puts_uint32_be(rate); /* Sample rate */
	puts_uint32_be(channels); /* Number of interleaved channels */
	puts_uint32_be(0); /* Optional text information */

	while(loops == 0 || xm_get_loop_count(ctx) < loops) {
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

	xm_free_context(ctx);
	return 0;
}
