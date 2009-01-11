/*
 * fnext.c
 *
 * Copyright 2005, 2006 by Anthony Howe. All rights reserved.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <com/snert/lib/version.h>

/***********************************************************************
 ***
 ***********************************************************************/

/*
 * Find the next occurence of string. If the string is not found.
 * rewind to the file pointer to its starting point.
 *
 * @param fp
 *	File pointer to scan through.
 *
 * @param s
 *	The string to match. An astrisk '*' acts as  wildcard,
 *	scanning over zero or more bytes:
 *
 *	"abc"		match "abc" at the current offset
 *
 *	"*abc"		find "abc" from the current offset.
 *
 *	"abc*def"	match "abc", then find "def".
 *
 *	"*abc*def"	find "abc", then find "def"
 *
 * @return
 *	True on a successful match.
 */
int
fnext(FILE *fp, char *s)
{
	int ch;
	char *t = s;
	long mark = ftell(fp);

	while (*t != '\0' && (ch = fgetc(fp)) != EOF) {
		if (*t == '*')
			s = t++;
		if (tolower(ch) != *t) {
			if (*s != '*')
				break;
			t = s;
		}

		t++;
	}

	if (*t != '\0')
		fseek(fp, mark, SEEK_SET);

	return *t == '\0';
}

