/*
 * rawrm.c
 *
 * Copyright 2004 by Anthony Howe.  All rights reserved.
 *
 * usage: rawrm list-file
 */

#include <stdio.h>
#include <stdlib.h>

static char line[BUFSIZ];

long
TextInputLine(FILE *fp, char *line, long size)
{
	long i;

	for (i = 0, --size; i < size; ++i) {
		line[i] = (char) fgetc(fp);

		if (feof(fp) || ferror(fp))
			return -1;

		if (line[i] == '\n') {
			line[i] = '\0';
			if (0 < i && line[i-1] == '\r')
				line[--i] = '\0';
			break;
		}
	}

	line[i] = '\0';

	return i;
}

int
main(int argc, char **argv)
{
	long length;
	
	if (argc != 1) {
		fprintf(stderr, "usage: rawrm <file-list\n");
		return 2;
	}

	while (0 <= (length = TextInputLine(stdin, line, sizeof (line)))) {
		if (length != 0 && unlink(line))
			fprintf(stdout, "%s not removed\n", line);
	}	
	
	return 0;
}
