/* Author: Romain "Artefact2" Dalmaso <artefact2@gmail.com> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#include "common.h"

xm_context_t* load_module(const char* path) {
	/* Read xm file contents to a buffer */
	FILE* xm_file = fopen(path, "rb");
	if(xm_file == NULL) {
		perror("fopen");
		exit(1);
	}
	if(fseek(xm_file, 0, SEEK_END)) {
		perror("fseek");
		exit(1);
	}
	long xm_file_length = ftell(xm_file);
	static_assert(UINT32_MAX <= SIZE_MAX);
	if(xm_file_length < 0 || xm_file_length > UINT32_MAX) {
		fprintf(stderr, "input file too large\n");
		exit(1);
	}
	rewind(xm_file);
	char* xm_file_data = malloc((size_t)xm_file_length);
	if(xm_file_data == NULL) {
		perror("malloc");
		exit(1);
	}
	if(fread(xm_file_data, (size_t)xm_file_length, 1, xm_file) != 1) {
		perror("fread");
		exit(1);
	}
	fclose(xm_file);

	/* Allocate xm context and free xm file data */
	xm_prescan_data_t* p = alloca(XM_PRESCAN_DATA_SIZE);
	if(!xm_prescan_module(xm_file_data, (uint32_t)xm_file_length, p)) {
		exit(1);
	}
	char* ctx_buffer = malloc(xm_size_for_context(p));
	if(ctx_buffer == NULL) {
		perror("malloc");
		exit(1);
	}
	xm_context_t* ctx = xm_create_context(ctx_buffer, p, xm_file_data,
	                                      (uint32_t)xm_file_length);
	free(xm_file_data);
	return ctx;
}
