/*
 * TextCat.c
 *
 * Copyright 2006 by Anthony Howe. All rights reserved.
 */

#include <com/snert/lib/version.h>
#include <com/snert/lib/util/Text.h>

/***********************************************************************
 ***
 ***********************************************************************/

#ifdef TEST
# undef HAVE_STRLCAT
# undef TextCat
#endif

#ifndef HAVE_STRLCAT
/**
 * <p>
 * Append the source string to the end of the target string, without
 * exceeding the tsize, and always null terminating the result.
 * </p>
 *
 * @param t
 *	A pointer to a C string target.
 *
 * @param tsize
 *	The physical length of target string buffer.
 *
 * @param s
 *	A pointer to the source string buffer, which might not be null
 *	terminated.
 *
 * @return
 *	The length of the target string, excluding the terminating null.
 *	The string will have been truncated if the length returned
 *	equals or is greater than tsize.
 *
 * @see
 *	snprintf(), strncat(), strlcat()
 */
size_t
TextCat(char *t, size_t tsize, char *s)
{
	char *stop;
	size_t tlength;

	stop = t + tsize;
	tlength = strlen(t);

	for (t += tlength; t < stop; t++) {
		if ((*t = *s++) == '\0')
			break;
	}

	/* Truncate the target string so as to assert that it is always
	 * null terminated. This means nothing bad will happen if the
	 * caller does not test for truncation and proceeds to use the
	 * string in further C string operations. This differs from
	 * strlcat()'s behaviour.
	 */
	if (0 < tsize && stop <= t)
		t[-1] = '\0';

	return tsize - (stop - t);
}
#endif

#ifdef TEST
#include <stdio.h>

typedef struct {
	char target[10];
	char *append;
	char *expect;
	size_t size;
	size_t length;
} Test;

Test table[] = {
	{ "", 		"", 		"",		5,	0 },
	{ "1", 		"", 		"1", 		5,	1 },
	{ "", 		"a", 		"a", 		5,	1 },
	{ "2", 		"a", 		"2a", 		5,	2 },
	{ "3", 		"ab", 		"3ab", 		5,	3 },
	{ "45",		"ab", 		"45ab",		5,	4 },
	{ "56",		"abc",		"56ab",		5,	5 },
	{ "678",	"abcd",		"678a",		5,	5 },
	{ "78901234",	"abcde",	"7890123",	5,	8 },
	{ NULL }
};

void
test(Test *x)
{
	size_t length = TextCat(x->target, x->size, x->append);
	int truncated = x->size <= length;
	int match = strncmp(x->target, x->expect, sizeof (x->target)) == 0;

	printf("match=%d truncated=%d length=%lu n=%lu t=\"%s\" s=\"%s\"\n", match, truncated, length, x->size, x->target, x->append);
}

int
main(int argc, char **argv)
{
	Test *x;

	for (x = table; x->append != NULL; x++)
		test(x);

	return 0;
}
#endif
