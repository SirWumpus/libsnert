/*
 * arse.c
 *
 * Anthony's Regular Search Expressions
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

#define ASCII_NUL		0x00
#define ASCII_BEL		0x07
#define ASCII_BS		0x08
#define ASCII_TAB		0x09
#define ASCII_LF		0x0A
#define ASCII_VT		0x0B
#define ASCII_FF		0x0C
#define ASCII_CR		0x0D
#define ASCII_ESC		0x1B
#define ASCII_SPACE		0x20
#define ASCII_DEL		0x7F

#define isword(c)		(isalnum(c) || (c) == '_')

typedef struct {
	int caseless;

	const char *hay;
	const char *hay_here;

	const char *pin;
	const char *pin_here;

	const char *start_of_match;
} ARSE;

static int
shorthand(int hay, int pat)
{
	int match = 0;

	switch (pat) {
	case 'a': match = hay == ASCII_BEL; break;
	case 'b': match = hay == '\b'; break;
	case 'e': match = hay == ASCII_ESC; break;
	case 'f': match = hay == '\f'; break;
	case 'n': match = hay == '\n'; break;
	case 'r': match = hay == '\r'; break;
	case 't': match = hay == '\t'; break;
	case 'v': match = hay == '\v'; break;

	case 'd': match = isdigit(hay); break;
	case 'h': match = isxdigit(hay); break;
	case 'p': match = ispunct(hay); break;
	case 's': match = isspace(hay); break;
	case 'w': match = isword(hay); break;

	case 'D': match = !isdigit(hay); break;
	case 'H': match = !isxdigit(hay); break;
	case 'P': match = !ispunct(hay); break;
	case 'S': match = !isspace(hay); break;
	case 'W': match = !isword(hay); break;

	default: match = pat == hay; break;
	}

	return match;
}

static int
literal(int caseless, const char *hay, const char **pin)
{
	int match_set = 1;
	const char *pat = *pin;

	switch (*pat) {
	case '.':
		/* Wildcard matches any character. */
		*pin = pat+1;
		return 1;

	case '\\':
		/* Start of word boundary. */
		if (*pat == '<' && !isword(hay[-1]) && isword(*hay)) {
			*pin = pat+1;
			return 0;
		}

		/* End of word boundary. */
		if (*pat == '>' && isword(hay[-1]) && !isword(*hay)) {
			*pin = pat+1;
			return 0;
		}

		/* A speical character, character literal, or shorthand. */
		if (shorthand(*hay, pat[1])) {
			*pin = pat+2;
			return 1;
		}

	case '[':
		/* Invert (negated) the character class. */
		if (pat[1] == '^') {
			match_set = -1;
			pat++;
		}

		/* A closing square bracket as the first character
		 * in the set is treated a character literal.
		 */
		if (pat[1] == ']' && *hay == ']') {
			*pin = *pat + strcspn(pat+1, "]");
			return match_set;
		}

		for (pat++; *pat != ']'; pat++) {
			/* A character range, eg "0-9", "a-f" which
			 * is done the in native character set order.
			 */
			if (pat[1] == '-' && *pat < pat[2] && *pat <= *hay && *hay <= pat[2])
				break;

			/* A speical character, character literal, or shorthand. */
			if (*pat++ == '\\' && shorthand(*hay, *pat))
				break;

			/* A literal (caseless) character. */
			if (*pat == *hay || (caseless && tolower(*pat) == tolower(*hay)))
				break;
		}

		/* Did we find a match from the character set? */
		if (*pat != ']') {
			*pin = *pat + strcspn(pat+1, "]");
			return match_set;
		}

		/* The character set did not match. */
		return match_set == 1 ? -1 : 1;

	default:
		/* A literal (caseless) character. */
		if (*pat == *hay || (caseless && tolower(*pat) == tolower(*hay))) {
			*pin = pat+1;
			return 1;
		}
	}

	return -1;
}

