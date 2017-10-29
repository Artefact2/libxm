/* Author: Romain "Artefact2" Dalmaso <artefact2@gmail.com> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#include <stdio.h>
#include <string.h>

int main(int argc, char** argv) {
	if(argc != 4) {
		fprintf(stderr, "Usage: %s <resource_name> <infile> <outfile.h>\n", argv[0]);
		return 1;
	}

	FILE *in, *out;
	int c;

	in = fopen(argv[2], "r");
	if(in == NULL) {
		perror("could not open input file");
		return 1;
	}

	out = fopen(argv[3], "w");
	if(out == NULL) {
		perror("could not open output file");
		return 1;
	}

	fprintf(out, "static const char %s[] = {\n", argv[1]);

	while((c = fgetc(in)) != EOF) {
		fprintf(out, "%hhi,", c);
	}

	fprintf(out, "0\n};\n");
	fclose(in);
	fclose(out);
	return 0;
}
