/* Author: Romain "Artefact2" Dalmaso <artefact2@gmail.com> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#include "testprog.h"
#include <time.h>

static const unsigned int channels = 2;
static const unsigned int rate = 48000;
static const size_t buffer_size = 1 << 15; /* Use a relatively big
											* buffer to minimize loop
											* impact on
											* measurements */
static const unsigned int ideal_running_time = 5;

int main(int argc, char** argv) {
	xm_context_t* ctx;
	float buffer[buffer_size];
	clock_t start, end;
	double cpu_time, gen_time;
	unsigned int num_passes = 0;

	if(argc != 2)
		FATAL("Usage: %s <filename>\n", argv[0]);

	create_context_from_file(&ctx, rate, argv[1]);
	if(ctx == NULL) exit(1);

	start = clock();
	while((clock() - start) < ideal_running_time * CLOCKS_PER_SEC) {
		++num_passes;
		xm_generate_samples(ctx, buffer, sizeof(buffer) / (channels * sizeof(float)));
	}
	end = clock();

	cpu_time = (double)(end - start) / CLOCKS_PER_SEC;
	gen_time = (double)num_passes * (double)buffer_size / (channels * rate);
	printf("Generated %.2f second(s) of %iHz audio in %.2f CPU seconds, playback speed is %.2fx\n",
		   gen_time, rate, cpu_time, gen_time / cpu_time);

	xm_free_context(ctx);
	return 0;
}
