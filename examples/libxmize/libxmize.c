/* Author: Romain "Artefact2" Dal Maso <artefact2@gmail.com> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#include <xm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* XXX: implement per-waveform zapping */
static void zero_waveforms(xm_context_t* ctx) {
	xm_sample_point_t* sample_data;
	uint32_t sample_length;
	for(uint8_t i = 1; i <= xm_get_number_of_instruments(ctx); ++i) {
		for(uint8_t s = 0; s < xm_get_number_of_samples(ctx, i); ++s) {
			sample_data = xm_get_sample_waveform(ctx, i, s,
			                                     &sample_length);
			if(sample_data == NULL) continue;
			memset(sample_data, 0, sample_length
			       * sizeof(xm_sample_point_t));
		}

	}
}

__attribute__((noreturn))
int main(int argc, char** argv) {
	if(argc < 2) {
		fprintf(stderr,
		        "Usage: %s [--zero-all-waveforms] [--analyze] <in.xm>\n",
		        argv[0]);
		exit(1);
	}

	FILE* in = fopen(argv[argc - 1], "rb");
	if(in == NULL) {
		perror("fopen");
		exit(1);
	}

	if(fseek(in, 0, SEEK_END)) {
		perror("fseek");
		exit(1);
	}

	long in_length = ftell(in);
	if(in_length == -1) {
		perror("ftell");
		exit(1);
	}
	if(in_length > UINT32_MAX) {
		fprintf(stderr, "input file too large\n");
		exit(1);
	}
	char* xmdata = malloc((size_t)in_length);
	if(xmdata == NULL) {
		perror("malloc");
		exit(1);
	}
	rewind(in);

	if(!fread(xmdata, (size_t)in_length, 1, in)) {
		perror("fread");
		exit(1);
	}

	xm_prescan_data_t* p = alloca(XM_PRESCAN_DATA_SIZE);
	if(!xm_prescan_module(xmdata, (uint32_t)in_length, p)) {
		fprintf(stderr, "xm_prescan_module() failed\n");
		exit(1);
	}

	uint32_t ctx_size = xm_size_for_context(p);
	char* buf = malloc(2 * ctx_size);
	char* libxmized = buf + ctx_size;

	xm_context_t* ctx = xm_create_context(buf, p, xmdata,
	                                      (uint32_t)in_length, 48000);

	for(int i = 1; i < argc - 1; ++i) {
		if(!strcmp("--zero-all-waveforms", argv[i])) {
			zero_waveforms(ctx);
		} else if(!strcmp("--analyze", argv[i])) {
			char* analyze_out =
				malloc((size_t)XM_ANALYZE_OUTPUT_SIZE);
			if(analyze_out == NULL) {
				perror("malloc");
				exit(1);
			}
			xm_analyze(ctx, analyze_out);
			fprintf(stdout, "%s\n", analyze_out);
			free(analyze_out);
			xm_reset_context(ctx);
			exit(0);
		} else {
			fprintf(stderr, "unknown command-line argument: %s", argv[i]);
			exit(1);
		}
	}

	xm_context_to_libxm(ctx, libxmized);

	if(!fwrite(libxmized, ctx_size, 1, stdout)) {
		perror("fwrite");
		exit(1);
	}

	exit(0);
}
