/*
 * TextJoin.c
 *
 * Copyright 2001, 2005 by Anthony Howe. All rights reserved.
 */

#include <com/snert/lib/version.h>
#include <com/snert/lib/util/Buf.h>
#include <com/snert/lib/util/Text.h>

/***********************************************************************
 ***
 ***********************************************************************/

/**
 * <p>
 * Join a vector of strings into a string with the given delimiter.
 * </p>
 *
 * @param delim
 *	A delimiter of one or more characters.
 *
 * @param strings
 *	A list of strings to be joined.
 *
 * @return
 *	A string comprised of all the objects in string representation
 *	each separated by the string delimiter.
 */
char *
TextJoin(const char *delim, Vector strings)
{
	long i;
	char *s;
	Buf *collector = BufCreate(100);

	for (i = 0; i < VectorLength(strings); ++i) {
		BufAddString(collector, VectorGet(strings, i));
		BufAddString(collector, (char *) delim);
	}

	BufSetLength(collector, BufLength(collector) - strlen(delim));
	s = BufToString(collector);
	BufDestroy(collector);

	return s;
}

