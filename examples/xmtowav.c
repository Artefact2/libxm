/* Author: Romain "Artefact2" Dalmaso <artefact2@gmail.com> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#include "testprog.h"

static const unsigned int channels = 2;
static const unsigned int rate = 48000;
static const size_t buffer_size = 48000;

void puts_uint32_le(uint32_t i, FILE* f) {
	char* c = (char*)(&i);

#if XM_BIG_ENDIAN
	putc(c[3], f);
	putc(c[2], f);
	putc(c[1], f);
	putc(c[0], f);
#else
	putc(c[0], f);
	putc(c[1], f);
	putc(c[2], f);
	putc(c[3], f);
#endif
}

void puts_uint16_le(uint16_t i, FILE* f) {
	char* c = (char*)(&i);

#if XM_BIG_ENDIAN
	putc(c[1], f);
	putc(c[0], f);
#else
	putc(c[0], f);
	putc(c[1], f);
#endif
}

int main(int argc, char** argv) {
	xm_context_t* ctx;
	FILE* out;
	uint32_t num_samples = 0;
	float buffer[buffer_size];

	if(argc != 3)
		FATAL("Usage: %s <xm-file-input> <wav-file-out>\n", argv[0]);

	create_context_from_file(&ctx, rate, argv[1]);
	if(ctx == NULL) exit(1);
	xm_set_max_loop_count(ctx, 1);

	out = fopen(argv[2], "wb");
	if(out == NULL) FATAL_ERR("could not open output file for writing");

	/* WAVE format info taken from
	 * http://www-mmsp.ece.mcgill.ca/Documents/AudioFormats/WAVE/WAVE.html */

	/* Unlike AU, WAVE files cannot have an unknown length. This
	 * is why we can't write directly to stdout (we need to rewind
	 * because module length is hard to know. */

	fputs("RIFF", out);
	puts_uint32_le(0, out); /* Chunk size. Will be filled later. */
	
	fputs("WAVE", out);

	fputs("fmt ", out); /* Start format chunk */
	puts_uint32_le(16, out); /* Format chunk size */
	puts_uint16_le(3, out); /* IEEE float sample data */
	puts_uint16_le(channels, out); /* Number of channels */
	puts_uint32_le(rate, out);  /* Frames/sec (sampling rate) */
	puts_uint32_le(rate * channels * sizeof(float), out); /* nAvgBytesPerSec ? */
	puts_uint16_le(channels * sizeof(float), out); /* nBlockAlign ? */
	puts_uint16_le(8 * sizeof(float), out); /* wBitsPerSample ? */

	fputs("data", out); /* Start data chunk */
	puts_uint32_le(0, out); /* Data chunk size. Will be filled later. */
	
	while(xm_get_loop_count(ctx) == 0) {
		xm_generate_samples(ctx, buffer, buffer_size / channels);
		num_samples += buffer_size;
		for(size_t k = 0; k < buffer_size; ++k) {
			union {
				float f;
				uint32_t i;
			} u;

			u.f = buffer[k];
			puts_uint32_le(u.i, out);
		}
	}

	fseek(out, 4, SEEK_SET);
	puts_uint32_le(36 + num_samples * sizeof(float), out);

	fseek(out, 40, SEEK_SET);
	puts_uint32_le(num_samples * sizeof(float), out);

	fclose(out);
	xm_free_context(ctx);
	return 0;
}
