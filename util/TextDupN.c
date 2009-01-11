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
TextDupN(const char *orig, size_t size)
{
	char *copy;

	if ((copy = malloc(size+1)) != NULL) {
		memcpy(copy, orig, size);
		copy[size] = '\0';
	}

	return copy;
}

