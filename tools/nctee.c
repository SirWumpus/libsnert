/*
 * netcat tee
 *
 * Copyright 2014 by Anthony Howe.  All rights reserved.
 *
 * usage: nctee [-a][-i secs] file
 *
 * 	Interleave netcat client input with the returned server output.
 *
 * Examples:
 *
 * 1. Feed input line by line to netcat, waiting for server replies between each line.
 *
 *	$ printf "HELP\nEHLO mx.example.com\nQUIT\n" | nctee save | nc localhost 25 >>save
 *
 * 2.  Use backquote to specify a block of lines to feed as one unit to netcat:
 *
 *	$ printf 'HELP\n`EHLO mx.example.com\nNOOP\nRSET`\nQUIT\n' | nctee save | nc localhost 25 >>save
 *
 */

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/stat.h>

#define BLOCK_START	'`'
#define BLOCK_END	'`'
#define POLL_INTERVAL	2

static int
follow_stream(FILE *fp, int poll_interval)
{
	struct stat sb;
	size_t last_size = 0;

	do {
		clearerr(fp);

		/* Check invalid file handle or truncation of file. */
		if (fstat(fileno(fp), &sb) != 0 || sb.st_size < last_size)
			return -1;

		/* Capture file has remain constant between polls? */
		if (sb.st_size == last_size)
			break;

		last_size = sb.st_size;
		sleep(poll_interval);
	} while (!ferror(fp));

	return 0;
}

int
process(FILE *capture, long interval)
{
	int ch, block, escape;

	block = 0;
	escape = 0;

	if (follow_stream(capture, interval))
		return -1;

	while ((ch = fgetc(stdin)) != EOF) {
		/* Within input block? */
		if (block) {
			/* Escape _this_ character. */
			if (escape)
				escape = 0;

			/* Escape next character. */
			else if (ch == '\\') {
				escape = 1;
				continue;
			}

			/* Close block? */
			else if (ch == BLOCK_END) {
				block = 0;
				continue;
			}
		}

		/* Not an input block. */
		else {
			if (ch == BLOCK_START) {
				block = 1;
				continue;
			}
		}

		(void) fputc(ch, capture);
		(void) fputc(ch, stdout);

		/* On newline, wait for capture file to stop growing. */
		if (!block && ch == '\n') {
			(void) fflush(capture);
			(void) fflush(stdout);
		 	if (follow_stream(capture, interval)) 
				return -1;
		}
	}

	return 0;
}

const char usage[] = 
"usage: nctee [-a][-i secs] file\n"
"\n"
"-a\t\tappend to capture file\n"
"-i secs\t\tpoll interval in seconds; default 2\n"
"file\t\tcapture file for both input and netcat output\n"
"\n"
;

int
main(int argc, char **argv)
{
	FILE *capture;
	long interval;
	int ch, append;

	append = 0;
	interval = POLL_INTERVAL;

	while ((ch = getopt(argc, argv, "ai:")) != -1) {
		switch (ch) {
		case 'a':
			append = 1;
			break;
		case 'i':
			interval = strtol(optarg, NULL, 10);
			break;
		default:
			optind = argc;
		}
	}
	if (argc <= optind || interval < 0) 
		errx(64, usage);

	if ((capture = fopen(argv[optind], "ab")) == NULL) 
		err(EXIT_FAILURE, "%s", argv[optind]);
	if (!append) 
		(void) ftruncate(fileno(capture), 0);
	if (process(capture, interval))
		err(EXIT_FAILURE, "%s", argv[optind]);
	(void) fclose(capture);

	return EXIT_SUCCESS;
}

