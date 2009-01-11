/*
 * strlrspn.c
 *
 * Copyright 2001, 2006 by Anthony Howe. All rights reserved.
 */

#include <com/snert/lib/version.h>
#include <com/snert/lib/util/Text.h>

/***********************************************************************
 ***
 ***********************************************************************/

/**
 * Scan backwards from an offset in the string looking for the
 * last matching delimiter ahead of a non-delimiter character.
 *
 * @param string
 *	String to scan backwards through.
 *
 * @param offset
 *	Offset in string of a previously matching delimiter.
 *
 * @param delims
 *	String of delimiter characters to match.
 *
 * @return
 *	The offset from the start of the string of the last matching
 *	delimiter found while scanning backwards.
 */
int
strlrspn(const char *string, size_t offset, const char *delims)
{
	const char *d, *s;

	for (s = string + offset - 1; string <= s; s--) {
		for (d = delims; ; d++) {
			if (*d == '\0')
				return (int)(s - string) + 1;
			else if (*d == *s)
				break;
		}
	}

	return 0;
}

