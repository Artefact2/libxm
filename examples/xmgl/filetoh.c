/* Author: Romain "Artefact2" Dalmaso <artefact2@gmail.com> */

/* This program is free software. It comes without any warranty, to the
 * extent permitted by applicable law. You can redistribute it and/or
 * modify it under the terms of the Do What The Fuck You Want To Public
 * License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */

#include <stdio.h>
#include <string.h>

/* Usage: ./program <resource_name> <resource_file>
 *
 * Will create a file named resource_name.h containing the resource
 * file as a static, const byte array.
 */
int main(int argc, char** argv) {
	FILE *in, *out;
	int c;

	size_t rlen = strlen(argv[1]);
	char outname[rlen + 2];
	memcpy(outname, argv[1], rlen);
	outname[rlen] = '.';
	outname[rlen + 1] = 'h';
	
	in = fopen(argv[2], "r");
	if(in == NULL) {
		perror("could not open input file");
		return 1;
	}

	out = fopen(outname, "w");
	if(out == NULL) {
		perror("could not open output file");
		return 1;
	}

	fprintf(out, "static const char %s[] = {\n", argv[1]);

	while((c = fgetc(in)) != EOF) {
		fprintf(out, "%i,", c);
	}
	
	fprintf(out, "0\n};\n");
	fclose(in);
	fclose(out);
	return 0;
}
