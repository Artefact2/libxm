/* Author: Romain "Artefact2" Dal Maso <artefact2@gmail.com> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#include <xm.h>
#include <stdio.h>

static const unsigned int channels = 2;
static const unsigned int rate = 48000;
static const size_t buffer_size = (1 << 8);

static void puts_uint32_be(uint32_t i) {
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

static void load_internal(xm_context_t** ctxp, unsigned int rate, const char* path) {
	size_t ctx_size;
	void* data;
	FILE* in = fopen(path, "rb");
	fread(&ctx_size, sizeof(size_t), 1, in);
	fseek(in, 0, SEEK_SET);
	data = malloc(ctx_size);
	fread(data, ctx_size, 1, in);
	xm_create_context_from_libxmize(ctxp, data, rate);
	free(data);
}

int main(int argc, char** argv) {
	xm_context_t* ctx;
	float buffer[buffer_size];

	if(argc != 2) return 1;
	load_internal(&ctx, rate, argv[1]);

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
