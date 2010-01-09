/*
 * TextMatch.c
 *
 * Copyright 2005, 2010 by Anthony Howe. All rights reserved.
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
 * Find the first occurence of "needle" in "haystack".
 *
 * @param haystack
 *	A C string to search.
 *
 * @param needle
 *	The C string pattern to find. An astrisk (*) acts as wildcard,
 *	scanning over zero or more bytes. A question-mark (?) matches
 *	any single character; a space ( ) will match any single white
 *	space character.
 *
 *	A left square bracket ([) starts a character class that ends
 * 	with a right square bracket (]) and matches one character
 *	from the class. If the first character of the class is a carat
 *	(^), then the remainder of character class is negated. If the
 *	first character (after a carat if any) is a right square bracket,
 *	then the right square bracket is a literal and loses any special
 *	meaning. If the first character (after a carat and/or right
 *	square bracket) is a hypen (-), then the hyphen is a literal
 *	and loses any special meaning. A range expression expressed as
 *	a start character followed by a hyphen followed by an end
 *	character matches a character in character-set order between
 *	start and end characters inclusive.
 *
 * 	A backslash follow by any character treats that character as
 *	a literal (it loses any special meaning).
 *
 *	(If you need more than that, think about using regex(3) instead.)
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
 *	"a[]]c"		exact match for "a]c"
 *
 *	"[abc]"		match a single "a", "b", or "c".
 *
 *	"[^abc]"	match a single charcater except "a", "b", or "c".
 *
 *	"[a-z]"		match a single character "a" through "z" (assumes ASCII)
 *
 *	"[0-9]"		match a single digit "0" through "9" (assumes ASCII)
 *
 *	"[-ac]"		match a single charcater "-", "a", or "c".
 *
 *	"[]-ac]		match a single charcater "]", "-", "a", or "c".
 *
 *	"[^-ac]"	match a single charcater except "-", "a", or "c".
 *
 *	"[^]-ac]	match a single charcater execpt "]", "-", "a", or "c".
 *
 * @param hay_size
 *	How much of haystack to search or -1 for the maximum size or
 *	until a null byte is found.
 *
 * @param caseless
 *	Set true for case insensitive matching.
 *
 * @return
 *	Offset into haystack or -1 if not found.
 */
int
TextMatch(const char *hay, const char *pin, long hay_size, int caseless)
{
	return 0 <= TextFind(hay, pin, hay_size, caseless);
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
