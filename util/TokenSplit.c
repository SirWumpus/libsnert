/*
 * TokenSplit.c
 *
 * Copyright 2004, 2013 by Anthony Howe. All rights reserved.
 */

#include <com/snert/lib/version.h>

#include <stdlib.h>
#include <string.h>

#include <com/snert/lib/util/Text.h>
#include <com/snert/lib/util/Token.h>

#ifdef DEBUG_MALLOC
# include <com/snert/lib/util/DebugMalloc.h>
#endif

/***********************************************************************
 ***
 ***********************************************************************/

/**
 * <p>
 * Parse the string of delimited tokens into an array of pointers to C
 * strings. A token consists of characters not found in the set of
 * delimiters. It may contain backslash-escape sequences, which shall be
 * converted into literals or special ASCII characters. It may contain
 * single or double quoted strings, in which case the quotes shall be
 * removed, though any backslash escape sequences within the quotes are
 * left as is.
 * </p>
 *
 * @param string
 *	A quoted string. A copy is made.
 *
 * @param delims
 *	A set of delimiter characters. If NULL, then the default of set
 *	consists of space, tab, carriage-return, and line-feed (" \t\r\n").
 *
 * @param pargv
 *	Used to pass back to the caller an array of pointers to C strings.
 *	The array is always terminated by a NULL pointer. Its the caller's
 *	responsibility to free() this array when done, which will also
 *	free its contents.
 *
 * @param pargc
 *	Used to pass back the length of the array of pointers to C strings,
 *	excluding the ternminating NULL pointer. Can be NULL to ignore.
 *
 * @param pad
 *	The array passed back is prefixed with this many unused elements.
 *	The first token from string will be placed at this offset.
 *
 * @return
 *	Zero on success. Otherwise -1 on error.
 *
 * @see #TextBackslash(char)
 * @see #TokenSplitA(char *, const char *, char **, int, int)
 */
int
TokenSplit(const char *string, const char *delims, char ***pargv, int *pargc, int pad)
{
	int argc;
	char **argv;
	size_t size;

	if (string == NULL || pargv == NULL)
		return -1;

	size = strlen(string)+1;
	argc = pad + TokenCount(string, delims) + 1;
	argv = (char **) malloc(argc * sizeof (*argv) + size);

	if (argv == NULL)
		return -1;

	/* Save a copy of the string we can modify. */
	TextCopy((char *) &argv[argc], size, string);
	string = (char *) &argv[argc];

	/* Split our string in place into the array of tokens. */
	if (TokenSplitA((char *) string, delims, argv+pad, argc-pad) != argc-pad-1) {
		free(argv);
		return -1;
	}

	*pargv = argv;

	if (pargc != NULL)
		*pargc = argc-1;

	return 0;
}

#ifdef TEST
#include <stdio.h>

void
TestTokenSplit(char *string, char *delims)
{
	char **argv;
	int i, argc, pad = 0;

	printf("string=[%s] delims=[%s]\n", string, delims);
	if (TokenSplit(string, delims, &argv, &argc, pad)) {
		printf("error\n");
		return;
	}

	printf("  length=%d ", argc);
	for (i = pad; i < argc; i++) {
		printf("[%s]", argv[i] == NULL ? "" : argv[i]);
	}

	if (argv[argc] != NULL)
		printf(" array not NULL terminated");

	printf("\n");
	free(argv);
}

int
main(int argc, char **argv)
{
	printf("\n--TokenSplit--\n");

	/* Empty string and empty delimeters. */
	TestTokenSplit("", "");				/* length=0 */

	/* Empty string. */
	TestTokenSplit("", ",");			/* length=0 */

	/* Empty delimiters. */
	TestTokenSplit("a,b,c", "");			/* length=1 [a,b,c] */

	/* Assorted combinations of empty tokens. */
	TestTokenSplit(",", ",");			/* length=0 */
	TestTokenSplit("a,,", ",");			/* length=1 [a] */
	TestTokenSplit(",b,", ",");			/* length=1 [b] */
	TestTokenSplit(",,c", ",");			/* length=1 [c] */
	TestTokenSplit("a,,c", ",");			/* length=2 [a][c] */
	TestTokenSplit("a,b,c", ",");			/* length=3 [a][b][c] */

	/* Quoting of tokens. */
	TestTokenSplit("a,b\\,c", ",");			/* length=2 [a][b,c] */
	TestTokenSplit("a,'b,c'", ",");			/* length=2 [a][b,c] */
	TestTokenSplit("\"a,b\",c", ",");		/* length=2 [a,b][c] */
	TestTokenSplit("a,'b,c'd,e", ",");		/* length=3 [a][b,cd][e] */
	TestTokenSplit("a,'',e", ",");			/* length=3 [a][][e] */
	TestTokenSplit("a,b''d,e", ",");		/* length=3 [a][bd][e] */

	/* Double quote embedded within single quotes. */
	TestTokenSplit("a,b'd\",e',f", ",");		/* length=3 [a][bd",e][f] */

	/* Missing close quote. */
	TestTokenSplit("a,b'd,e", ",");			/* length=2 [a][bd,e] */

	/* Backslash literal quotes and missing close quote. */
	TestTokenSplit("a,b\\'d\\\",e',f", ",");	/* length=3 [a][b'd"][e,f] */

        TestTokenSplit("name=\"value's\"", ",");        /* length=1 [name=value's] */
	TestTokenSplit("name=\"value's\"", "=,");        /* length=2 [name][value's] */

	printf("\n--DONE--\n");

	return 0;
}
#endif
