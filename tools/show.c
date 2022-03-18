/*
 * show.c
 *
 * Copyright 2000, 2022 by Anthony Howe.  All rights reserved.
 *
 * usage: show [-bfu][-n lines][-p string] file ...
 */

#ifndef DEFAULT_LINES
#define DEFAULT_LINES		(-10)
#endif

#ifndef POLL_INTERVAL
#define POLL_INTERVAL		2
#endif

#ifndef BUFFER_SIZE
#define BUFFER_SIZE		(32*1024)
#endif

#include <com/snert/lib/version.h>

#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>


#include <com/snert/lib/io/posix.h>
#include <com/snert/lib/sys/Time.h>
#include <com/snert/lib/sys/sysexits.h>
#include <com/snert/lib/util/getopt.h>

static char buffer[BUFFER_SIZE];

static char usage[] =
"usage: show [-bfu][-n lines][-p string] file ...\n"
"\n"
"-b\t\tbeep when pattern matches\n"
"-f\t\tcontinue to output data as a file grows\n"
"-n lines\tdisplay the top N or bottom -N lines; 0 for all\n"
"-p string\thighlight string in the output\n"
"-u\t\tunbuffered output\n"
"files ...\tlist of files to show\n"
"\n"
"show Copyright 2000, 2022 by Anthony Howe. All rights reserved.\n"
;

static int beep;
static int follow_flag;
static int reverse_flag;
static long nlines = DEFAULT_LINES;
static char *pattern;

static const char asciiBeep[] = "\007";
static const char ansiNormal[] = "\033[0m";
static const char ansiReverse[] = "\033[5;7m";

static size_t
output(const char *buf, size_t length, const char *pattern, long *resume)
{
	long i, plen;
	const char *stop;

	if (pattern == NULL) {
		return fwrite(buf, 1, length, stdout);
	}

	plen = strlen(pattern);
	stop = buf + length;

	if (0 < *resume) {
		for (i = 0; buf + i < stop; i++) {
			if (buf[i] != pattern[*resume + i]) {
				break;
			}
		}

		if (pattern[*resume + i] == '\0') {
			(void) fwrite(ansiReverse, 1, sizeof (ansiReverse) - 1, stdout);
			(void) fwrite(pattern, 1, plen, stdout);
			(void) fwrite(ansiNormal, 1, sizeof (ansiNormal) - 1, stdout);

			buf += i;
		} else if (stop <= buf + i) {
			*resume += i;
			return length - i;
		} else {
			(void) fwrite(pattern, 1, *resume + i, stdout);
			buf += i;
		}

		*resume = 0;
	}

	while (buf < stop) {
		for (i = 0; buf + i < stop; i++) {
			if (buf[i] == pattern[0]) {
				break;
			}
		}

		(void) fwrite(buf, 1, i, stdout);
		buf += i;

		for (i = 0; buf + i < stop && pattern[i] != '\0'; i++) {
			if (buf[i] != pattern[i]) {
				break;
			}
		}

		if (pattern[i] == '\0') {
			if (beep) {
				(void) fwrite(asciiBeep, 1, sizeof (asciiBeep) - 1, stdout);
			}
			(void) fwrite(ansiReverse, 1, sizeof (ansiReverse) - 1, stdout);
			(void) fwrite(pattern, 1, plen, stdout);
			(void) fwrite(ansiNormal, 1, sizeof (ansiNormal) - 1, stdout);
			buf += i;
		} else if (stop <= buf + i) {
			*resume += i;
			return length - i;
		} else {
			fwrite(buf, 1, i, stdout);
			buf += i;
		}
	}

	return length;
}

static int
follow_stream(FILE *fp)
{
	size_t n;
	struct stat sb;
	ino_t last_ino = 0;
	dev_t last_dev = 0;
	dev_t last_rdev = 0;
	size_t last_size = 0;
	long pattern_offset = 0;

	if (!follow_flag) {
		return 0;
	}

	/* Initialise our current position and file "instance" variables. */
	if (fstat(fileno(fp), &sb) == 0) {
		last_ino = sb.st_ino;
		last_dev = sb.st_dev;
		last_rdev = sb.st_rdev;
		last_size = sb.st_size;

		/* The -f option is ignored if the standard input
		 * is a pipe, but not if it is a FIFO.
		 */
		follow_flag = fileno(fp) != STDIN_FILENO || S_ISFIFO(sb.st_mode);
	}

	do {
		/* Wait for new data to accumulate. */
		(void) sleep(POLL_INTERVAL);

		while (0 < (n = fread(buffer, 1, sizeof (buffer), fp))) {
			(void) output(buffer, n, pattern, &pattern_offset);

			/* Check for truncation or new instance of file. */
			if (fstat(fileno(fp), &sb) != 0
			|| sb.st_size < last_size
			|| sb.st_ino != last_ino
			|| sb.st_dev != last_dev
			|| sb.st_rdev != last_rdev) {
				return -1;
			}

			last_ino = sb.st_ino;
			last_dev = sb.st_dev;
			last_rdev = sb.st_dev;
			last_size = sb.st_size;
		}
	} while (!ferror(fp));

	(void) fflush(stdout);

	return 0;
}

