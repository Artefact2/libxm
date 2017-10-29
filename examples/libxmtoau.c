/* Author: Romain "Artefact2" Dal Maso <artefact2@gmail.com> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

/* A stupid hack, but we need to know the internals of xm_context_s */
#include "../src/xm_internal.h"
#undef DEBUG

#include "testprog.h"
#include <string.h>

#define OFFSET(ptr) do {							\
		(ptr) = (void*)((intptr_t)(ptr) + (intptr_t)(*ctxp));	\
	} while(0)

static const unsigned int channels = 2;
static const unsigned int rate = 48000;
static const size_t buffer_size = (1 << 8);

void puts_uint32_be(uint32_t i) {
	char* c = (char*)(&i);

	if(XM_BIG_ENDIAN) {
		putchar(c[0]);
		putchar(c[1]);
		putchar(c[2]);
		putchar(c[3]);
	} else {
		putchar(c[3]);
		putchar(c[2]);
		putchar(c[1]);
		putchar(c[0]);
	}
}

/* XXX: refactor me in libxm eventually */
static void load_internal(xm_context_t** ctxp, unsigned int rate, const char* path) {
	size_t ctx_size, i, j, k;
	FILE* in = fopen(path, "rb");

	fread(&ctx_size, sizeof(size_t), 1, in);

	*ctxp = malloc(ctx_size);
	fseek(in, 0, SEEK_SET);

	fread(*ctxp, ctx_size, 1, in);

	(*ctxp)->rate = rate;

	/* Reverse steps of xmconvert.c */

	OFFSET((*ctxp)->module.patterns);
	OFFSET((*ctxp)->module.instruments);
	OFFSET((*ctxp)->row_loop_count);
	OFFSET((*ctxp)->channels);

	for(i = 0; i < (*ctxp)->module.num_patterns; ++i) {
		OFFSET((*ctxp)->module.patterns[i].slots);
	}

	for(i = 0; i < (*ctxp)->module.num_instruments; ++i) {
		OFFSET((*ctxp)->module.instruments[i].samples);

		for(j = 0; j < (*ctxp)->module.instruments[i].num_samples; ++j) {
			OFFSET((*ctxp)->module.instruments[i].samples[j].data8);

			if((*ctxp)->module.instruments[i].samples[j].length > 1) {
				if((*ctxp)->module.instruments[i].samples[j].bits == 8) {
					for(k = 1; k < (*ctxp)->module.instruments[i].samples[j].length; ++k) {
						(*ctxp)->module.instruments[i].samples[j].data8[k] += (*ctxp)->module.instruments[i].samples[j].data8[k-1];
					}
				} else {
					for(k = 1; k < (*ctxp)->module.instruments[i].samples[j].length; ++k) {
						(*ctxp)->module.instruments[i].samples[j].data16[k] += (*ctxp)->module.instruments[i].samples[j].data16[k-1];
					}
				}
			}
		}
	}
}

int main(int argc, char** argv) {
	xm_context_t* ctx;
	float buffer[buffer_size];

	if(argc != 2) FATAL("Usage: %s <foo.libxm>\n", argv[0]);
	load_internal(&ctx, rate, argv[1]);
	if(ctx == NULL) exit(1);

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

	xm_free_context(ctx);
	return 0;
}
