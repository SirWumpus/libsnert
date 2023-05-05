/*
 * rsleep.c
 *
 * Sleep random intervals between M and N.
 *
 * Copyright 2009 by Anthony Howe.  All rights reserved.
 */

#include <com/snert/lib/version.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#ifdef HAVE_SYS_TIME_H
# include <sys/time.h>
#endif
#include <time.h>

#include <com/snert/lib/util/getopt.h>

#ifdef HAVE_RAND_R
#define RANDOM_NUMBER(max)	((int)((double)(max) * (double) rand_r(&rand_seed) / (RAND_MAX+1.0)))
#else
#define RANDOM_NUMBER(max)	((int)((double)(max) * (double) rand() / (RAND_MAX+1.0)))
#endif

static char usage[] =
"usage: rsleep [-e][-s seed] max\n"
"\n"
"-e\t\texit with a random exit value without sleeping\n"
"-s seed\t\tspecify a random seed\n"
"\n"
"Select a random number between 0 and max - 1 and sleep\n"
"provided -e is not specified.\n"
"\n"
"Copyright 2009 by Anthony Howe. All rights reserved.\n"
;

static int rand_exit;
static unsigned rand_seed;

int
main(int argc, char **argv)
{
	long max;
	int ch, number;

	while ((ch = getopt(argc, argv, "es:")) != -1) {
		switch (ch) {
		case 'e':
			rand_exit = 1;
			break;

		case 's':
			rand_seed = strtol(optarg, NULL, 10);
			break;
		default:
			fprintf(stderr, usage);
			return 2;
		}
	}

	if (argc <= optind) {
		fprintf(stderr, usage);
		return 2;
	}

	max = strtol(argv[optind], NULL, 10);

	if (rand_seed == 0)
		rand_seed = max ^ time(NULL);

#ifndef HAVE_RAND_R
	srand(rand_seed);
#endif
	number = RANDOM_NUMBER(max);
	if (!rand_exit)
		(void) sleep(number);

	return rand_exit ? number : 0;
}
