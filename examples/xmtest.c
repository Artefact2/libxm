/* Author: Romain "Artefact2" Dalmaso <artefact2@gmail.com> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#include <xm.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>

int main(int argc, char** argv) {
	int fd, ret;
	off_t size;
	void* data;
	xm_context_t* ctx;
	float buffer[1024];
	uint32_t total = 0;

	if(argc != 2) {
		fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
		return 1;
	}

	fd = open(argv[1], O_RDONLY);
	size = lseek(fd, 0, SEEK_END);
	data = mmap(0, size, PROT_READ, MAP_SHARED, fd, 0);

	if((ret = xm_create_context(&ctx, data, 48000)) != 0) {
		fprintf(stderr, "xm_create_context() failed and returned %i\n", ret);
		return 1;
	}

	munmap(data, size); /* Data no longer needed, all necessary data was copied in the context */

	while(xm_get_loop_count(ctx) == 0) {
		xm_generate_samples(ctx, buffer, sizeof(buffer) / (2 * sizeof(float)));
		total += sizeof(buffer) / (2 * sizeof(float));
	}

	xm_free_context(ctx);
	printf("Total play time: %i seconds\n", (int)(total / 48000));
	return 0;
}
