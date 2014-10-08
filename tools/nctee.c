/*
 * netcat tee
 *
 * Copyright 2014 by Anthony Howe.  All rights reserved.
 *
 * usage: nctee [-a][-e delim][-i secs] file
 *
 * 	Interleave netcat client input with the returned server output.
 *
 * Examples:
 *
 * 1. Feed input line by line to netcat, waiting for server replies between each line.
 *
 *	$ printf "HELP\nEHLO mx.example.com\nQUIT\n" | nctee save | nc localhost 25 >>save
 *
 * 2. Use baskslash newline to continue the input unit:
 *
 *	$ printf 'HELP\nEHLO mx.example.com\\\nNOOP\\\nRSET\nQUIT\n' | nctee save | nc localhost 25 >>save
 *
 */

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/stat.h>

#define POLL_INTERVAL	2

static int escape_delim = '\\';
static int poll_interval = POLL_INTERVAL;

static int
follow_stream(FILE *fp)
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
process(FILE *capture)
{
	int ch, nch, escape;

	if (follow_stream(capture))
		return -1;

	for (escape = 0; (ch = fgetc(stdin)) != EOF; ) {
		if (escape) {
			(void) fputc(ch, capture);
			(void) fputc(ch, stdout);
			escape = 0;
			continue;
		}

		if (ch == escape_delim) {
			nch = fgetc(stdin);
			(void) ungetc(nch, stdin);

			/* Escaped newline does not end input unit. */
			if (nch == '\n') {
				escape = 1;
				/* Discard the escape character. */
				continue;
			}

			/* Escaped escape character? */
			if (nch == escape_delim) {
				escape = 1;
				/* Escape character passed through. */
			}
		}

		(void) fputc(ch, capture);
		(void) fputc(ch, stdout);

		/* On newline, wait for capture file to stop growing. */
		if (ch == '\n') {
			(void) fflush(capture);
			(void) fflush(stdout);
		 	if (follow_stream(capture))
				return -1;
		}
	}

	return 0;
}

const char usage[] =
"usage: nctee [-a][-e delim][-i sec] file\n"
"\n"
"-a\t\tappend to capture file\n"
"-e delim\tescape character; default '\\'\n"
"-i sec\t\tpoll interval in seconds; default 2\n"
"file\t\tcapture file for both input and netcat output\n"
;

int
main(int argc, char **argv)
{
	FILE *capture;
	int ch, append;

	append = 0;

	while ((ch = getopt(argc, argv, "ae:i:")) != -1) {
		switch (ch) {
		case 'a':
			append = 1;
			break;
		case 'e':
			escape_delim = *optarg;
			break;
		case 'i':
			poll_interval = (int) strtol(optarg, NULL, 10);
			break;
		default:
			optind = argc;
		}
	}
	if (argc <= optind || poll_interval < 0)
		errx(64, usage);

	if ((capture = fopen(argv[optind], "ab")) == NULL)
		err(EXIT_FAILURE, "%s", argv[optind]);
	if (!append)
		(void) ftruncate(fileno(capture), 0);
	if (process(capture))
		err(EXIT_FAILURE, "%s", argv[optind]);
	(void) fclose(capture);

	return EXIT_SUCCESS;
}

