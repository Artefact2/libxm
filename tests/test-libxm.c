/* Author: Romain "Artefact2" Dalmaso <artefact2@gmail.com> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#include "common.h"
#include <string.h>

static void print_position(const xm_context_t*);
static uint16_t modal_interpeak_distance(const float*, uint16_t, uint16_t);

/* Checks generated audio samples for channel1==channel2, channel3==channel4,
   etc. If swap_lr is true, swaps LR channels of each odd channel before
   comparing. If left_only is true, only compare left part of the stereo
   samples. */
static int channelpairs_eq(xm_context_t*, bool swap_lr, bool left_only);

/* Similar to channelpairs_eq(), but checks pitch of each channel pair. Assumes
   sawtooth samples only as pitch detection is really simplistic. */
static int channelpairs_pitcheq(xm_context_t*);

/* Checks generated audio samples for pattern0==pattern1. */
static int pat0_pat1_eq(xm_context_t*);

static int channelpairs_pitcheq(xm_context_t*);


int main(int argc, char** argv) {
	if(argc != 3) {
		fprintf(stderr, "Usage: %s <method> <file.xm>\n", argv[0]);
		return 1;
	}

	xm_context_t* ctx = load_module(argv[2]);
	xm_set_sample_rate(ctx, 48000);

	/* Perform the test */
	if(strcmp(argv[1], "channelpairs_eq") == 0) {
		return channelpairs_eq(ctx, false, false);
	} else if(strcmp(argv[1], "channelpairs_lreqrl") == 0) {
		return channelpairs_eq(ctx, true, false);
	} else if(strcmp(argv[1], "channelpairs_leql") == 0) {
		return channelpairs_eq(ctx, false, true);
	} else if(strcmp(argv[1], "channelpairs_pitcheq") == 0) {
		return channelpairs_pitcheq(ctx);
	} else if(strcmp(argv[1], "pat0_pat1_eq") == 0) {
		return pat0_pat1_eq(ctx);
	}

	fprintf(stderr, "Invalid 1st argument\n");
	return 1;
}

static void print_position(const xm_context_t* ctx) {
	uint8_t pot;
	uint8_t pat;
	uint8_t row;
	xm_get_position(ctx, &pot, &pat, &row, nullptr);
	fprintf(stderr, "At position %X in pot, pattern %X, row %X\n",
	        pot, pat, row);
}

static int channelpairs_eq(xm_context_t* ctx, bool swap_lr, bool left_only) {
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
			if(!swap_lr && frames[i] == frames[i+2]
			   && (left_only || frames[i+1] == frames[i+3]))
				continue;
			if(swap_lr && frames[i] == frames[i+3]
			   && (left_only || frames[i+1] == frames[i+2]))
				continue;
			fprintf(stderr, "Channel mismatch, LRLR=%f %f %f %f\n",
			        (double)frames[i], (double)frames[i+1],
			        (double)frames[i+2], (double)frames[i+3]);
			print_position(ctx);
			return 1;
		}
	}
	return 0;
}

static int channelpairs_pitcheq(xm_context_t* ctx) {
	if(xm_get_number_of_channels(ctx) != 2) return 1;
	uint8_t bpm;
	xm_get_playing_speed(ctx, &bpm, nullptr);
	if(bpm != 32) return 1;
	float frames[3750*4]; /* 48000 Hz, 32 BPM => 1 tick is 3750 samples */
	while(!xm_get_loop_count(ctx)) {
		xm_generate_samples_unmixed(ctx, frames, 3750);
		for(uint16_t i = 0; i < 2; ++i) {
			uint16_t a = modal_interpeak_distance(frames+i,
			                                      3750, 4);
			uint16_t b = modal_interpeak_distance(frames+i+2,
			                                      3750, 4);
			/* Allow some error caused by period rounding */
			if(a != b && a != b+1 && a != b-1) {
				fprintf(stderr, "MIPD mismatch, %u != %u\n",
				        a, b);
				print_position(ctx);
				return 1;
			}
		}
	}
	return 0;
}

static int pat0_pat1_eq(xm_context_t* ctx0) {
	if(xm_get_module_length(ctx0) != 2) {
		fprintf(stderr, "This method requires 2 patterns "
		        "with a pattern order table length of 2\n");
		return 1;
	}

	/* Copy the context */
	char* buf = malloc(xm_context_size(ctx0));
	if(buf == NULL) return 1;
	xm_context_to_libxm(ctx0, buf);
	xm_context_t* ctx1 = xm_create_context_from_libxm(buf);
	xm_seek(ctx1, 1, 0, 0);

	float frames0[128], frames1[128];
	uint8_t idx;
	uint32_t smp_count;
	while(xm_get_position(ctx0, &idx, NULL, NULL, &smp_count), idx == 0) {
		xm_generate_samples(ctx0, frames0, 64);
		xm_generate_samples(ctx1, frames1, 64);
		for(uint8_t i = 0; i < 128; ++i) {
			if(frames0[i] == frames1[i]) continue;
			fprintf(stderr,
			        "Found mismatch at frame position %u: "
			        "pat0=%f pat1=%f\n", smp_count + i/2,
			        (double)frames0[i], (double)frames1[i]);
			print_position(ctx0);
			print_position(ctx1);
			return 1;
		}
	}

	return 0;
}

static uint16_t modal_interpeak_distance(const float* data, uint16_t count,
                                         uint16_t stride) {
	if(count < 3) return 0;

	uint16_t* counts = alloca(sizeof(uint16_t) * count);
	__builtin_memset(counts, 0, sizeof(uint16_t) * count);

	uint16_t last_peak_idx = count;
	for(uint16_t i = 2; i < count; ++i) {
		if(data[(i-1)*stride] < data[(i-2)*stride] ||
		   data[(i-1)*stride] <= data[i*stride]) {
			continue;
		}
		if(last_peak_idx < count) {
			counts[i-1 - last_peak_idx]++;
		}
		last_peak_idx = i-1;
	}

	last_peak_idx = 0;
	uint16_t last_peak_count = 0;
	for(uint16_t i = 1; i < count; ++i) {
		if(counts[i] < last_peak_count) continue;
		last_peak_count = counts[i];
		last_peak_idx = i;
	}

	return last_peak_idx;
}