/**
 * Find the first occurence of "needle" in "haystack".
 *
 * @param haystack
 *	A C string to search.
 *
 * @param needle
 *	The C string pattern to find.
 *
 *	Atoms
 *
 *		^ $		start or end of string
 *		< >		begin and end of word
 *		.		wildcard character
 *		[set]		in character set
 *		[^set]		not in character set
 *		(re)		match subexpression
 *		\punct		literal punctuation character
 *		\alpha		short hand
 *		\xhh		2-hexdigit value hh
 *		\num		1 to 3 octal digits
 *
 *	Operators
 *
 *		re~		does not match
 *		re? re* re+	0 or 1; zero or more (greedy); one or more (greedy)
 *		re!		shortest match;
 *		re1re2		concatenation
 *		re|re		alternation
 *
 *	Shorthands
 *
 *		\\		backslash
 *		\a		0x07 alert/bell
 *		\b		0x08 backspace
 *		\e		0x1B escape
 *		\f		0x0C form feed
 *		\n		0x0A line feed
 *		\r		0x0D carriage return
 *		\t		0x09 tab
 *		\v		0x0B vertical tab
 *
 *		\d		[0-9]
 *		\D		[^0-9]
 *		\h		[0-9A-Fa-f]
 *		\H		[^0-9A-Fa-f]
 *		\p		punctuation (ispunct)
 *		\P		not punctuation
 *		\s		[ \t\b\f\r\n\v]
 *		\S		[^ \t\b\f\r\n\v]
 *		\w		[A-Za-z0-9_]
 *		\W		[^A-Za-z0-9_]
 *
 *


    	a) that supports a regex-like subset and single character backreferences
    	b) shortest match instead of longest
    	c) parse on the fly
    	d) ^ $ anchors, . ? wild cards, * + (long match), # ~ (short match),
    	   < > word boundaries,
         e) \w \W word chars, \d \D digits, \s \S whitespace, and other predefined
            classes with a special design that treats them like "(\w)" so that
            backreferences could be done on single characters;
            \p \P punctuation, \x \X hex digits,
 	f) no ( ) or { } to avoid recusion issues

 	As long as you have enough puntuation characters for special operations,
 	you can parse in real time. Actually if you drop the longest match and only
 	use first/shortest match, you simplify things and you can drop the +
 	operator since you can do "a+" as "aa*"

 	*** To be refined. (ARSE = anthony's reduced search expressions)

 * @param caseless
 *	Set true for case insensitive matching.
 *
 * @return
 *	Offset into haystack or -1 if not found.
 */
