/*
 * show.c
 *
 * Copyright 2000, 2004 by Anthony Howe.  All rights reserved.
 *
 * usage: show lines files...
 */

#include <com/snert/lib/version.h>

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <com/snert/lib/io/posix.h>
#include <com/snert/lib/sys/Time.h>
#include <com/snert/lib/util/getopt.h>

char buf[BUFSIZ];

char usage[] =
"usage: show [-bfu][-n lines][-p string] files...\n"
"\n"
"-b\t\tbeep when pattern matches\n"
"-f\t\tcontinue to output data as the file grows\n"
"-n lines\tdisplay the top N or bottom -N lines; 0 for all\n"
"-p string\thighlight string in the output\n"
"-u\t\tunbuffered output\n"
"files ...\tlist of files to show\n"
"\n"
"show/1.4 Copyright 2000, 2004 by Anthony Howe. All rights reserved.\n"
;

int beep;
const char asciiBeep[] = "\007";
const char ansiNormal[] = "\033[0m";
const char ansiReverse[] = "\033[5;7m";

int
output(const char *buf, long length, const char *pattern, long *resume)
{
	long i, plen;
	const char *stop;

	if (pattern == (const char *) 0)
#ifdef UNBUFFERED
		return write(1, buf, length);
#else
		return fwrite(buf, 1, length, stdout);
#endif
	plen = strlen(pattern);
	stop = buf + length;

	if (0 < *resume) {
		for (i = 0; buf + i < stop; i++) {
			if (buf[i] != pattern[*resume + i])
				break;
		}

		if (pattern[*resume + i] == '\0') {
#ifdef UNBUFFERED
			write(1, ansiReverse, sizeof (ansiReverse) - 1);
			write(1, pattern, plen);
			write(1, ansiNormal, sizeof (ansiNormal) - 1);
#else
			fwrite(ansiReverse, 1, sizeof (ansiReverse) - 1, stdout);
			fwrite(pattern, 1, plen, stdout);
			fwrite(ansiNormal, 1, sizeof (ansiNormal) - 1, stdout);
#endif
			buf += i;
		} else if (stop <= buf + i) {
			*resume += i;
			return length - i;
		} else {
#ifdef UNBUFFERED
			write(1, pattern, *resume + i);
#else
			fwrite(pattern, 1, *resume + i, stdout);
#endif

			buf += i;
		}

		*resume = 0;
	}

	while (buf < stop) {
		for (i = 0; buf + i < stop; i++) {
			if (buf[i] == pattern[0])
				break;
		}

#ifdef UNBUFFERED
		(void) write(1, buf, i);
#else
		(void) fwrite(buf, 1, i, stdout);
#endif
		buf += i;

		for (i = 0; buf + i < stop && pattern[i] != '\0'; i++) {
			if (buf[i] != pattern[i])
				break;
		}

		if (pattern[i] == '\0') {
#ifdef UNBUFFERED
			write(1, ansiReverse, sizeof (ansiReverse) - 1);
			write(1, pattern, plen);
			write(1, ansiNormal, sizeof (ansiNormal) - 1);
#else
			if (beep)
				fwrite(asciiBeep, 1, sizeof (asciiBeep) - 1, stdout);
			fwrite(ansiReverse, 1, sizeof (ansiReverse) - 1, stdout);
			fwrite(pattern, 1, plen, stdout);
			fwrite(ansiNormal, 1, sizeof (ansiNormal) - 1, stdout);
#endif
			buf += i;
		} else if (stop <= buf + i) {
			*resume += i;
			return length - i;
		} else {
#ifdef UNBUFFERED
			write(1, buf, i);
#else
			fwrite(buf, 1, i, stdout);
#endif
			buf += i;
		}
	}

	return length;
}

