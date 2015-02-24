/*
 * TextCopy.c
 *
 * Copyright 2001, 2010 by Anthony Howe. All rights reserved.
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
 *	A pointer to the source string buffer, which might NOT be null
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
	size_t length;

	for (length = 0; length < tsize; length++) {
		if ((*t++ = *s++) == '\0')
			break;
	}

	if (0 < tsize && tsize <= length) {
		/* Assert the target string is always null terminated. This
		 * means nothing bad will happen if the caller does not test
		 * for truncation and proceeds to use the string in further
		 * C string operations.
		 */
		t[-1] = '\0';

		/* Return expected length of target, strlcpy semantics. */
		while (*s++ != '\0')
			length++;
	}

	return length;
}
#endif

#ifdef TEST
#include <stdio.h>

#define FILLER	"ABCDEFGHIJKLMNOPQRSTUVWXYZ"

void
test(size_t n, char *s, size_t expect)
{
	size_t length;
	char buffer[sizeof (FILLER)];

	if (sizeof (buffer) < n)
		n = sizeof (buffer);

	/* Fill in the target buffer predefined junk. */
	(void) memcpy(buffer, FILLER, sizeof (buffer));

	length = TextCopy(buffer, n, s);

	printf(
		"%s truncated=%d length=%zu size=%zu t=\"%s\" s=\"%s\"\n",
		length == expect ? "pass" : "FAIL",
		n <= length, length, n, buffer, s
	);
}

int
main(int argc, char **argv)
{
	test(0, "", 0);
	test(0, "123", 0);
	test(1, "123", 3);
	test(2, "123", 3);
	test(3, "123", 3);
	test(5, "", 0);
	test(5, "1", 1);
	test(5, "12", 2);
	test(5, "123", 3);
	test(5, "1234", 4);
	test(5, "12345", 5);
	test(5, "123456", 6);

	return 0;
}
#endif
