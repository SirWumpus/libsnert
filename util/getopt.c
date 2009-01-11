/*
 * getopt.c
 *
 * Copyright 1992, 2004 by Anthony Howe.  All rights reserved.
 */

#include <stdio.h>
#include <string.h>

#include <com/snert/lib/util/getopt.h>

char *optarg;
int optind = 1;
int opterr = 1;
int optopt;

int
alt_getopt(int argc, char * const *argv, const char *optstring)
{
	int nextopt;
	register char *ptr;
	static int index = 0;

	optopt = '\0';
	optarg = (char *) 0;

	/* Extension: reset index if optstring is NULL. */
	if (optstring == (char *) 0) {
		/* Reset getopt(). */
		optind = index = 1;
		return -1;
	}

	ptr = argv[optind];
	if (argc <= optind || ptr == (char *) 0)
		return -1;

	if (index <= 1) {
		switch (ptr[0]) {
		case '-':
			switch (ptr[1]) {
			case '-':
				/* "--" ends the argument list. */
				if (ptr[2] == '\0') {
					++optind;
					return -1;
				}
				if (opterr)
					fprintf(stderr, "Long options %s not supported.\n", ptr);
				return '?';
			case '\0':
				/* Extension: a minus (-) in the optstring
				 * permits sole "-" as an option.
				 */
				if (strchr(optstring, '-') != (char *) 0) {
					++optind;
					return '-';
				}
				return -1;
			default:
				/* Start of option string. */
				index = 1;
			}
			break;
		case '+':
			/* Extension: a plus (+) in the optstring
			 * permits "+[string]" as an option.
			 */
			if (strchr(optstring, '+') != (char *) 0) {
				optarg = &argv[optind++][1];
				return '+';
			}
			return -1;
		default:
			return -1;
		}
	}

	optopt = ptr[index++];
	nextopt = ptr[index];
	ptr = strchr(optstring, optopt);

	/* Colon (:) not permitted as an option, because it is a special
	 * token used in optstring, or optopt is not in optstring.
	 */
	if (optopt == ':' || ptr == (char *) 0) {
		if (opterr && optstring[0] != ':') {
			fprintf(stderr, "Unknown option -%c.\n", optopt);
		}

		if (nextopt == '\0') {
			index = 1;
			++optind;
		}

		return '?';
	}

	/* A colon (:) following an option character in optstring
	 * indicates a required argument either immediately following
	 * the option character or as the next argument in the list,
	 * ie. "-f<arg>" or "-f <arg>".
	 *
	 * Extension: a semi-colon (;) following an option character
	 * in optstring indicates an optional argument that immediates
	 * follows the option character, ie. "-f[opt-arg]" or "-f".
	 */
	if (*++ptr == ':' || *ptr == ';') {
		if (nextopt != '\0') {
			/* "-f<arg>" required and optional formats. */
			optarg = &argv[optind++][index];
		} else if (*ptr == ';') {
			/* "-f" optional argument format. */
			++optind;
		} else if (++optind < argc) {
			/* "-f <arg>" required argument format. */
			optarg = argv[optind++];
		} else {
			/* Missing required argument error. */
			if (opterr && optstring[0] != ':') {
				fprintf(stderr, "Option -%c argument missing.\n", optopt);
				return '?';
			}
			return ':';
		}
		index = 1;
	} else if (nextopt == '\0') {
		index = 1;
		++optind;
	}

	return optopt;
}

