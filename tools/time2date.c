/*
 * time2date.c
 *
 * Copyright 2004 by Anthony Howe.  All rights reserved.
 *
 * To build:
 *
 *	gcc -o time2date time2date.c
 *
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>

#include <sys/types.h>
#include <time.h>

char usage[] =
"usage: time2date seconds ...\n"
"\n"
"seconds\t\tthe number of seconds from the system epoch\n"
"\n"
"time2date/1.0 Copyright 2004 by Anthony Howe.  All rights reserved.\n"
;

int
main(int argc, char **argv)
{
	int argi;
	char *stop;
	time_t seconds;

	if (argc < 2) {
		fprintf(stderr, usage);
		return EX_USAGE;
	}

	for (argi = 1; argi < argc; argi++) {
		seconds = (time_t) strtol(argv[argi], NULL, 10);
		printf(ctime(&seconds));
	}

	return 0;
}


