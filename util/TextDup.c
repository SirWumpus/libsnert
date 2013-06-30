/*
 * TextDup.c
 *
 * Copyright 2001, 2013 by Anthony Howe. All rights reserved.
 */

#include <stdlib.h>

#include <com/snert/lib/version.h>
#include <com/snert/lib/util/Text.h>

#ifdef DEBUG_MALLOC
#include <com/snert/lib/util/DebugMalloc.h>
#endif

/***********************************************************************
 ***
 ***********************************************************************/

#ifndef HAVE_STRDUP
char *
TextDup(const char *orig)
{
	char *copy;
	size_t size;

	size = strlen(orig) + 1;
	if ((copy = malloc(size)) != NULL)
		TextCopy(copy, size, orig);

	return copy;
}
#endif
