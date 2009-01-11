/*
 * TextInsensitiveEndsWith.c
 *
 * Copyright 2001, 2006 by Anthony Howe. All rights reserved.
 */

#include <com/snert/lib/version.h>
#include <ctype.h>
#include <com/snert/lib/util/Text.h>

/***********************************************************************
 ***
 ***********************************************************************/

/**
 * <p>
 * Compare the trailing part of the string in a case insensitive manner.
 * </p>
 *
 * @param text
 *	The string to check.
 *
 * @param suffix
 *	The string suffix to match.
 *
 * @return
 *	Return the offset into text where suffix starts, otherwise -1
 *	on no match.
 */
long
TextInsensitiveEndsWith(const char *text, const char *suffix)
{
	size_t length = strlen(suffix);
	const char *etext = text + strlen(text);
	const char *esuffix = suffix + length;

	while (suffix < esuffix) {
		if (etext <= text)
			return -1;

		--etext;
		--esuffix;

		if (tolower(*etext) != tolower(*esuffix))
			return -1;
	}

	return (long) (etext - text);
}

#ifdef TEST
#include <stdio.h>

int
main(int argc, char **argv)
{
	int offset;

	offset = TextInsensitiveEndsWith("lOOk", "loOK");
	printf("%s %d\n", offset == 0 ? "PASS" : "FAIL", offset);

	offset = TextInsensitiveEndsWith("we look", "loOK");
	printf("%s %d\n", offset == 3 ? "PASS" : "FAIL", offset);

	offset = TextInsensitiveEndsWith("we look but never FInd", "noT FInd");
	printf("%s %d\n", offset == -1 ? "PASS" : "FAIL", offset);

	return 0;
}
#endif
