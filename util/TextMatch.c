/*
 * TextMatch.c
 *
 * Copyright 2005, 2006 by Anthony Howe. All rights reserved.
 */

#include <com/snert/lib/version.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include <com/snert/lib/util/Text.h>

/***********************************************************************
 ***
 ***********************************************************************/

/**
 * Match or find the first occurence of "needle" in "haystack".
 *
 * @param haystack
 *	A C string to search.
 *
 * @param needle
 *	The C string pattern to match. An astrisk (*) acts as wildcard,
 *	scanning over zero or more bytes. A question-mark (?) matches
 *	any single character.
 *
 *	"abc"		exact match for "abc"
 *
 *	"abc*"		match "abc" at start of string
 *
 *	"*abc"		match "abc" at the end of string
 *
 *	"abc*def"	match "abc" at the start and match "def"
 *			at the end, maybe with stuff in between.
 *
 *	"*abc*def*"	find "abc", then find "def"
 *
 * @param hay_size
 *	How much of haystack to search or -1 for the maximum size or
 *	until a null byte is found.
 *
 * @param caseless
 *	Set true for case insensitive matching.
 *
 * @return
 *	True if a match was found.
 */
int
TextMatch(const char *hay, const char *pin, long hay_size, int caseless)
{
#ifdef ORIGINAL_VERSION
/* Remove this once I'm a happy chappy. */
	const char *start;

	if (hay == NULL || pin == NULL)
		return 0;

	if (hay_size < 0)
		hay_size = ~ (unsigned long) 0 >> 1;

	start = hay;

	/*** Previous version of this function used a ``stop'' pointer
	 *** variable, which in certain cases when hay_size = -1, could
	 *** wrap around the memory space and end up being less than
	 *** the hay pointer when compared. Now we decrement hay_size
	 *** instead to avoid this bug.
	 ***/

	for ( ; *pin != '\0'; hay++, pin++, hay_size--) {
		if (*pin == '*') {
			/* Skip redundant astrisks. */
			while (*++pin == '*')
				;

			/* Pattern with trailing wild card matches the
			 * remainder of the string.
			 */
			if (*pin == '\0')
				return 1;

			/* Search string for start of pattern substring.
			 * This is recusive and the depth is limited to
			 * the number of distinct '*' characters in the
			 * pattern.
			 */
			for ( ; 0 < hay_size && *hay != '\0'; hay++, hay_size--) {
				if (TextMatch(hay, pin, hay_size, caseless))
					return 1;
			}

			/* We reached the end of the string without
			 * matching the pattern substring.
			 */
			return 0;
		}

		/* End of string, but not end of pattern? */
		else if (hay_size <= 0 || *hay == '\0')
			return 0;

		/* Have we failed to match a backslash literal? */
		else if (*pin == '\\' && *hay != *++pin)
			return 0;

		/* Have we failed to match literals? */
		else if (*pin != '?') {
			if (caseless && tolower(*hay) != tolower(*pin))
				return 0;

			if (!caseless && *hay != *pin)
				return 0;
		}
	}

	/* Have we stop at the end of the pin AND the hay? */
	return hay_size == 0 || *hay == '\0';
#else
	return 0 <= TextFind(hay, pin, hay_size, caseless);
#endif
}

#ifdef TEST
#include <stdio.h>

typedef struct {
	const char *haystack;
	const char *needle;
	long size;
	int expect;
} entry;

entry test[] = {
	{ "", 				"", 		-1, 1 },
	{ "", 				"a", 		-1, 0 },
	{ "a", 				"", 		-1, 0 },
	{ "abc", 			"a", 		-1, 0 },
	{ "a", 				"abc", 		-1, 0 },
	{ "abc", 			"abc", 		-1, 1 },

	{ "abc", 			"a?c", 		-1, 1 },
	{ "a c", 			"a?c", 		-1, 1 },
	{ "ac", 			"a?c", 		-1, 0 },

	{ "abc",			"*",		-1, 1 },
	{ "abc",			"abc*",		-1, 1 },
	{ "abc",			"abc***",	-1, 1 },
	{ "abc blah",			"abc*",		-1, 1 },
	{ "def",			"*def",		-1, 1 },
	{ "blah def",			"*def",		-1, 1 },
	{ "blah def",			"***def",	-1, 1 },

	{ "abc blah def",		"abc*def",	-1, 1 },
	{ "blah blah",			"*abc*",	-1, 0 },
	{ "blah abc blah",		"*abc*",	-1, 1 },
	{ "yabba abc do",		"*abc*",	-1, 1 },
	{ "1st abc 2nd abc 3rd abc",	"*abc*",	-1, 1 },
	{ "blah abc blah def",		"*abc*def",	-1, 1 },
	{ "blah abc blah def blat",	"*abc*def",	-1, 0 },
	{ "blahabcblahdeffoo",		"*abc*def*",	-1, 1 },
	{ "see abc before def blat",	"***abc**def*",	-1, 1 },

	{ "abc", 			"a\\bc",	-1, 1 },
	{ "a c", 			"a\\?c",	-1, 0 },
	{ "a*c", 			"a\\*c",	-1, 1 },
	{ "a?c", 			"a\\?c",	-1, 1 },
	{ "a[c", 			"a\\[c",	-1, 1 },
	{ "abc blah def",		"abc\\*def",	-1, 0 },
	{ "abc * def",			"abc*\\**def",	-1, 1 },

	{ "AbC * dEf",			"abc*\\**def",	-1, 1 },
	{ "aBc * DeF",			"abc*\\**def",	-1, 0 },

	{ "say something clever here",	"say*here",	-1, 1 },
	{ "say something clever here",	"say*clever*",	-1, 1 },
	{ "say something clever here",	"say*here",	20, 0 },
	{ "say something clever here",	"say",		20, 0 },
	{ "say something clever here",	"say*",		20, 1 },
	{ "say something clever here",	"*clever",	20, 1 },
	{ "say something clever here",	"say*clever",	20, 1 },
	{ "say something clever here",	"say*clev*",	20, 1 },
	{ "say something clever here",	"say*clever*",	20, 1 },
	{ "say something clever here",	"*something*",	20, 1 },
	{ "say something clever here",	"*something*",	13, 1 },

	{ NULL,				NULL,		-1, 0 }
};

int
main(int argc, char **argv)
{
	int i, match, caseless;

	for (i = 0; test[i].haystack != NULL; i++) {
		caseless = isupper(*test[i].haystack);
		match = TextMatch(test[i].haystack, test[i].needle, test[i].size, caseless);
		printf("%2d. %s match=%d size=%ld caseless=%d {%s} {%s}\n", i, match == test[i].expect ? "pass" : "FAIL", match, test[i].size, caseless, test[i].haystack, test[i].needle);
	}

	return 0;
}
#endif
