/*
 * TextNull.c
 *
 * Copyright 2006 by Anthony Howe. All rights reserved.
 */

#include <com/snert/lib/version.h>
#include <com/snert/lib/util/Text.h>

/***********************************************************************
 ***
 ***********************************************************************/

#undef TextNull

const char *
TextNull(const char *s)
{
	return s == NULL ? "(NULL)" : s;
}
