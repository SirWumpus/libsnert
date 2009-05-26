/*
 * TextFind.c
 *
 * Copyright 2005, 2006 by Anthony Howe. All rights reserved.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <com/snert/lib/version.h>

/***********************************************************************
 ***
 ***********************************************************************/

/* Character class
 *
 *	[]]		Match a closing square bracket
 *	[abc]		Match a or b or c.
 *	[^abc]		Match anything but a or b or c.
 *	[a-z]		Match a through z (charset order).
 *	[][]		Match open or close square bracket.
 *
 *	[-ac]		Match hyphen, a, or c.
 *	[]-ac]		Match close square breacket, hyphen, a, or c.
 *	[^-ac]
 *	[^]-ac]
 *
 * @param haystack
 *	A C string to search.
 *
 * @param pin
 *	A pointer to the left square bracket of a class.
 *
 * @param caseless
 *	Set true for case insensitive matching.
 *
 * @return
 *	A pointer to the right square bracket of the class for a match,
 *	otherwise NULL for no match.
 */
static const char *
textClass(const char *hay, const char *pin, int caseless)
{
	int negate = 0;
	const char *set = pin+1;

	if (*set == '^' || *set == '!') {
		negate = 1;
		set++;
	}

	do {
		if ((pin = strchr(pin+1, ']')) == NULL)
			return NULL;
	} while (pin == set);

	/* Closing square bracket immediately following
	 * an open square bracket is a literal square
	 * bracket, eg "[]]"
	 */
	if (*set == ']') {
		if (*hay == ']')
			goto result;
		set++;
	}

	/* A hyphen following an open square bracket (or
	 * literal closing square bracket) is a literal
	 * hyphen, eg "[-ac]", "[]-ac]", "[^-ac]", "[^]-ac]"
	 */
	if (*set == '-') {
		if (*hay == '-')
			goto result;
		set++;
	}

	for ( ; *set != ']'; set++) {
		if (*set == '\\' && *hay == set[1]) {
			set++;
			break;
		}

		if (*set == '-') {
			if (caseless
			&& tolower(set[-1]) < tolower(*hay)
			&& tolower(*hay) <= tolower(set[1]))
				break;

			if (!caseless && set[-1] < *hay && *hay <= set[1])
				break;

			set++;
			continue;
		}

		if (caseless && tolower(*hay) == tolower(*set))
			break;

		if (!caseless && *hay == *set)
			break;
	}

	/* Did we reach the end of the class without matching? */
	if (set == pin)
		negate = !negate;
result:
	if (negate)
		return NULL;

	return pin;
}

