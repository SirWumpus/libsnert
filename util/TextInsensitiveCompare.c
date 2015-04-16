/*
 * TextInsensitiveCompare.c
 *
 * Copyright 2001, 2005 by Anthony Howe. All rights reserved.
 */

#include <com/snert/lib/version.h>
#include <com/snert/lib/util/Text.h>

/***********************************************************************
 ***
 ***********************************************************************/

#ifndef HAVE_STRNCASECMP
int
TextInsensitiveCompareN(const void *xp, const void *yp, long len)
{
	int diff;
	char *x = (char *) xp;
	char *y = (char *) yp;

	if (x == NULL && y != NULL)
		return 1;
	if (x != NULL && y == NULL)
		return -1;
	if (x == NULL && y == NULL)
		return 0;

	for ( ; *x != '\0' && *y != '\0'; ++x, ++y) {
		if (0 <= len && len-- == 0)
			return 0;

		if (*x != *y) {
			diff = tolower(*x) - tolower(*y);
			if (diff != 0)
				return diff;
		}
	}

	if (len == 0)
		return 0;

	return *x - *y;
}
#endif

#ifndef HAVE_STRCASECMP
int
TextInsensitiveCompare(const void *xp, const void *yp)
{
	return TextInsensitiveCompareN(xp, yp, -1);
}
#endif
