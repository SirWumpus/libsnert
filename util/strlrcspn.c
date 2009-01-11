/*
 * strlrcspn.c
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
 * last non-delimiter ahead of a delimiter character.
 *
 * @param string
 *	String to scan backwards through.
 *
 * @param offset
 *	Offset in string of a previously matching non-delimiter.
 *
 * @param delims
 *	String of delimiter characters to match.
 *
 * @return
 *	The offset from the start of the string of the last matching
 *	non-delimiter found while scanning backwards.
 */
int
strlrcspn(const char *string, size_t offset, const char *delims)
{
	const char *d, *s;

	for (s = string + offset - 1; string <= s; s--) {
		for (d = delims; *d != '\0'; d++) {
			 if (*d == *s)
				return (int)(s - string) + 1;
		}
	}

	return 0;
}

