/* Author: Romain "Artefact2" Dalmaso <artefact2@gmail.com> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#include <xm.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>

const size_t buffer_size = 1024;
const unsigned int channels = 2;
const unsigned int rate = 48000;

void puts_uint32_be(uint32_t i) {
	char* c = (char*)(&i);

	/* Assume little-endian, the decoder assumes it anyway */
	putchar(c[3]);
	putchar(c[2]);
	putchar(c[1]);
	putchar(c[0]);
}

int main(int argc, char** argv) {
	int fd, ret;
	off_t size;
	void* data;
	xm_context_t* ctx;
	float buffer[buffer_size];

	if(argc != 2) {
		fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
		return 1;
	}

	fd = open(argv[1], O_RDONLY);
	size = lseek(fd, 0, SEEK_END);
	data = mmap(0, size, PROT_READ, MAP_SHARED, fd, 0);

	if((ret = xm_create_context(&ctx, data, rate)) != 0) {
		fprintf(stderr, "xm_create_context() failed and returned %i\n", ret);
		return 1;
	}

	munmap(data, size); /* Data no longer needed, all necessary data was copied in the context */

	puts_uint32_be(0x2E736E64); /* .snd magic number */
	puts_uint32_be(24); /* Header size */
	puts_uint32_be((uint32_t)(-1)); /* Data size, unknown */
	puts_uint32_be(6); /* Encoding: 32-bit IEEE floating point */
	puts_uint32_be(rate); /* Sample rate */
	puts_uint32_be(channels); /* Number of interleaved channels */

	while(xm_get_loop_count(ctx) == 0) {
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
