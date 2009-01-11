/*
 * echo.c
 *
 * Copyright 1991, 2003 by Anthony Howe.  All rights reserved.
 */

#include <stdio.h>

/*
 * Print the array of multibyte strings to standard output,
 * delimited by <spaces> and terminated by a <newline>.
 */
int
main(argc, argv)
int argc;
char **argv;
{
	while (1 < argc) {
		(void) fputs(*++argv, stdout);

		if (1 < --argc)
			(void) fputc(' ', stdout);
	}

	(void) fputc('\n', stdout);

	return 0;
}
