/*
 * TextSplit.c
 *
 * Copyright 2001, 2012 by Anthony Howe. All rights reserved.
 */

#include <stdlib.h>
#include <com/snert/lib/version.h>
#include <com/snert/lib/util/Text.h>
#include <com/snert/lib/util/Token.h>

/***********************************************************************
 ***
 ***********************************************************************/

/**
 * <p>
 * The given string contains a list of substrings separated by the
 * specified delimiter characters. The substrings may contain quoted
 * strings and/or contain backslash-escaped characters. The common
 * backslash escape sequences are supported and return their ASCII
 * values.
 * </p>
 *
 * @param string
 *	A list represented as a string.
 *
 * @param delims
 *	A set of delimiter characters.
 *
 * @param flags
 *
 *	TOKEN_KEEP_EMPTY
 *
 *	If false, then a run of one or more delimeters is treated as a
 *	single delimeter separating tokens. Otherwise each delimeter
 *	separates a token that may be empty.
 *
 *	string		true		false
 *	-------------------------------------------
 *	[a,b,c]		[a] [b] [c]	[a] [b] [c]
 *	[a,,c]		[a] [] [c]	[a] [c]
 *	[a,,]		[a] [] [] 	[a]
 *	[,,]		[] [] []	(null)
 *	[]		[]		(null)
 *
 *	TOKEN_KEEP_BACKSLASH
 *
 *	The token might have backslash escapes that are suppose to be
 *	part of the token, like a regex string /RE/ where you need to
 *	keep any "\/" between the open and closing slashes. We still
 *	need to recognise escapes and not convert them to a literal.
 *
 *	TOKEN_IGNORE_QUOTES
 *
 *	Disable any special processing of quoted substrings; quotes
 *	are treated as literals.
 *
 *	TOKEN_KEEP_ASIS
 *
 *	Shorthand for TOKEN_KEEP_BACKSLASH | TOKEN_IGNORE_QUOTES.
 *
 *	TOKEN_KEEP_BRACKETS
 *
 *	Split strings with brackets, keeping the open and close:
 *	parenthesis, "(" and ")"; angle brackets, "<" and ">"; square
 *	brackets, "[" and "]"; and/or braces, "{" and "}" grouped
 *	together. Both open and close brackets must in the set of
 *	delimiters. For example:
 *
 *	string		delims	vector
 *	-------------------------------------------
 *	"a{b}c"		"{}"	"a", "{b}", "c"
 *	"a{{b}}c"	"{}"	"a", "{{b}}", "c"
 *	"a{{b\{c}}d"	"{}"	"a", "{{b{c}}", "d"
 *	"a{{b[(<c}}d"	"{}"	"a", "{{b[(<c}}", "d"
 *	"a{b{c}{d}e}f"	"{}"	"a", "{b{c}{d}e}", "f"
 *	"<>a{b<c>d}<e>"	"{}<>"	"<>", "a", "{b<c>d}", "<e>", ""
 *
 * @return
 *	A vector of C strings.
 */
Vector
TextSplit(const char *string, const char *delims, int flags)
{
	char *token;
	Vector list;

	if ((list = VectorCreate(5)) == NULL)
		return NULL;

	VectorSetDestroyEntry(list, free);

	while ((token = TokenNext(string, &string, delims, flags)) != NULL)
		(void) VectorAdd(list, token);

	return list;
}

/***********************************************************************
 *** Test
 ***********************************************************************/

#ifdef TEST
#include <stdio.h>
#include <com/snert/lib/type/Vector.h>
#include <com/snert/lib/util/getopt.h>

typedef struct {
	const char *string;
	const char *delims;
	int flags;
	int expect_length;
	const char *expect_items[6];
} test_case;

