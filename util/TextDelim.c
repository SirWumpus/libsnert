/*
 * TextDelim.c
 *
 * Copyright 2006 by Anthony Howe. All rights reserved.
 */

#include <com/snert/lib/version.h>
#include <com/snert/lib/util/Text.h>

/***********************************************************************
 ***
 ***********************************************************************/

const char *
TextDelim(const char *s, const char *delim)
{
	return (s == NULL || *s == '\0') ? "" : delim;
}
