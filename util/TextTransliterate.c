/*
 * TextTransliterate.c
 *
 * Copyright 2005, 2006 by Anthony Howe. All rights reserved.
 */

#include <stdlib.h>
#include <com/snert/lib/version.h>
#include <com/snert/lib/util/Text.h>

/***********************************************************************
 ***
 ***********************************************************************/

void
TextTransliterate(char *s, const char *from_set, const char *to_set, long length)
{
	const char *f;

	if (s == NULL || from_set == NULL || to_set == NULL)
		return;

	if (length < 0)
		length = ((unsigned long) ~0) >> 1;

	for ( ; *s != '\0' && 0 < length; s++, length--) {
		if ((f = strchr(from_set, *s)) != NULL)
			*s = to_set[f - from_set];
	}
}

#ifdef TEST
#include <stdio.h>

static char buffer[20] = "ab;^cd;^ef;";

int
main(int argc, char **argv)
{
	printf("before:[%s]\n", buffer);
	TextTransliterate(buffer, ";^", "\n\t", -1);
	printf("after:[%s]\n", buffer);

	return 0;
}
#endif
