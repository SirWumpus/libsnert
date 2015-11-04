/*
 * search.c
 *
 * Copyright 2015 by Anthony Howe. All rights reserved.
 */
 
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define min(a, b)	(a < b ? a : b)
#define max(a, b)	(a < b ? b : a)

#define INFO(...)       { if (0 < debug) { warnx(__VA_ARGS__); } }
#define DEBUG(...)      { if (1 < debug) { warnx(__VA_ARGS__); } }
#define DUMP(...)       { if (2 < debug) { warnx(__VA_ARGS__); } }

static int debug;

typedef struct {
	const unsigned char *pattern;
	size_t length;
	size_t (*delta)[256];
	unsigned max_err;
} Pattern;	

/*
 * Boyer-Moore-Horspool search algorithm.
 *
 * https://en.wikipedia.org/wiki/Boyer%E2%80%93Moore%E2%80%93Horspool_algorithm
 * http://www-igm.univ-mlv.fr/~lecroq/string/node18.html#SECTION00180
 * http://alg.csie.ncnu.edu.tw/course/StringMatching/Horspool.ppt
 */
int
horspool_init(Pattern *pp, const unsigned char *pattern, unsigned max_err)
{
	long i, k, m;
	
	pp->max_err = max_err;
	pp->pattern = pattern;
	pp->length = strlen((char *)pattern);
	m = pp->length - 1;		

	if (pp->length < max_err) {
		errno = EINVAL;
		return -1;
	}

	if ((pp->delta = malloc((max_err+1) * sizeof (*pp->delta))) == NULL) {
		return -1;
	}

	for (k = 0; k <= max_err; k++) {
		for (i = 0; i < sizeof (*pp->delta); i++)
			pp->delta[k][i] = pp->length;
		pp->delta[k][pattern[m - k]] = pp->length - k;		
		for (i = 0; i < m - k; i++)
			pp->delta[k][pattern[i]] = m - k - i;
	}

	return 0;
}

void
horspool_fini(Pattern *pp)
{
	if (pp != NULL)
		free(pp->delta);
}

long
horspool_search(Pattern *pp, const unsigned char *str, size_t len)
{
	long offset = 0;
	size_t m = pp->length - 1;	
	
	while (offset + pp->length <= len) {
		long i;
		int err = 0;
		size_t delta = pp->length - pp->max_err;
		
		INFO("off=%ld str=\"%s\"", offset, str+offset);		

		for (i = m; 0 <= i && err <= pp->max_err; i--) {			
			INFO(
				"delta=%lu e=%d T='%c' P='%c' m='%c' d=%lu",
				delta, err, str[offset + i], pp->pattern[i], 
				str[offset + m], pp->delta[err][str[offset + m]]
			);
			
			if (str[offset + i] != pp->pattern[i]) {
				delta = min(delta, pp->delta[err][str[offset + m]]);
				delta = min(delta, pp->delta[err][str[offset + m -1]]);
				err++;
			}
		}

		if (err <= pp->max_err) {
			INFO("return offset=%ld", offset);
			return offset;
		}
		offset += delta;
	}

	INFO("return -1 no match");
	return -1;
}

/*
 * Boyer-Moore-Horspool-Sunday search algorithm (quick search variant).
 *
 * https://csclub.uwaterloo.ca/~pbarfuss/p132-sunday.pdf
 * http://www-igm.univ-mlv.fr/~lecroq/string/node19.html#SECTION00190
 * http://alg.csie.ncnu.edu.tw/course/StringMatching/Quick%20Searching.ppt
 */
