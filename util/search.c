/*
 * search.c
 *
 * Copyright 2015 by Anthony Howe. All rights reserved.
 */

#include <string.h>

typedef struct {
	const unsigned char *pattern;
	size_t length;
	size_t delta[256];
} Pattern;	

/*
 * Boyer-Moore-Horspool search algorithm.
 *
 * https://en.wikipedia.org/wiki/Boyer%E2%80%93Moore%E2%80%93Horspool_algorithm
 */
void
horspool_init(Pattern *pp, const unsigned char *pattern)
{
	int i;
	
	pp->pattern = pattern;
	pp->length = strlen((char *)pattern);

	for (i = 0; i < 256; i++)
		pp->delta[i] = pp->length;
	for (i = 0; i < pp->length - 1; i++)
		pp->delta[pattern[i]] = pp->length - 1 - i;	
}

long
horspool_search(Pattern *pp, const unsigned char *str, size_t len)
{
	long offset = 0;
	
	while (offset + pp->length <= len) {
		size_t i;
		for (i = pp->length - 1; str[offset + i] == pp->pattern[i]; i--) {
			if (i == 0)
				return offset;
		}
		offset += pp->delta[str[offset + pp->length-1]];
	}

	return -1;
}

/*
 * Boyer-Moore-Horspool-Sunday search algorithm (quick search variant).
 *
 * https://csclub.uwaterloo.ca/~pbarfuss/p132-sunday.pdf
 */
void
sunday_init(Pattern *pp, const unsigned char *pattern)
{
	int i;
	
	pp->pattern = pattern;
	pp->length = strlen((char *)pattern);

	for (i = 0; i < 256; i++)
		pp->delta[i] = pp->length + 1;
	for (i = 0; i < pp->length - 1; i++)
		pp->delta[pattern[i]] = pp->length - 1 - i;	
}

long
sunday_search(Pattern *pp, const unsigned char *str, size_t len)
{
	long offset = 0;
	
	while (offset + pp->length <= len) {
		if (memcmp(pp->pattern, str + offset, pp->length) == 0)
			return offset;
		offset += pp->delta[str[offset + pp->length-1]];
	}
	
	return -1;
}

#ifdef TEST

#include <errno.h>
#include <stdio.h>
#include <getopt.h>

#define LINE_SIZE	2048

size_t
inputline(FILE *fp, unsigned char *buf, size_t size)
{
	int ch;
	size_t len;
	
	if (size == 0)
		return 0;
	size--;
	
	for (len = 0; len < size; ) {
		if ((ch = fgetc(fp)) == EOF)
			break;			
		buf[len++] = ch;
		if (ch == '\n')
			break;
	}
	
	buf[len] = '\0';
	
	return len;
}

int
main(int argc, char **argv)
{
	FILE *fp;
	Pattern pat;
	size_t line_len;
	int ch, rc, argi;
	unsigned char line[LINE_SIZE];
	void (*fn_init)(Pattern *, const unsigned char *);
	long (*fn_srch)(Pattern *, const unsigned char *, size_t);
	
	fn_init = sunday_init;
	fn_srch = sunday_search;
	
	while ((ch = getopt(argc, argv, "h")) != -1) {
		switch (ch) {
		case 'h':
			fn_init = horspool_init;
			fn_srch = horspool_search;
			break;
		default:
			optind = argc;
		}
	}
	
	if (argc <= optind + 1) {
		(void) fputs("usage: search [-h] string file ...\n", stderr);
		return 2;
	}

	rc = 1;
	fn_init(&pat, (const unsigned char *)argv[optind]);

	for (argi = optind+1; argi < argc; argi++) {
		if ((fp = fopen(argv[argi], "r")) == NULL) {
			(void) fprintf(stderr, "%s: %s (%d)\n", argv[argi], strerror(errno), errno);
			continue;
		}
		
		while (0 < (line_len = inputline(fp, line, sizeof (line)))) {
			if (fn_srch(&pat, line, line_len) != -1) {
				if (3 < argc)
					(void) printf("%s: %s", argv[argi], line);
				else
					(void) printf("%s", line);					
				rc = 0;
			}
		}
				
		(void) fclose(fp);
	}
			
	return rc;
}

#endif
