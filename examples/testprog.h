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
	int xmfiledes;
	off_t size;

	xmfiledes = open(filename, O_RDONLY);
	if(xmfiledes == -1)
		FATAL_ERR("Could not open input file");

	size = lseek(xmfiledes, 0, SEEK_END);
	if(size == -1)
		FATAL_ERR("lseek() failed");

	/* NB: using a VLA here was a bad idea, as the size of the
	 * module file has no upper bound, whereas the stack has a
	 * very finite (and usually small) size. Using mmap bypasses
	 * the issue (at the cost of portability…). */
	char* data = mmap(NULL, size, PROT_READ, MAP_SHARED, xmfiledes, (off_t)0);
	if(data == MAP_FAILED)
		FATAL_ERR("mmap() failed");

	switch(xm_create_context_safe(ctx, data, size, rate)) {
		
	case 0:
		break;

	case 1:
		FATAL("could not create context: module is not sane");
		break;

	case 2:
		FATAL("could not create context: malloc failed");
		break;
		
	default:
		FATAL("could not create context: unknown error");
		break;
		
	}
	
	munmap(data, size);
	close(xmfiledes);
}