int
main(int argc, char **argv)
{
#ifdef UNBUFFERED
	int fd;
#else
	FILE *fp;
#endif
	size_t n;
	off_t offset;
	struct stat finfo;
	char *b, *eb, *pattern;
	int ch, follow, unbuffered;
	long lines, count, patternOffset;

	follow = 0;
	lines = -10;
	unbuffered = 0;
	pattern = NULL;
	patternOffset = 0;

	while ((ch = getopt(argc, argv, "bfn:p:u")) != -1) {
		switch (ch) {
		case 'b':
			beep = 1;
			break;
		case 'f':
			follow = 1;
			break;
		case 'p':
			pattern = optarg;
			break;
		case 'u':
			unbuffered = 1;
			break;
		case 'n':
			lines = strtol(optarg, NULL, 10);
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

	for ( ; optind < argc; optind++) {
		if (stat(argv[optind], &finfo) < 0)
			return 1;

#ifdef UNBUFFERED
		if ((fd = open(argv[optind], O_RDONLY)) < 0)
			return 1;
#else
		if ((fp = fopen(argv[optind], "r")) == NULL)
			return 1;

		if (unbuffered)
			setvbuf(fp, NULL, _IONBF, 0);
#endif

		if (lines == 0) {
#ifdef UNBUFFERED
			while (0 < (n = read(fd, buf, sizeof (buf)))) {
#else
			while (0 < (n = fread(buf, 1, sizeof (buf), fp))) {
#endif
				(void) output(buf, n, pattern, &patternOffset);
			}
		} else {
			if (lines < 0) {
				count = lines = -lines;

				/* Start with the odd buffer length from the end of file. */
				offset = finfo.st_size % sizeof (buf);

#ifdef UNBUFFERED
				if ((offset = lseek(fd, -offset, SEEK_END)) == (off_t) -1)
					return 1;
#else
				if (fseek(fp, -offset, SEEK_END) == -1)
					return 1;

				offset = ftell(fp);
#endif

				for (;;) {
					/* Fill the buffer. */
#ifdef UNBUFFERED
					if ((n = read(fd, buf, sizeof (buf))) <= 0)
#else
					if ((n = fread(buf, 1, sizeof (buf), fp)) == 0)
#endif
						break;

					/* Count backwards newlines (fine for Unix, PC). */
					for (eb = buf+n; buf < eb; ) {
						if (*--eb == '\n' && --count < 0) {
							offset += (eb+1-buf);
#ifdef UNBUFFERED
							(void) lseek(fd, offset, SEEK_SET);
#else
							(void) fseek(fp, offset, SEEK_SET);
#endif
							break;
						}
					}

					if (count <= 0)
						break;

					/* Move backwards by BUFSIZ byte units until we
					 * seek beyond the start of the file.
					 */
#ifdef UNBUFFERED
					if ((offset = lseek(fd, offset-sizeof (buf), SEEK_SET)) == (off_t) -1)
						break;
#else
					if (fseek(fp, offset-sizeof (buf), SEEK_SET) == -1)
						break;

					offset = ftell(fp);
#endif
				}
			}

			while (0 < lines) {
#ifdef UNBUFFERED
				if ((n = read(fd, buf, sizeof (buf))) < 0)
#else
				if ((n = fread(buf, 1, sizeof (buf), fp)) == 0)
#endif
					break;

				/* Count newlines (fine for Unix, PC). */
				for (b = buf, eb = buf+n; b < eb; )
					if (*b++ == '\n' && --lines <= 0)
						break;

				(void) output(buf, b - buf, pattern, &patternOffset);
			}

#ifndef UNBUFFERED
			fflush(stdout);
#endif
		}

#ifdef UNBUFFERED
		while (follow && 0 <= (n = read(fd, buf, sizeof (buf)))) {
#else
		while (follow) {
			clearerr(fp);
			if ((n = fread(buf, 1, sizeof (buf), fp)) == 0) {
				/* For zero length reads check for an error.
				 * if no error pause for a bit, otherwise
				 * we'll eat CPU cycles.
				 */
				follow = !ferror(fp);
				sleep(1);
				continue;
			}
#endif
			(void) output(buf, n, pattern, &patternOffset);
#ifndef UNBUFFERED
			fflush(stdout);
#endif
		}

#ifdef UNBUFFERED
		(void) close(fd);
#else
		(void) fclose(fp);
#endif
	}

	return 0;
}
