/*
 * TextCount.c
 *
 * Copyright 2004, 2006 by Anthony Howe. All rights reserved.
 */

#include <string.h>

#include <com/snert/lib/util/Text.h>
#include <com/snert/lib/util/Token.h>

/***********************************************************************
 ***
 ***********************************************************************/

/**
 * <p>
 * Parse the string of delimited tokens into an array of pointers to C
 * strings. A token consists of characters not found in the set of
 * delimiters. It may contain backslash-escape sequences, which shall be
 * converted into literals or special ASCII characters. It may contain
 * single or double quoted strings, in which case the quotes shall be
 * removed, though any backslash escape sequences within the quotes are
 * left as is.
 * </p>
 *
 * @param string
 *	A quoted string. This string will be modified in place.
 *
 * @param delims
 *	A set of delimiter characters. If NULL, then the default of set
 *	consists of space, tab, carriage-return, and line-feed (" \t\r\n").
 *
 * @param argv
 *	An array of pointers to C strings to be filled in. This array is
 *	always terminated by a NULL pointer.
 *
 * @param size
 *	The size of the argv array, which must contain an extra element
 *	for a terminating NULL pointer.
 *
 * @return
 *	The length of the argv array filled. This is less than or equal to
 *	size. Otherwise -1 for an invalid argument if string or argv is NULL,
 *	or if size is too small.
 *
 * @see #TextBackslash(char)
 * @see #TokenSplit(const char *, const char *, char ***, int *, int)
 */
int
TokenSplitA(char *string, const char *delims, char **argv, int size)
{
	char *s, *t;
	int i, quote, escape;

	if (string == NULL || argv == NULL || size < 1)
		return -1;

	if (delims == NULL)
		delims = " \t\r\n";

	s = t = string;
	argv[0] = string;

	/* Skip leading delimiters. */
	s += strspn(s, delims);

	quote = escape = 0;

	for (i =  *s != '\0'; i < size && *s != '\0'; s++) {
		if (escape) {
			*t++ = (char) TextBackslash(*s);
			escape = 0;
			continue;
		}

		switch (*s) {
		case '"': case '\'':
			quote = *s == quote ? 0 : *s;
			continue;

		case '\\':
			escape = 1;
			continue;

		default:
			if (quote == 0 && strchr(delims, *s) != NULL) {
				s += strspn(s, delims)-1;

				/* Is there another argument or did
				 * we just skip trailing delimiters.
				 */
				if (s[1] != '\0') {
					*t++ = '\0';
					argv[i++] = t;
				}
				continue;
			}
			break;
		}

		*t++ = *s;
	}
	*t = '\0';

	argv[i] = NULL;

	return i;
}
