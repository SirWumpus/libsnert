/*
 * setBitWord.c
 *
 * Copyright 2004, 2006 by Anthony Howe.  All rights reserved.
 */

#include <stdlib.h>
#include <com/snert/lib/version.h>
#include <com/snert/lib/util/Text.h>
#include <com/snert/lib/util/setBitWord.h>

/***********************************************************************
 ***
 ***********************************************************************/

unsigned long
setBitWord(struct bitword *bits, const char *detail)
{
	return setBitWord2(bits, detail, NULL, 0);
}

unsigned long
setBitWord2(struct bitword *bits, const char *detail, const char *delims, unsigned long oflags)
{
	long i;
	Vector words;
	struct bitword *p;
	unsigned long flags;
	char *stop, *word, set;

	if (bits == NULL || detail == NULL)
		return oflags;

	flags = (unsigned long) strtol(detail, &stop, 0);
	if (*stop == '\0')
		return flags;

	if (delims == NULL)
		delims = ",";

	if ((words = TextSplit(detail, delims, 0)) == NULL)
		return oflags;

	flags = oflags;

	for (i = 0; i < VectorLength(words); i++) {
		word = VectorGet(words, i);
		switch (*word) {
		case '+': case '-': case '!':
			set = *word++;
			break;
		default:
			set = '+';
		}

		for (p = bits; p->bit != 0; p++) {
			if (TextInsensitiveCompare(word, p->name) == 0) {
				if (set == '+')
					flags |= p->bit;
				else
					flags &= ~p->bit;
				break;
			}
		}
	}

	VectorDestroy(words);

	return flags;
}

long
selectWord(const char **table, const char *word)
{
	long i;

	for (i = 0; table[i] != NULL; i++) {
		if (TextInsensitiveCompare(word, table[i]) == 0)
			return i;
	}

	return -1;
}
