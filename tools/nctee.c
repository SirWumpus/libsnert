/*
 * netcat tee
 *
 * Copyright 2014 by Anthony Howe.  All rights reserved.
 *
 * Interleave netcat client input with the returned server output.
 * Feed input line by line to netcat, waiting for server replies
 * between each line.
 *
 * Example usage:
 *
 * printf "HELP\nEHLO mx.example.com\nQUIT\n" | nctee -c save | nc localhost 25 >>save
 */

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/stat.h>

#define POLL_PERIOD	2

static int map_lf_crlf;
static int poll_delay = POLL_PERIOD;
static int poll_period = POLL_PERIOD;

static int
follow_stream(FILE *fp)
{
	struct stat sb;
	off_t last_size = 0;

	/* Pause _before_ fstat(); allows protocols with
	 * connection greetings to be caught before the
	 * first input line and slow replies.
	 */
	(void) sleep(poll_delay);

	while (!ferror(fp)) {
		clearerr(fp);

		/* Check invalid file handle or truncation of file. */
		if (fstat(fileno(fp), &sb) != 0 || sb.st_size < last_size)
			return -1;

		/* Capture file remains constant between polls? */
		if (sb.st_size == last_size)
			break;

		(void) sleep(poll_period);
		last_size = sb.st_size;
	}

	return 0;
}

int
process(FILE *capture)
{
	int ch, pch;

	/* Wait on output from protocols that send greeting line. */
	if (follow_stream(capture))
		return -1;

	for (pch = EOF; (ch = fgetc(stdin)) != EOF; pch = ch) {
		if (ch == '\n' && pch != '\r' && map_lf_crlf) {
			(void) fputc('\r', capture);
			(void) fputc('\r', stdout);
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
"usage: nctee [-ac][-d sec][-p sec] file\n"
"\n"
"-a\t\tappend to capture file\n"
"-c\t\tmap bare LF to CRLF\n"
"-d sec\t\tinitial capture delay in seconds; default 2\n"
"-p sec\t\tcapture poll period in seconds; default 2\n"
"file\t\tcapture file for both input and netcat output\n"
"\n"
"eg. printf \"HELP\\nNOOP\\nQUIT\\n\" | nctee -c save | nc localhost 25 >>save\n"
;

int
main(int argc, char **argv)
{
	FILE *capture;
	int ch, append;

	append = 0;

	while ((ch = getopt(argc, argv, "acd:p:")) != -1) {
		switch (ch) {
		case 'a':
			append = 1;
			break;
		case 'c':
			map_lf_crlf = 1;
			break;
		case 'd':
			poll_delay = (int) strtol(optarg, NULL, 10);
			break;
		case 'p':
			poll_period = (int) strtol(optarg, NULL, 10);
			break;
		default:
			optind = argc;
		}
	}
	if (argc <= optind || poll_delay < 0 || poll_period < 0)
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

