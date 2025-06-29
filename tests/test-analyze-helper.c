/* Author: Romain "Artefact2" Dalmaso <artefact2@gmail.com> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#include "common.h"
#include <string.h>

/* Load module in path, then dumb unmixed (2 floats per channel per frame) audio
   frames to standard output. */
static void generate_unmixed_f32ne(const char* path) {
	xm_context_t* ctx = load_module(path);
	xm_set_sample_rate(ctx, 44100);

	if(xm_get_number_of_channels(ctx) * 2 > 4096) {
		fprintf(stderr, "too many channels\n");
		exit(1);
	}
	const uint16_t numsamples = (uint16_t)
		(4096 / 2 / xm_get_number_of_channels(ctx));
	const uint16_t used_elems = (uint16_t)
		(numsamples * 2u * xm_get_number_of_channels(ctx));
	float buf[4096];

	while(xm_get_loop_count(ctx) == 0) {
		xm_generate_samples_unmixed(ctx, buf, numsamples);
		if(fwrite((char*)buf, used_elems * sizeof(float), 1, stdout) != 1) {
			exit(1);
		}
	}
}

/* Compare two float32 (native-endian) bitstreams. */
static int compare_f32ne_streams(const char* path1, const char* path2) {
	FILE* a = fopen(path1, "rb");
	FILE* b = fopen(path2, "rb");
	if(a == NULL || b == NULL) {
		return 1;
	}

	float buf1[2048];
	float buf2[2048];
	while(!feof(a) && !feof(b)) {
		size_t x = fread(buf1, sizeof(float), 2048, a);
		size_t y = fread(buf2, sizeof(float), 2048, b);
		if(x != y) return 1;
		for(size_t i = 0; i < x; ++i) {
			/* Maximum error per channel: .002 (max rounding
			   error for panning) * .25 (global
			   amplification) = .0005 */
			if(__builtin_fabsf(buf1[i] - buf2[i]) > .0005f) {
				return 1;
			}
		}
	}
	if(!feof(a) || !feof(b)) {
		return 1;
	}
	return 0;
}

int main(int argc, char** argv) {
	if(argc == 2) {
		generate_unmixed_f32ne(argv[1]);
	} else if(argc == 3) {
		if(strcmp(argv[1], "--analyze") == 0) {
			char* analyze_out =
				malloc((size_t)XM_ANALYZE_OUTPUT_SIZE);
			if(analyze_out == NULL) {
				perror("malloc");
				return 1;
			}
			xm_context_t* ctx = load_module(argv[2]);
			xm_analyze(ctx, analyze_out);
			fprintf(stdout, "%s\n", analyze_out);
		} else {
			return compare_f32ne_streams(argv[1], argv[2]);
		}
	} else {
		fprintf(stderr,
		        "Usage: \n"
		        "\t%s --analyze foo.xm\n"
		        "\t%s <(build-generic/%s foo.xm)"
		        " <(build-analyzed/%s foo.xm)\n",
		        argv[0], argv[0], argv[0], argv[0]);
		return 1;
	}

	return 0;
}