int
sunday_init(Pattern *pp, const unsigned char *pattern, unsigned max_err)
{
	long i, k;
	
	pp->max_err = max_err;
	pp->pattern = pattern;
	pp->length = strlen((char *)pattern);

	if (pp->length < max_err) {
		errno = EINVAL;
		return -1;
	}

	if ((pp->delta = malloc((max_err+1) * sizeof (*pp->delta))) == NULL) {
		return -1;
	}

#ifdef K_EQ_0
	for (i = 0; i < sizeof (*pp->delta); i++)
		pp->delta[0][i] = pp->length + 1;
	for (i = 0; i < pp->length; i++)
		pp->delta[0][pattern[i]] = pp->length - i;
#else
	for (k = 0; k <= max_err; k++) {
		for (i = 0; i < sizeof (*pp->delta); i++)
			pp->delta[k][i] = pp->length + 1 - k;
		for (i = 0; i < pp->length - k; i++)
			pp->delta[k][pattern[i]] = pp->length - i - k;
	}
#endif		
	return 0;
}

void
sunday_fini(Pattern *pp)
{
	horspool_fini(pp);
}

long
sunday_search(Pattern *pp, const unsigned char *str, size_t len)
{
	long offset = 0;
	
	while (offset + pp->length <= len) {
		INFO("off=%ld str=\"%s\"", offset, str+offset);		
#ifdef K_EQ_0
		if (memcmp(pp->pattern, str + offset, pp->length) == 0) {
			INFO("return offset=%ld", offset);
			return offset;
		}
		offset += pp->delta[0][str[offset + pp->length]];
#else
		long i;
		int err = 0;
		size_t delta = pp->length + 1 - pp->max_err;

		/* Sunday algorithm can scan any order. */
		for (i = 0; i < pp->length && err <= pp->max_err; i++) {			
			INFO(
				"delta=%lu e=%d T='%c' P='%c' m='%c'",
				delta, err, str[offset + i], pp->pattern[i], 
				str[offset + pp->length - err]
			);

			if (str[offset + i] != pp->pattern[i]) {
				delta = min(delta, pp->delta[err][str[offset + pp->length - err]]);
				err++;
			}
		}

		if (err <= pp->max_err) {
			INFO("return offset=%ld", offset);
			return offset;
		}
		offset += delta;
#endif
	}
	
	INFO("return -1 no match");
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
	unsigned max_err;
	long lineno, offset;
	int brackets, ch, rc, argi;
	unsigned char line[LINE_SIZE];
	void (*fn_fini)(Pattern *);
	int (*fn_init)(Pattern *, const unsigned char *, unsigned);
	long (*fn_srch)(Pattern *, const unsigned char *, size_t);
	
	max_err = 0;
	brackets = 0;
	fn_init = sunday_init;
	fn_fini = sunday_fini;
	fn_srch = sunday_search;
	
	while ((ch = getopt(argc, argv, "bhk:v")) != -1) {
		switch (ch) {
		case 'b':
			brackets = 1;
			break;
		case 'h':
			fn_fini = horspool_fini;
			fn_init = horspool_init;
			fn_srch = horspool_search;
			break;
		case 'k':
			max_err = strtoul(optarg, NULL, 10);
			break;
		case 'v':
			debug++;
			break;
		default:
			optind = argc;
		}
	}
	
	if (argc <= optind + 1) {
		(void) fputs("usage: search [-bhv][-k n] string file ...\n", stderr);
		return 2;
	}

	rc = 1;
	fn_init(&pat, (const unsigned char *)argv[optind], max_err);

	for (argi = optind+1; argi < argc; argi++) {
		if ((fp = fopen(argv[argi], "r")) == NULL) {
			(void) fprintf(stderr, "%s: %s (%d)\n", argv[argi], strerror(errno), errno);
			continue;
		}
		
		lineno = 0;
		while (0 < (line_len = inputline(fp, line, sizeof (line)))) {
			lineno++;
			if ((offset = fn_srch(&pat, line, line_len)) != -1) {
				if (optind+2 < argc)
					(void) printf("%s: ", argv[argi]);
				if (brackets) {
					(void) printf(
						"%ld %ld %.*s[%.*s]%s", 
						lineno, offset,
						(int) offset, line,
						(int) pat.length, line + offset,
						line + offset + pat.length
					);
				} else {
					(void) printf("%ld %-2ld %s", lineno, offset, line);					
				}
				rc = 0;
			}
		}
				
		(void) fclose(fp);
	}
	
	fn_fini(&pat);
			
	return rc;
}

#endif
