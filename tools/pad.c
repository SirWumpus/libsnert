/*
 * spad.c
 *
 * Copyright 2012 by Anthony Howe.  All rights reserved.
 */
 
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>

char usage[] = 
"usage: pad [-l length][-s countC][-p countC][-w width] <input\n"
"\n"
"-l length\tmaximum byte length per line; default very big\n"
"-p countC\tprefix each line with count C characters\n"
"-s countC\tsuffix each line with count C characters\n"
"-w width\tmaximum column width per line; default very big\n"
"\n"
;

void
countc(const char *arg, long *count, int *pad)
{
	int ch;
	unsigned char *stop;
	
	*count = strtol(arg, (char **)&stop, 10);
	ch = stop[0];

	if (ch == '\\') {
		switch (stop[1]) {
		case '\0': break;
		case 'a': ch = '\a'; break;
		case 'f': ch = '\f'; break;
		case 'n': ch = '\n'; break;
		case 'r': ch = '\r'; break;
		case 's': ch = ' ' ; break;
		case 't': ch = '\t'; break;
		case 'v': ch = '\v'; break;
		default: ch = stop[1];
		}
	}

	if (*count < 0 || ch == '\0')
		*count = 0;
	*pad = ch;
}

void
pad(FILE *out, int ch, long count, long *column, long *offset, long width, long length)
{
	long n, col, off;

	col = *column;
	off = *offset;

	for (n = count; 0 < n && off < length && col < width; n--, off++) {
		col += (ch == '\t') ? 8 - col % 8 : isprint(ch) ? 1 : 0;
		(void) fputc(ch, out);
	}

	*offset = off;
	*column = col;
}

int
main(int argc, char **argv)
{
	int ppad, spad, ch, cr;
	long pcount, scount, length, width, off, col;

	pcount = scount = 0;
	width = ((unsigned long) ~0) >> 1;
	length = ((unsigned long) ~0) >> 1;

	while ((ch = getopt(argc, argv, "p:s:l:w:")) != -1) {
		switch (ch) {
		case 'p':
			countc(optarg, &pcount, &ppad);
			break;
		case 's':
			countc(optarg, &scount, &spad);
			break;
		case 'l':
			length = strtol(optarg, NULL, 10);
			break;
		case 'w':
			width = strtol(optarg, NULL, 10);
			break;
		default:
			fputs(usage, stderr);
			return 1;
		}
	}

	cr = col = off = 0;
	for ( ; (ch = fgetc(stdin)) != EOF; ) {
		if (off <= 0)
			pad(stdout, ppad, pcount, &col, &off, width, length);

		/* Found newline? */
		if (ch == '\n') {
			pad(stdout, spad, scount, &col, &off, width, length - cr - 1);

			/* Was LF preceeded by CR? */
			if (cr) 
				fputc('\r', stdout);
			fputc('\n', stdout);

			/* Start of next line. */
			cr = col = off = 0;
		} else if (ch == '\r') {
			/* Look at next character. */
			ch = fgetc(stdin);
			ungetc(ch, stdin);

			/* CRLF found? */
			if (ch == '\n') {
				cr++;
				continue;
			}

			/* Bare CR in line. */
			fputc('\r', stdout);
			col = 0;
			off++;
		} else {
			/* Advance column by tab width, printable character, or invisible control. */
			col += (ch == '\t') ? 8 - col % 8 : isprint(ch) ? 1 : 0;
			fputc(ch, stdout);
			off++;
		}
	}

	return 0;
}
