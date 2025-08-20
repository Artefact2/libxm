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
	for(uint16_t s = 0; s < xm_get_number_of_samples(ctx); ++s) {
		sample_data = xm_get_sample_waveform(ctx, s, &sample_length);
		if(sample_data == NULL) continue;
		memset(sample_data, 0,
		       sample_length * sizeof(xm_sample_point_t));
	}
}

__attribute__((noreturn))
int main(int argc, char** argv) {
	if(argc < 3) {
		fprintf(stderr,
		        "Usage: %s [--zero-all-waveforms] analyze|save|dump <in.xm|in.mod|in.s3m>\n",
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
	char* buf = malloc(ctx_size);
	if(buf == NULL) {
		perror("malloc");
		exit(1);
	}

	xm_context_t* ctx = xm_create_context(buf, p, xmdata,
	                                      (uint32_t)in_length);
	free(xmdata);

	for(int i = 1; i < argc - 2; ++i) {
		if(!strcmp("--zero-all-waveforms", argv[i])) {
			zero_waveforms(ctx);
		} else {
			fprintf(stderr, "unknown command-line argument: %s\n",
			        argv[i]);
			exit(1);
		}
	}

	char* action = argv[argc - 2];
	if(!strcmp("analyze", action)) {
		char* analyze_out = malloc((size_t)XM_ANALYZE_OUTPUT_SIZE);
		if(analyze_out == NULL) {
			perror("malloc");
			exit(1);
		}
		xm_analyze(ctx, analyze_out);
		fprintf(stdout, "%s\n", analyze_out);
		exit(0);
	} else if(!strcmp("save", action)) {
		/* TODO */
	} else if(!strcmp("dump", action)) {
		char* dump = malloc(xm_dump_size(ctx));
		if(dump == NULL) {
			perror("malloc");
			exit(1);
		}
		xm_dump_context(ctx, dump);
		if(!fwrite(dump, ctx_size, 1, stdout)) {
			perror("fwrite");
			exit(1);
		}
		exit(0);
	}

	fprintf(stderr, "unknown action %s, expected analyze, save or dump\n",
	        action);
	exit(1);
}
