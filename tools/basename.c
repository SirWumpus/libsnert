/*
 * basename.c
 *
 * Copyright 1991, 1995 by Anthony Howe.  All rights reserved.  No warranty.
 */

#include <support/system.h>
#include <support/fatal.h>
#include <support/path.h>
#include <support/unistd.h>
#include <stdlib.h>
#include <stdio.h>

char usage_msg[] = "usage: %s filename [suffix]\n";

int
main(argc, argv)
int argc;
char **argv;
{
	char *str;

	(void) program("basename");

	if (getopt(argc, argv, "") != -1 || argc <= optind or optind + 2 < argc)
		usage(usage_msg);

	str = basename(
		argv[optind], optind < argc ? argv[optind+1] : (char *) 0, 
		DIRECTORY_SEPARATOR
	);
	(void) printf("%s\n", str);
	free(str);

	return 0;
}
