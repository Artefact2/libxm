/* Author: Romain "Artefact2" Dal Maso <artefact2@gmail.com> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

/* A stupid hack, but we need to know the internals of xm_context_s */
#include "../src/xm_internal.h"
#undef DEBUG

#include "testprog.h"

#define OFFSET(ptr) do {							\
		(ptr) = (void*)((intptr_t)(ptr) - (intptr_t)ctx);	\
	} while(0)

int main(int argc, char** argv) {
	xm_context_t* ctx;
	FILE* out;
	size_t i, j, k;
	
	if(argc != 3) FATAL("Usage: %s <in.xm> <out.libxm>\n", argv[0]);
	create_context_from_file(&ctx, 0, argv[1]);
	if(ctx == NULL) exit(1);
	out = fopen(argv[2], "wb");
	if(out == NULL) FATAL("cannot open output file %s for writing\n", argv[2]);

	fputs("WARNING! The resulting file is highly non-portable.\nIt will most likely not be loadable:\n* On a different CPU architecture\n* On a different libxm version\n* On a libxm compiled with a different compiler\n* On a libxm compiled with different cflags\nYou have been warned!\n", stderr);
	
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

	xm_free_context(ctx);	
	return 0;
}
