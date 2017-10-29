/* Author: Romain "Artefact2" Dal Maso <artefact2@gmail.com> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#include "xm_internal.h"
#include <stdio.h>

#define OFFSET(ptr) do {									\
		(ptr) = (void*)((intptr_t)(ptr) - (intptr_t)ctx);	\
	} while(0)

#define FATAL(...) do {							\
		fprintf(stderr, __VA_ARGS__);			\
		exit(1);								\
	} while(0)

int main(int argc, char** argv) {
	xm_context_t* ctx;
	FILE* in;
	FILE* out;
	void* xmdata;
	size_t i, j, k;

	if(argc != 3) FATAL("Usage: %s <in.xm> <out.libxm>\n", argv[0]);

	in = fopen(argv[1], "rb");
	if(in == NULL) FATAL("input file %s not readable (fopen)\n", argv[1]);
	if(fseek(in, 0, SEEK_END)) FATAL("input file %s not seekable\n", argv[1]);
	xmdata = malloc(i = ftell(in));
	if(xmdata == NULL) FATAL("malloc failed to allocate %lu bytes\n", i);
	rewind(in);
	if(!fread(xmdata, i, 1, in)) FATAL("input file %s not readable (fread)\n", argv[1]);
	xm_create_context_safe(&ctx, xmdata, i, 48000);
	if(ctx == NULL) exit(1);
	free(xmdata);

	out = fopen(argv[2], "wb");
	if(out == NULL) FATAL("output file %s not writeable\n", argv[2]);

	fprintf(stderr, "%s: this format is highly non-portable. Check the README for more information.\n", argv[0]);

	/* Ugly pointer offsetting ahead */

	for(i = 0; i < ctx->module.num_patterns; ++i) {
		OFFSET(ctx->module.patterns[i].slots);
	}

	for(i = 0; i < ctx->module.num_instruments; ++i) {
		for(j = 0; j < ctx->module.instruments[i].num_samples; ++j) {
			if(ctx->module.instruments[i].samples[j].length > 1) {
				/* Half-ass delta encoding of samples, this compresses
				 * much better */
				if(ctx->module.instruments[i].samples[j].bits == 8) {
					for(k = ctx->module.instruments[i].samples[j].length - 1; k > 0; --k) {
						ctx->module.instruments[i].samples[j].data8[k] -= ctx->module.instruments[i].samples[j].data8[k-1];
					}
				} else {
					for(k = ctx->module.instruments[i].samples[j].length - 1; k > 0; --k) {
						ctx->module.instruments[i].samples[j].data16[k] -= ctx->module.instruments[i].samples[j].data16[k-1];
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
	fprintf(stderr, "%s: done writing %lu bytes\n", argv[0], ctx->ctx_size);

	xm_free_context(ctx);
	return 0;
}
