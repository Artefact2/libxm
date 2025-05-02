/* Author: Romain "Artefact2" Dalmaso <artefact2@gmail.com> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#include <xm.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define DEBUG(...) do {	  \
		fprintf(stderr, __VA_ARGS__); \
		fflush(stderr); \
	} while(0)

#define DEBUG_ERR(...) do {	  \
		perror(__VA_ARGS__); \
		fflush(stderr); \
	} while(0)

#define FATAL(...) do {	  \
		fprintf(stderr, __VA_ARGS__); \
		fflush(stderr); \
		exit(1); \
	} while(0)

#define FATAL_ERR(...) do {	  \
		perror(__VA_ARGS__); \
		fflush(stderr); \
		exit(1); \
	} while(0)

static xm_context_t* create_context_from_file(uint32_t rate, const char* filename) {
	int xmfiledes;
	off_t size;

	xmfiledes = open(filename, O_RDONLY);
	if(xmfiledes == -1) {
		DEBUG_ERR("Could not open input file");
		return NULL;
	}

	size = lseek(xmfiledes, 0, SEEK_END);
	if(size == -1) {
		close(xmfiledes);
		DEBUG_ERR("lseek() failed");
		return NULL;
	}

	/* NB: using a VLA here was a bad idea, as the size of the
	 * module file has no upper bound, whereas the stack has a
	 * very finite (and usually small) size. Using mmap bypasses
	 * the issue (at the cost of portability…). */
	char* data = mmap(NULL, size, PROT_READ, MAP_SHARED, xmfiledes, (off_t)0);
	if(data == MAP_FAILED)
		FATAL_ERR("mmap() failed");

	char p_raw[XM_PRESCAN_DATA_SIZE];
	xm_prescan_data_t* p = (xm_prescan_data_t*)p_raw;
	if(xm_prescan_module(data, size, p) == false) {
		DEBUG_ERR("xm_prescan_module() failed");
		return NULL;
	}

	uint32_t ctx_size = xm_size_for_context(p);
	char* pool = mmap(0, ctx_size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	if(data == MAP_FAILED)
		FATAL_ERR("mmap() failed");

	xm_context_t* ctx = xm_create_context(pool, p, data, size, rate);
	munmap(data, size);
	close(xmfiledes);
	return ctx;
}
