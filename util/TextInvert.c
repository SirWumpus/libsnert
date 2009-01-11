/*
 * TextInvert.c
 *
 * Copyright 2005, 2006 by Anthony Howe. All rights reserved.
 */

#include <com/snert/lib/version.h>
#include <ctype.h>
#include <com/snert/lib/util/Text.h>

/***********************************************************************
 ***
 ***********************************************************************/

void
TextInvert(char *s, long length)
{
	if (s == NULL)
		return;

	if (length < 0)
		length = ((unsigned long) ~0) >> 1;

	for ( ; *s != '\0' && 0 < length; s++, length--) {
		*s = (char) (isupper(*s) ? tolower(*s) : toupper(*s));
	}
}