long
TextFindQuote(const char *string, char *buffer, size_t size)
{
	const char *start = buffer;

	while (0 < size-- && *string != '\0') {
		switch (*string) {
		case '*': case '?': case '[': case '\\':
			*buffer++ = '\\';
			if (size == 0)
				return buffer - start;
			/*@fallthrough@*/
		default:
			*buffer++ = *string++;
		}
	}

	return buffer - start;
}

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
long
TextFind(const char *hay, const char *pin, long hay_size, int caseless)
{
	long offset = -1;
	const char *start;

	if (hay == NULL || pin == NULL)
		return -1;

	if (hay_size < 0)
		hay_size = ~ (unsigned long) 0 >> 1;

#ifdef OTHER
	/* Wildcard all matches everything. */
	if (pin[0] == '*' && pin[1] == '\0')
		return 0;
#endif
	/*** Previous version of this function used a ``stop'' pointer
	 *** variable, which in certain cases when hay_size = -1, could
	 *** wrap around the memory space and end up being less than
	 *** the hay pointer when compared. Now we decrement hay_size
	 *** instead to avoid this bug.
	 ***/

	for (start = hay; *pin != '\0'; hay++, pin++, hay_size--) {
		if (*pin == '*') {
			/* Skip redundant astrisks. */
			while (*++pin == '*')
				;

			/* Pattern with trailing wild card matches the
			 * remainder of the string.
			 */
			if (*pin == '\0') {
				if (offset < 0)
					offset = (long) (hay - start);
				return offset;
			}

#ifdef OFF
			/* Allow *[...] */
			if (*pin == '[') {
				const char *right;

				while ((right = textClass(hay, pin, caseless)) != NULL) {
					if (offset < 0)
						offset = (long) (hay - start);
					hay_size--;
					hay++;
				}

				pin = right;
				hay_size++;
				hay--;
				continue;
			}
#endif

			/* Search string for start of pattern substring.
			 * This is recusive and the depth is limited to
			 * the number of distinct '*' characters in the
			 * pattern.
			 */
			for ( ; 0 < hay_size && *hay != '\0'; hay++, hay_size--) {
				if (TextFind(hay, pin, hay_size, caseless) == 0) {
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
		else if (*pin == '\\') {
			if (*hay != *++pin)
				return -1;
		}

		else do {
			if (*pin == '?')
				break;

			if (*pin == '[') {
				if ((pin = textClass(hay, pin, caseless)) == NULL)
					return -1;
				break;
			}

			if (*pin == ' ' && isspace(*hay))
				break;

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
#include <errno.h>
#include <stdio.h>
#include <string.h>

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

	{ "abc",			"*",		-1, 0 },
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
	{ "a[c", 			"a\\[c",	-1, 0 },
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

	{ "a",				"[abc]",	-1, 0 },
	{ "b",				"[abc]",	-1, 0 },
	{ "c",				"[abc]",	-1, 0 },
	{ "Z",				"[abc]",	-1, -1 },

	{ "a",				"[^abc]",	-1, -1 },
	{ "b",				"[^abc]",	-1, -1 },
	{ "c",				"[^abc]",	-1, -1 },
	{ "Z",				"[^abc]",	-1, 0 },

	{ "]",				"[]]", 		-1, 0 },
	{ "]",				"[^]]",		-1, -1 },
	{ "Z",				"[!]]",		-1, 0 },

	{ "0",				"[0-3]",	-1, 0 },
	{ "1",				"[0-3]",	-1, 0 },
	{ "2",				"[0-3]",	-1, 0 },
	{ "3",				"[0-3]",	-1, 0 },
	{ "4",				"[0-3]",	-1, -1 },

	{ "c",				"[-ac]",	-1, 0 },
	{ "a",				"[-ac]",	-1, 0 },
	{ "-",				"[-ac]",	-1, 0 },
	{ "b",				"[-ac]",	-1, -1 },

	{ "abc 123 def",		"*[0-9]*",	-1, 4 },
	{ "abc 123 def",		"*[0-9][0-9][0-9]*",	-1, 4 },
	{ "abc 123 def",		"* [0-9]*def",	-1, 3 },
	{ "ABCDEFGHI",			"*[d-f]*",	-1, 3 },
	{ "ABC1GHI",			"*c[!d-f]*",	-1, 2 },

	{ NULL,				NULL,		-1, 0 }
};

static int ignore_case;
static char buffer[65536];
static const char usage[] =
"usage:\tTextFind [-i] pattern files...\n"
"\tTextFind -t\n"
"\n"
"-i\t\tignore case\n"
"-t\t\trun built in tests\n"
;

int
main(int argc, char **argv)
{
	FILE *fp;
	int i, ch, match;
	long offset, index;
	size_t length, size;

	while ((ch = getopt(argc, argv, "it")) != -1) {
		switch (ch) {
		case 'i':
			ignore_case = 1;
			break;
		case 't':
			for (i = 0; test[i].haystack != NULL; i++) {
				ignore_case = isupper(*test[i].haystack);
				match = TextFind(test[i].haystack, test[i].needle, test[i].size, ignore_case);
				printf("%2d. %s found=%d size=%ld caseless=%d {%s} {%s}\n", i, match == test[i].expect ? "pass" : "FAIL", match, test[i].size, ignore_case, test[i].haystack, test[i].needle);
			}

			return 0;
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

		for (size = 0; 0 < (length = fread(buffer, 1, sizeof (buffer)-1, fp)); size += length) {
			buffer[length] = '\0';

			for (offset = 0; offset < length; offset++) {
				index = TextFind(buffer+offset, argv[optind], length-offset, ignore_case);
				if (index < 0)
					break;

				offset += index;
				printf("%s: +%lu ", argv[i], size+offset);

				for ( ; buffer[offset] != '\n' && buffer[offset] != '\0'; offset++)
					putchar(buffer[offset]);
				putchar('\n');
			}
		}

		fclose(fp);
	}

	return 0;
}
#endif
