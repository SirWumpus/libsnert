/*
 * TextDupN.c
 *
 * Copyright 2006 by Anthony Howe. All rights reserved.
 */

#include <stdlib.h>
#include <com/snert/lib/version.h>
#include <com/snert/lib/util/Text.h>

/***********************************************************************
 ***
 ***********************************************************************/

char *
TextDupN(const char *orig, size_t length)
{
	char *copy;

	if ((copy = malloc(length+1)) != NULL) {
		(void) memcpy(copy, orig, length);
		copy[length] = '\0';
	}

	return copy;
}