static int
seek_last_n_lines(FILE *fp, size_t lines)
{
	char *eb;
	long offset;
	size_t n, count;
	struct stat finfo;

	if (fstat(fileno(fp), &finfo) != 0) {
		return -1;
	}

	/* Start with the odd buffer length from the end of file. */
	offset = finfo.st_size - finfo.st_size % sizeof (buffer);

	for (count = 0; ; ) {
		/* Seeking on a pipe will fail with errno ESPIPE. */
		if (fseek(fp, offset, SEEK_SET) == -1) {
			break;
		}

		/* Fill the buffer. */
		if ((n = fread(buffer, 1, sizeof (buffer), fp)) <= 0) {
			break;
		}

		/* Count backwards N newlines. */
		for (eb = buffer + n; buffer < eb; ) {
			if (*--eb == '\n' && lines <= count++) {
				offset += (eb+1-buffer);
				break;
			}
		}

		if (offset <= 0 || lines <= count) {
			break;
		}

		/* Move backwards by buffer size units until we reach
		 * the start of the file.
		 */
		offset -= sizeof (buffer);
	}

	/* Set to start of file or start of last N lines. */
	(void) fseek(fp, offset, SEEK_SET);

	return 0;
}

static FILE *
show_n_lines(const char *file, long lines)
{
	FILE *fp;
	size_t n;
	char *b, *eb;
	long pattern_offset = 0;

	if (file[0] == '-' && file[1] == '\0') {
		fp = stdin;
	} else if ((fp = fopen(file, "rb")) == NULL) {
		return NULL;
	}

	if (lines < 0) {
		lines = -lines;
		if (seek_last_n_lines(fp, lines)) {
			(void) fclose(fp);
			return NULL;
		}
	}

	while (0 < lines) {
		if ((n = fread(buffer, 1, sizeof (buffer), fp)) == 0) {
			break;
		}

		/* Count newlines in the buffer. */
		for (b = buffer, eb = buffer+n; b < eb; ) {
			if (*b++ == '\n' && --lines <= 0) {
				break;
			}
		}

		(void) output(buffer, b - buffer, pattern, &pattern_offset);
	}

	(void) fflush(stdout);

	return fp;
}

static void
show_file(const char *file)
{
	FILE *fp;

	/* Show first N or last N lines of a file. */
	fp = show_n_lines(file, nlines);

	/* Follow the file as it grows, like a log file. */
	while (fp != NULL && follow_stream(fp)) {
		/* The file has been truncated or replaced. */
		fp = freopen(file, "rb", fp);
		if (seek_last_n_lines(fp, 1)) {
			break;
		}
	}

	if (fp != NULL) {
		(void) fclose(fp);
	} else {
		(void) fprintf(stderr, "%s: %s (%d)\n", file, strerror(errno), errno);
	}
}

int
main(int argc, char **argv)
{
	int ch;

	while ((ch = getopt(argc, argv, "bfn:p:ru")) != -1) {
		switch (ch) {
		case 'b':
			beep = 1;
			break;
		case 'f':
			follow_flag = 1;
			break;
		case 'p':
			pattern = optarg;
			break;
		case 'u':
			(void) setvbuf(stdout, NULL, _IONBF, 0);
			break;
		case 'n':
			nlines = strtol(optarg, NULL, 10);
			break;
		case 'r':
			reverse_flag = 1;
			break;
		default:
			optind = argc+1;
			break;
		}
	}

	if (argc < optind) {
		fprintf(stderr, usage);
		return EX_USAGE;
	}

	/* Can follow only a single file argument. */
	if (optind + 1 != argc) {
		follow_flag = 0;
	}

	if (argc == optind) {
		show_file("-");
	} else {
		for ( ; optind < argc; optind++) {
			show_file(argv[optind]);
		}
	}

	return EXIT_SUCCESS;
}
