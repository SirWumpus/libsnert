/*
 * TokenQuote.c
 *
 * Copyright 2009, 2013 by Anthony Howe. All rights reserved.
 */

#include <stdlib.h>
#include <string.h>

#include <com/snert/lib/version.h>
#include <com/snert/lib/util/Token.h>

#ifdef DEBUG_MALLOC
#include <com/snert/lib/util/DebugMalloc.h>
#endif

/***********************************************************************
 ***
 ***********************************************************************/

/**
 * @param string
 *	An unquoted string.
 *
 * @param delims
 *	A set of characters to be backslash quoted. If NULL, then the
 *	default of set consists of single and double quotes.
 *
 * @return
 *	A pointer to an allocated quoted C string or NULL. It is the
 *	caller's responsiblity to free() this string.
 */
char *
TokenQuote(const char *string, const char *delims)
{
	size_t length;
	char *quoted, *s;

	if (string == NULL)
		return NULL;

	if (delims == NULL)
		delims = "'\"";

	for (length = 0, s = (char *) string; *s != '\0'; s++) {
		if (strchr(delims, *s) != NULL)
			length++;
	}

	length += s - string;

	if ((quoted = malloc(length+1)) != NULL) {
		for (s = quoted; *string != '\0'; string++) {
			if (strchr(delims, *string) != NULL)
				*s++ = '\\';
			*s++ = *string;
		}

		*s = '\0';
	}

	return quoted;
}
