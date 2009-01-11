/*
 * TextSensitiveStartsWith.c
 *
 * Copyright 2001, 2005 by Anthony Howe. All rights reserved.
 */

#include <com/snert/lib/version.h>
#include <com/snert/lib/util/Text.h>

/***********************************************************************
 ***
 ***********************************************************************/

/**
 * <p>
 * Compare the leading part of the string in a case sensitive manner.
 * </p>
 *
 * @param text
 *	The string to check.
 *
 * @param prefix
 *	The string prefix to match.
 *
 * @return
 * 	Return -1 on no match, otherwise the length of the matching prefix.
 */
long
TextSensitiveStartsWith(const char *text, const char *prefix)
{
	const char *start = text;

	for ( ; *prefix != '\0'; ++text, ++prefix) {
		if (*text != *prefix)
			return -1;
	}

	return (long) (text - start);
}

