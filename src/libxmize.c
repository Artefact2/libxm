/* Author: Romain "Artefact2" Dal Maso <artefact2@gmail.com> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#include "xm_internal.h"
#include <stdio.h>
#include <string.h>

#define OFFSET(ptr) do {									\
		(ptr) = (void*)((intptr_t)(ptr) - (intptr_t)ctx);	\
	} while(0)

#define FATAL(...) do {							\
		fprintf(stderr, __VA_ARGS__);			\
		exit(1);								\
	} while(0)

/* XXX: implement per-waveform zapping */
/* XXX: maybe also wipe loop info, finetune, length etc */
/* XXX: properly replace sample by a 0-length waveform instead of zeroing buffer? harder than it sounds */
static size_t zero_waveforms(xm_context_t* ctx) {
	size_t i, j, total_saved_bytes = 0;

	for(i = 0; i < ctx->module.num_instruments; ++i) {
		xm_instrument_t* inst = &(ctx->module.instruments[i]);

		for(j = 0; j < inst->num_samples; ++j) {
			xm_sample_t* sample = &(inst->samples[j]);

			size_t saved_bytes = (sample->bits == 8) ? sample->length : sample->length * 2;
			memset(sample->data8, 0, saved_bytes);
			total_saved_bytes += saved_bytes;
		}
	}

	return total_saved_bytes;
}

static void analyze(const char* arg0, xm_context_t* ctx) {
	printf("%s: detected features: cmake", arg0);

	printf(" -D XM_FREQUENCY_TYPES=%d",
	        #if XM_FREQUENCY_TYPES != 3
                XM_FREQUENCY_TYPES
	        #else
	        ctx->module.frequency_type == XM_LINEAR_FREQUENCIES ? 1 : 2
	        #endif
	       );

	printf("\n");
}

int main(int argc, char** argv) {
	xm_context_t* ctx;
	FILE* in;
	FILE* out;
	void* xmdata;
	size_t i, j;

	if(argc < 3) FATAL("Usage: %s [--zero-all-waveforms] <in.xm> <out.libxm>\n", argv[0]);

	printf("%s: this format is highly non-portable. Check the README for more information.\n", argv[0]);

	in = fopen(argv[argc - 2], "rb");
	if(in == NULL) FATAL("input file %s not readable (fopen)\n", argv[argc - 2]);
	if(fseek(in, 0, SEEK_END)) FATAL("input file %s not seekable\n", argv[argc - 2]);
	xmdata = malloc(i = ftell(in));
	if(xmdata == NULL) FATAL("malloc failed to allocate %lu bytes\n", i);
	rewind(in);
	if(!fread(xmdata, i, 1, in)) FATAL("input file %s not readable (fread)\n", argv[argc - 2]);
	xm_create_context_safe(&ctx, xmdata, i, 0); /* sample rate of 0 will be overwritten at load time */
	if(ctx == NULL) exit(1);
	free(xmdata);

	analyze(argv[0], ctx);

	out = fopen(argv[argc - 1], "wb");
	if(out == NULL) FATAL("output file %s not writeable\n", argv[argc - 1]);

	if(!strcmp("--zero-all-waveforms", argv[1])) {
		printf("%s: zeroing waveforms, saved %lu bytes.\n", argv[0], zero_waveforms(ctx));
	}

	/* Ugly pointer offsetting ahead */

	for(i = 0; i < ctx->module.num_patterns; ++i) {
		OFFSET(ctx->module.patterns[i].slots);
	}

	for(i = 0; i < ctx->module.num_instruments; ++i) {
		for(j = 0; j < ctx->module.instruments[i].num_samples; ++j) {
			if(XM_LIBXMIZE_DELTA_SAMPLES) {
				if(ctx->module.instruments[i].samples[j].length > 1) {
					/* Half-ass delta encoding of samples, this compresses
					 * much better */
					if(ctx->module.instruments[i].samples[j].bits == 8) {
						for(size_t k = ctx->module.instruments[i].samples[j].length - 1; k > 0; --k) {
							ctx->module.instruments[i].samples[j].data8[k] -= ctx->module.instruments[i].samples[j].data8[k-1];
						}
					} else {
						for(size_t k = ctx->module.instruments[i].samples[j].length - 1; k > 0; --k) {
							ctx->module.instruments[i].samples[j].data16[k] -= ctx->module.instruments[i].samples[j].data16[k-1];
						}
					}
				}
			}

			OFFSET(ctx->module.instruments[i].samples[j].data8);
		}

		OFFSET(ctx->module.instruments[i].samples);
	}

	OFFSET(ctx->module.patterns);
	OFFSET(ctx->module.instruments);
	OFFSET(ctx->row_loop_count);
	OFFSET(ctx->channels);

	if(!fwrite(ctx, ctx->ctx_size, 1, out)) FATAL("fwrite() failed writing %lu bytes\n", ctx->ctx_size);
	fclose(out);
	printf("%s: done writing %lu bytes.\n", argv[0], ctx->ctx_size);

	xm_free_context(ctx);
	return 0;
}
