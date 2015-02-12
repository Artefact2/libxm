/* Author: Romain "Artefact2" Dalmaso <artefact2@gmail.com> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#include <xm.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define FATAL(...) do {							\
		fprintf(stderr, __VA_ARGS__);			\
		fflush(stderr);							\
		exit(1);								\
	} while(0)

#define FATAL_ERR(...) do {								\
		perror(__VA_ARGS__);							\
		fflush(stderr);									\
		exit(1);										\
	} while(0)

static void create_context_from_file(xm_context_t** ctx, uint32_t rate, const char* filename) {
	FILE* xmfile;
	long size;
	int ret;

	xmfile = fopen(filename, "rb");

	if(xmfile == NULL)
		FATAL_ERR("Could not open input file");

	fseek(xmfile, 0, SEEK_END);
	size = ftell(xmfile);
	char data[size];
	rewind(xmfile);

	if(fread(data, 1, size, xmfile) != size)
		FATAL_ERR("Could not read input file");

	fclose(xmfile);

	if((ret = xm_create_context_safe(ctx, data, size, rate)) != 0)
		FATAL("Context creation failed (xm_create_context_safe() returned %i)\n", ret);
}
