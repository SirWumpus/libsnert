/*
 * TextSensitiveCompare.c
 *
 * Copyright 2001, 2005 by Anthony Howe. All rights reserved.
 */

#include <com/snert/lib/version.h>
#include <com/snert/lib/util/Text.h>

/***********************************************************************
 ***
 ***********************************************************************/

#ifndef HAVE_STRNCMP
int
TextSensitiveCompareN(const void *xp, const void *yp, long len)
{
	char *x = (char *) xp;
	char *y = (char *) yp;

	if (x == NULL && y != NULL)
		return 1;
	if (x != NULL && y == NULL)
		return -1;
	if (x == NULL && y == NULL)
		return 0;

	if (len < 0)
		return strcmp(x, y);

	return strncmp(x, y, len);
}
#endif

#ifndef HAVE_STRCMP
int
TextSensitiveCompare(const void *xp, const void *yp)
{
	return TextSensitiveCompareN(xp, yp, -1);
}
#endif