static test_case tests[] = {

	/* Empty string and empty delimeters. */
	{ "", 		"", TOKEN_KEEP_EMPTY, 1, { "" } },
	{ "", 		"", 0, 0 },

	/* Empty string. */
	{ "", 		",", TOKEN_KEEP_EMPTY, 1, { "" } },
	{ "", 		",", 0, 0 },

	/* Empty delimiters. */
	{ "a,b,c", 	"", TOKEN_KEEP_EMPTY, 1, { "a,b,c" } },
	{ "a,b,c", 	"", 0, 1, { "a,b,c" } },

	/* Assorted combinations of empty tokens. */
	{ ",", 		",", TOKEN_KEEP_EMPTY, 2, { "", "" } },
	{ ",", 		",", 0, 0 },
	{ "a,,", 	",", TOKEN_KEEP_EMPTY, 3, { "a", "", "" } },
	{ "a,,", 	",", 0, 1, { "a" } },
	{ ",b,", 	",", TOKEN_KEEP_EMPTY, 3, { "", "b", "" } },
	{ ",b,", 	",", 0, 1, { "b" } },
	{ ",,c", 	",", TOKEN_KEEP_EMPTY, 3, { "", "", "c" } },
	{ ",,c", 	",", 0, 1, { "c" } },
	{ "a,,c", 	",", TOKEN_KEEP_EMPTY, 3, { "a", "", "c" } },
	{ "a,,c", 	",", 0, 2, { "a", "c" }  },
	{ "a,b,c", 	",", TOKEN_KEEP_EMPTY, 3, { "a", "b", "c" } },
	{ "a,b,c", 	",", 0, 3, { "a", "b", "c" } },

	/* Quoting of tokens. */
	{ "a,b\\,c", 	",", TOKEN_KEEP_EMPTY, 2, { "a", "b,c" } },
	{ "a,b\\,c", 	",", 0, 2, { "a", "b,c" } },
	{ "a,'b,c'", 	",", TOKEN_KEEP_EMPTY, 2, { "a", "b,c" } },
	{ "a,'b,c'", 	",", 0, 2, { "a", "b,c" } },
	{ "\"a,b\",c", 	",", TOKEN_KEEP_EMPTY, 2, { "a,b", "c" } },
	{ "\"a,b\",c", 	",", 0, 2, { "a,b", "c" } },
	{ "a,'b,c'd,e",	",", TOKEN_KEEP_EMPTY, 3, { "a", "b,cd", "e" } },
	{ "a,'b,c'd,e",	",", 0, 3, { "a", "b,cd", "e" } },
	{ "a,'',e", 	",", TOKEN_KEEP_EMPTY, 3, { "a", "", "e" } },
	{ "a,'',e", 	",", 0, 3, { "a", "", "e" } },
	{ "a,b''d,e", 	",", TOKEN_KEEP_EMPTY, 3, { "a", "bd", "e" } },
	{ "a,b''d,e", 	",", 0, 3, { "a", "bd", "e" } },

	/* Double quoted string containing single quotes. */
	{ "\"a'b\",c", 	",", 0, 2, { "a'b", "c" } },

	/* Single quoted string containing double quotes. */
	{ "'a\"b',c", 	",", 0, 2, { "a\"b", "c" } },

	/* Literal backslash in and out of a quoted string. */
	{ "'a\\\\b',c\\\\d", 	",", 0, 2, { "a\\b", "c\\d" } },

	/* Literal quotes. */
	{ "a\\\"b,c\\'d", 	",", 0, 2, { "a\"b", "c'd" } },

	/* Ignore quotes. */
	{ "a\"b,c\"d", 	",", TOKEN_IGNORE_QUOTES, 2, { "a\"b", "c\"d" } },
	{ "a'b,c'd", 	",", TOKEN_IGNORE_QUOTES, 2, { "a'b", "c'd" } },

	/* Keep backslash escapes. */
	{ "/a\\/b\\/c/ tail",	"/ ", TOKEN_KEEP_BACKSLASH, 2, { "a\\/b\\/c", "tail" } },

	/* Keep backslash escapes and ignore quotes. */
	{ "/a'\\/b\\/'c/ tail",	"/ ", TOKEN_KEEP_ASIS, 2, { "a'\\/b\\/'c", "tail" } },

	/* Keep quotes. */
	{ "tab\\t,\"tab,\\t, quotes\",tab\\t", ",", TOKEN_KEEP_QUOTES, 3, { "tab\t", "\"tab,\t, quotes\"", "tab\t" } },

	/* Open & close delimiters. */
	{ "a{b}c", "{}", TOKEN_KEEP_EMPTY|TOKEN_KEEP_BRACKETS, 3, { "a", "{b}", "c" } },
	{ "a{b}c", "{}", TOKEN_KEEP_BRACKETS, 3, { "a", "{b}", "c" } },

	/* Nested braces. */
	{ "a{{b}}c", "{}", TOKEN_KEEP_EMPTY|TOKEN_KEEP_BRACKETS, 3, { "a", "{{b}}", "c" } },
	{ "a{{b\\{c}}d", "{}", TOKEN_KEEP_EMPTY|TOKEN_KEEP_BRACKETS, 3, { "a", "{{b{c}}", "d" } },
	{ "a{{b\\{c}}d", "{}", TOKEN_KEEP_EMPTY|TOKEN_KEEP_BRACKETS|TOKEN_KEEP_BACKSLASH, 3, { "a", "{{b\\{c}}", "d" } },
	{ "a{{b[(<c}}d", "{}", TOKEN_KEEP_EMPTY|TOKEN_KEEP_BRACKETS, 3, { "a", "{{b[(<c}}", "d" } },
	{ "a{b{c}{d}e}f", "{}", TOKEN_KEEP_EMPTY|TOKEN_KEEP_BRACKETS, 3, { "a", "{b{c}{d}e}", "f" } },
	{ "<>a{b<c>d}<e>", "{}<>", TOKEN_KEEP_EMPTY|TOKEN_KEEP_BRACKETS, 5, { "<>", "a", "{b<c>d}", "<e>", "" } },

// Consider split braces,
//	{ "a{b,c}d", "{}", TOKEN_KEEP_EMPTY|TOKEN_KEEP_BRACKETS, 4, { "a", "{b", "c}", "d" } },

	{ NULL, NULL, 0, 0 }
};

