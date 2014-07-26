/*
 * TextTransliterate.c
 *
 * Copyright 2005, 2014 by Anthony Howe. All rights reserved.
 */

#include <stdlib.h>
#include <com/snert/lib/version.h>
#include <com/snert/lib/util/Text.h>

/***********************************************************************
 ***
 ***********************************************************************/

size_t
TextTransliterate(char *string, const char *from_set, const char *to_set)
{
	char *s;
	const char *f;
	size_t len, from_set_len, to_set_len;

	if (string == NULL || from_set == NULL)
		return 0;

	len = strlen(string);
	from_set_len = strlen(from_set);
	to_set_len = to_set == NULL ? 0 : strlen(to_set);

	if (from_set_len == 0)
		return 0;

	for (s = string; *s != '\0'; s++) {
		if ((f = strchr(from_set, *s)) != NULL) {
			if (to_set_len == 0) {
				/* Empty to_set, delete character from string. */
				(void) memmove(s, s+1, len-(s-string));
				s--;
			} else if (to_set_len <= f-from_set) {
				/* Short to_set, use last character of to_set. */
				*s = to_set[to_set_len-1];
			} else {
				/* Replace character. */
				*s = to_set[f - from_set];
			}
		}
	}

	return (size_t)(s-string);
}

#ifdef TEST
#include <stdio.h>
#include <com/snert/lib/util/getopt.h>

static const char usage[] =
"usage: translit -T\n"
"       translit string1 [string2]\n"
;

typedef struct {
	const char *from_set;
	const char *to_set;
	const char *source;
	const char *expect;
	size_t expect_length;
} Test;

static Test test_table[] = {
	{ "!@#", "123", "!@#@#!#!@", "123231312", 9 },
	{ "!@#", "_", "1!2@3#", "1_2_3_", 6 },
	{ "#", "", "#1##2###3", "123", 3 },
	{ NULL }
};

static int
test_translit(Test *table)
{
	int rc;
	Test *t;
	char *copy;
	size_t length;

	rc = EXIT_SUCCESS;
	for (t = table; t->expect != NULL; t++) {
		if ((copy = strdup(t->source)) == NULL)
			continue;

		length = TextTransliterate(copy, t->from_set, t->to_set);
		printf(
			"%s f=\"%s\" t=\"%s\" src=[%s] got=[%s] l=%lu\n",
			length == t->expect_length && strcmp(copy, t->expect) == 0
				? "-OK-" : (rc = EXIT_FAILURE, "FAIL"),
			t->from_set, t->to_set,
			t->source, copy, (unsigned long)length
		);
		free(copy);
	}

	return rc;
}

static int
translit(FILE *fp, const char *from_set, const char *to_set)
{
	size_t length;
	ssize_t nbytes;
	char buffer[BUFSIZ];

	while (0 < (nbytes = fread(buffer, sizeof (*buffer), BUFSIZ-1, fp))) {
		buffer[nbytes] = '\0';
		length = TextTransliterate(buffer, from_set, to_set);
		(void) fwrite(buffer, sizeof (*buffer), length, stdout);
	}

	return EXIT_SUCCESS;
}

int
main(int argc, char **argv)
{
	int ch, rc, do_test;

	rc = 0;
	do_test = 0;

	while ((ch = getopt(argc, argv, "T")) != -1) {
		switch (ch) {
		case 'T':
			do_test = 1;
			break;
		default:
			optind = argc;
			break;
		}
	}
	if (argc < optind+2 && !do_test) {
		fprintf(stderr, usage);
		return EXIT_FAILURE;
	}

	if (do_test)
		rc = test_translit(test_table);
	else
		rc = translit(stdin, argv[optind], argv[optind+1]);

	return rc;
}
#endif