long
arseFind(const char *hay, const char *pin, int caseless)
{
	long offset = -1;
	const char *start;

	if (hay == NULL || pin == NULL)
		return 0;

	if (


	for (start = hay; *pin != '\0'; hay++, pin++, hay_size--) {
		if (*pin == '*') {
			/* Skip redundant astrisks. */
			while (*++pin == '*')
				;

			/* Pattern with trailing wild card matches the
			 * remainder of the string.
			 */
			if (*pin == '\0')
				return offset;

			/* Search string for start of pattern substring.
			 * This is recusive and the depth is limited to
			 * the number of distinct '*' characters in the
			 * pattern.
			 */
			for ( ; 0 < hay_size && *hay != '\0'; hay++, hay_size--) {
				if (arseFind(hay, pin, hay_size, caseless) == 0) {
					if (offset < 0)
						offset = (long) (hay - start);

					return offset;
				}
			}

			/* We reached the end of the string without
			 * matching the pattern substring.
			 */
			return -1;
		}

		/* End of string, but not end of pattern? */
		else if (hay_size <= 0 || *hay == '\0')
			return -1;

		/* Have we failed to match a backslash literal? */
		else if (*pin == '\\' && *hay != *++pin)
			return -1;

		do {
			if (*pin == '?')
				break;

			if (*pin == ' ' && isspace(*hay))
				break;
#ifdef NOT_YET
			if (*pin == '#' && isdigit(*hay))
				break;
#endif
			if (caseless && tolower(*hay) != tolower(*pin))
				return -1;

			if (!caseless && *hay != *pin)
				return -1;
		} while (0);

		/* We matched something. */
		if (offset < 0)
			offset = (long) (hay - start);
	}

	/* Have we stop at the end of the pin AND the hay? */
	if (hay_size == 0 || *hay == '\0')
		return 0;

	return -1;
}

#ifdef TEST
#include <stdio.h>

typedef struct {
	const char *haystack;
	const char *needle;
	long size;
	long expect;
} entry;

entry test[] = {
	{ "", 				"", 		-1, 0 },
	{ "", 				"a", 		-1, -1 },
	{ "a", 				"", 		-1, -1 },
	{ "abc", 			"a", 		-1, -1 },
	{ "a", 				"abc", 		-1, -1 },
	{ "abc", 			"abc", 		-1, 0 },

	{ "abc", 			"a?c", 		-1, 0 },
	{ "a c", 			"a?c", 		-1, 0 },
	{ "ac", 			"a?c", 		-1, -1 },

	{ "abc",			"abc*",		-1, 0 },
	{ "abc",			"abc***",	-1, 0 },
	{ "abc blah",			"abc*",		-1, 0 },
	{ "def",			"*def",		-1, 0 },
	{ "blah def",			"*def",		-1, 5 },
	{ "blah def",			"***def",	-1, 5 },

	{ "abc blah def",		"abc*def",	-1, 0 },
	{ "blah blah",			"*abc*",	-1, -1 },
	{ "blah abc blah",		"*abc*",	-1, 5 },
	{ "yabba abc do",		"*abc*",	-1, 6 },
	{ "1st abc 2nd abc 3rd abc",	"*abc*",	-1, 4 },
	{ "blah abc blah def",		"*abc*def",	-1, 5 },
	{ "blah abc blah def blat",	"*abc*def",	-1, -1 },
	{ "blahabcblahdeffoo",		"*abc*def*",	-1, 4 },
	{ "see abc before def blat",	"***abc**def*",	-1, 4 },

	{ "abc", 			"a\\bc",	-1, 0 },
	{ "a c", 			"a\\?c",	-1, -1 },
	{ "a*c", 			"a\\*c",	-1, 0 },
	{ "a?c", 			"a\\?c",	-1, 0 },
	{ "abc blah def",		"abc\\*def",	-1, -1 },
	{ "abc * def",			"abc*\\**def",	-1, 0 },

	{ "AbC * dEf",			"abc*\\**def",	-1, 0 },
	{ "aBc * DeF",			"abc*\\**def",	-1, -1 },

	{ "say something clever here",	"say*here",	-1, 0 },
	{ "say something clever here",	"say*clever*",	-1, 0 },
	{ "say something clever here",	"say*here",	20, -1 },
	{ "say something clever here",	"say",		20, -1 },
	{ "say something clever here",	"say*",		20, 0 },
	{ "say something clever here",	"*clever",	20, 14 },
	{ "say something clever here",	"say*clever",	20, 0 },
	{ "say something clever here",	"say*clev*",	20, 0 },
	{ "say something clever here",	"say*clever*",	20, 0 },
	{ "say something clever here",	"*something*",	20, 4 },
	{ "say something clever here",	"*something*",	13, 4 },

	{ "abc  def",			"*  *",		-1, 3 },
	{ "abc \tdef",			"*  *",		-1, 3 },
	{ "abc \tdef",			"* \\ *",	-1, -1 },
	{ "abc\r\ndef",			"*  *",		-1, 3 },
#ifdef NOT_YET
	{ "abc 123 def",		"*###*",	-1, 4 },
#endif
	{ NULL,				NULL,		-1, 0 }
};

int
main(int argc, char **argv)
{
	int i, match, caseless;

	for (i = 0; test[i].haystack != NULL; i++) {
		caseless = isupper(*test[i].haystack);
		match = arseFind(test[i].haystack, test[i].needle, test[i].size, caseless);
		printf("%2d. %s found=%d size=%ld caseless=%d [%s] [%s]\n", i, match == test[i].expect ? "pass" : "FAIL", match, test[i].size, caseless, test[i].haystack, test[i].needle);
	}

	return 0;
}
#endif

#ifdef TEST2
#include <errno.h>
#include <stdio.h>
#include <string.h>

static int ignore_case;
static char buffer[65536];
static const char usage[] = "usage: arseFind [-i] pattern files...\n";

int
main(int argc, char **argv)
{
	FILE *fp;
	int i, ch;
	long offset;
	size_t length, size;

	while ((ch = getopt(argc, argv, "i")) != -1) {
		switch (ch) {
		case 'i':
			ignore_case = 1;
			break;
		default:
			(void) fprintf(stderr, usage);
			return 64;
		}
	}

	if (argc <= optind) {
		(void) fprintf(stderr, usage);
		return 64;
	}



	for (i = optind + 1; i < argc; i++) {
		if ((fp = fopen(argv[i], "r")) == NULL) {
			fprintf(stderr, "opern error \"%s\": %s (%d)\n", argv[i], strerror(errno), errno);
			continue;
		}

		for (size = 0; 0 < (length = fread(buffer, 1, sizeof (buffer), fp)); size += length) {
			offset = arseFind(buffer, argv[optind], length, ignore_case);
			if (0 <= offset) {
				printf("%s: \"%s\" at offset %lu\n", argv[i], argv[optind], size + offset);
				break;
			}
		}

		fclose(fp);
	}

	return 0;
}
#endif
