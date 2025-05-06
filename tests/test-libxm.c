/* Author: Romain "Artefact2" Dalmaso <artefact2@gmail.com> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#include <xm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_position(xm_context_t*);

/* Checks that channel1==channel2, channel3==channel4, etc. */
static int channelpairs_eq(xm_context_t*);

int main(int argc, char** argv) {
	if(argc != 3) {
		fprintf(stderr, "Usage: %s channelpairs_eq file.xm\n", argv[0]);
	}

	/* Read xm file contents to a buffer */
	FILE* xm_file = fopen(argv[2], "rb");
	if(xm_file == NULL) {
		perror("fopen");
		return 1;
	}
	if(fseek(xm_file, 0, SEEK_END)) {
		perror("fseek");
		return 1;
	}
	long xm_file_length = ftell(xm_file);
	if(xm_file_length == -1) return 1;
	rewind(xm_file);
	char* xm_file_data = malloc(xm_file_length);
	if(xm_file_data == NULL) return 1;
	if(fread(xm_file_data, xm_file_length, 1, xm_file) != 1) {
		perror("fread");
		return 1;
	}
	fclose(xm_file);

	/* Allocate xm context and free xm file data */
	xm_prescan_data_t* p = alloca(XM_PRESCAN_DATA_SIZE);
	if(xm_prescan_module(xm_file_data, xm_file_length, p) == false) return 1;
	char* ctx_buffer = malloc(xm_size_for_context(p));
	if(ctx_buffer == NULL) return 1;
	xm_context_t* ctx = xm_create_context(ctx_buffer, p, xm_file_data,
	                                      xm_file_length, 48000);
	free(xm_file_data);

	/* Perform the test */
	if(strcmp(argv[1], "channelpairs_eq") == 0) {
		return channelpairs_eq(ctx);
	}

	fprintf(stderr, "Invalid 1st argument\n");
	return 1;
}

static void print_position(xm_context_t* ctx) {
	uint8_t pot;
	uint8_t pat;
	uint8_t row;
	xm_get_position(ctx, &pot, &pat, &row, nullptr);
	fprintf(stderr, "At position %u in pot, pattern %u, row %u\n",
	        pot, pat, row);
}

static int channelpairs_eq(xm_context_t* ctx) {
	float frames[256];
	uint16_t chans = xm_get_number_of_channels(ctx);
	/* Make sure our buffer can at least fit one frame of unmixed data */
	if(chans > 128) return 1;
	/* Assume even number of channels */
	if(chans % 2) return 1;
	while(!xm_get_loop_count(ctx)) {
		xm_generate_samples_unmixed(ctx, frames, 128 / chans);
		/* Read LRLR of a channel pair */
		for(unsigned int i = 0; i < 256; i += 4) {
			if(frames[i] == frames[i+2]
			   && frames[i+1] == frames[i+3]) continue;
			fprintf(stderr, "Channel mismatch, LRLR=%f %f %f %f\n",
			        (double)frames[i], (double)frames[i+1],
			        (double)frames[i+2], (double)frames[i+3]);
			print_position(ctx);
			return 1;
		}
	}
	return 0;
}
