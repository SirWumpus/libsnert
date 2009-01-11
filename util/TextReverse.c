/*
 * TextReverse.c
 *
 * Copyright 2005, 2006 by Anthony Howe. All rights reserved.
 */

#include <com/snert/lib/version.h>
#include <com/snert/lib/util/Text.h>

/***********************************************************************
 ***
 ***********************************************************************/

void
TextReverse(char *s, long length)
{
	char ch, *x, *y;

	if (s == NULL)
		return;

	if (length < 0)
		length = (long) strlen(s);

	/* Reverse segment of string. */
	for (x = s, y = s+length; x < --y; x++) {
		ch = *y;
		*y = *x;
		*x = ch;
	}
}