int
test_text_split(test_case *test)
{
	int i, rc;
	char **token;
	Vector tokens;

	rc = 0;

	printf("s=\"%s\" d=\"%s\" f=0x%02X ", test->string, test->delims, test->flags);
	tokens = TextSplit(test->string, test->delims, test->flags);
	if (tokens == NULL) {
		return -1;
	}

	printf("e=%d l=%ld ", test->expect_length, VectorLength(tokens));
	if (test->expect_length != VectorLength(tokens)) {
		rc = -1;
	}

	for (i = 0, token = (char **)VectorBase(tokens); *token != NULL; token++, i++) {
		if (test->expect_items[i] == NULL || strcmp(*token, test->expect_items[i]) != 0)
			rc = -1;
		printf("[%s]", *token);
	}
	if (test->expect_items[i] != NULL)
		rc = -1;

	printf("... %s\n", rc == 0 ? "OK" : "FAIL");
	VectorDestroy(tokens);

	return rc;
}

int
test_suite(void)
{
	int ex;
	test_case *test;

	ex = EXIT_SUCCESS;
	for (test = tests; test->string != NULL; test++) {
		if (test_text_split(test))
			ex = EXIT_FAILURE;
	}
	
	return ex;
}

static const char usage[] =
"usage: TextSplit [-t][-d delims][-f flags] string ...\n"
;

int
main(int argc, char **argv)
{
	Vector v;
	char *delims;
	int i, ch, argi, flags;

	flags = 0;
	delims = NULL;
	
	while ((ch = getopt(argc, argv, "d:f:t")) != -1) {
		switch (ch) {
		case 'd':
			delims = optarg;
			break;
			
		case 'f':
			flags = (int) strtol(optarg, NULL, 0);
			break;
			
		case 't':
			return test_suite();
		
		default:
			optind = argc;
		}
	}
	
	if (argc <= optind) {
		(void) fputs(usage, stderr);
		return EXIT_FAILURE;
	}
	
	for (argi = optind; argi < argc; argi++) {		
		if ((v = TextSplit(argv[argi], delims, flags)) == NULL) {
			(void) fprintf(stderr, "out of memory\n");
			return EXIT_FAILURE;
		}
		
		for (i = 0; i < VectorLength(v); i++)
			(void) printf("%s\n", (char *)VectorGet(v, i));			

		VectorDestroy(v);
	}

	return EXIT_SUCCESS;
}
#endif
