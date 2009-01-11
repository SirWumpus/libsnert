/*
 * TextCopy.c
 *
 * Copyright 2001, 2006 by Anthony Howe. All rights reserved.
 */

#include <com/snert/lib/version.h>
#include <com/snert/lib/util/Text.h>

/***********************************************************************
 ***
 ***********************************************************************/

#ifdef TEST
# undef HAVE_STRLCPY
# undef TextCopy
#endif

#ifndef HAVE_STRLCPY
/**
 * <p>
 * Copy up to tsize - 1 octets from the null terminated C source string
 * into the target string buffer, always null terminating the result. If
 * tsize is zero, then nothing is copied and the contents of the target
 * string buffer remain unaltered.
 * </p>
 *
 * @param t
 *	A pointer to the target string buffer.
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
 *	The string will have been truncated if the length returned equals
 *	tsize.
 *
 * @see
 *	memcpy(), snprintf(), strncpy(), strlcpy()
 */
size_t
TextCopy(char *t, size_t tsize, const char *s)
{
	char *stop;

	for (stop = t + tsize; t < stop; t++) {
		if ((*t = *s++) == '\0')
			break;
	}

	/* Truncate the target string so as to assert that it is always
	 * null terminated. This means nothing bad will happen if the
	 * caller does not test for truncation and proceeds to use the
	 * string in further C string operations.
	 */
	if (0 < tsize && stop <= t)
		t[-1] = '\0';

	return tsize - (stop - t);
}
#endif

#ifdef TEST
#include <stdio.h>

void
test(char *t, size_t n, char *s)
{
	size_t length = TextCopy(t, n, s);
	printf("truncated=%d length=%lu n=%lu t=\"%s\"\ts=\"%s\"\n", n <= length, length, n, t, s);
}

int
main(int argc, char **argv)
{
	char target[5];

	target[0] = '$';
	target[1] = '\0';
	test(target, 0, "");

	test(target, sizeof (target), "");
	test(target, sizeof (target), "1");
	test(target, sizeof (target), "12");
	test(target, sizeof (target), "123");
	test(target, sizeof (target), "1234");
	test(target, sizeof (target), "12345");
	test(target, sizeof (target), "123456");

	return 0;
}
#endif
