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
#include <time.h>

const unsigned int channels = 2;
const unsigned int rate = 48000;
const size_t buffer_size = 1 << 15;

int main(int argc, char** argv) {
	int fd, ret;
	off_t size;
	void* data;
	xm_context_t* ctx;
	float buffer[buffer_size];
	clock_t start, end;
	double cpu_time, gen_time;
	unsigned int num_passes = 0;

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

	munmap(data, size);

	start = clock();
	while((clock() - start) < 5 * CLOCKS_PER_SEC) {
		++num_passes;
		xm_generate_samples(ctx, buffer, sizeof(buffer) / (channels * sizeof(float)));
	}
	end = clock();

	cpu_time = (double)(end - start) / CLOCKS_PER_SEC;
	gen_time = (double)num_passes * (double)buffer_size / (channels * rate);
	printf("Generated %.2f second(s) of %iHz audio in %.2f CPU seconds, playback speed is %.2fx\n", gen_time, rate, cpu_time, gen_time / cpu_time);

	xm_free_context(ctx);
	return 0;
}
