/*
 * range.c
 *
 * Display a list of numbers.
 *
 * Copyright 1994, 2003 by Anthony Howe.  All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char usage[] =
"\033[1musage: range low high\033[0m\n"
"\n"
"Display a list of numbers, 1 per line, from low to high inclusive.\n"
"\n"
"\033[1mrange/1.0 Copyright 2004 by Anthony Howe. All rights reserved.\033[0m\n"
;

int
main(int argc, char **argv)
{
	long a, b;

	if (argc != 3) {
		fprintf(stderr, usage);
		return 2;
	}

	a = strtol(argv[1], NULL, 10);
	b = strtol(argv[2], NULL, 10);

	if (b < a) {
		fprintf(stderr, usage);
		return 2;
	}

	for ( ; a <= b; a++)
		printf("%ld\n", a);

	return 0;
}
