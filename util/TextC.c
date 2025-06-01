/*
 * Text.c
 *
 * Copyright 2001, 2013 by Anthony Howe.  All rights reserved.
 */

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>

#include <com/snert/lib/version.h>
#include <com/snert/lib/util/Buf.h>
#include <com/snert/lib/util/Text.h>
#include <com/snert/lib/util/Token.h>
#include <com/snert/lib/io/posix.h>

#ifdef DEBUG_MALLOC
# include <com/snert/lib/util/DebugMalloc.h>
#endif

typedef int (*TextCompareFunction)(const void *, const void *);
typedef long (*TextLongCompareFunction)(const void *, const void *);

/***********************************************************************
 ***
 ***********************************************************************/

void
boundOffsetLength(long slength, long *offset, long *length)
{
	/* Correct offset if out of bounds. */
	if (*offset < 0)
		*offset = 0;
	else if (slength < *offset)
		*offset = slength;

	/* Correct length if out of bounds. */
	if (*length < 0)
		*length = slength;
	if (slength < *offset + *length)
		*length = slength - *offset;
}

char *
TextSubstring(const char *orig, long offset, long length)
{
	char *copy;

	if (orig == (const char *) 0)
		return NULL;

	boundOffsetLength(strlen((char *) orig), &offset, &length);

	copy = malloc(length + 1);
	if (copy == NULL)
		return NULL;

	(void) strncpy(copy, orig+offset, length);
	copy[length] = '\0';

	return copy;
}

/**
 * <p>
 * Does the string consist only of digits in the given radix?
 * </p>
 *
 * @param s
 *	A string to scan.
 *
 * @param radix
 *	The expected radix.
 *
 * @return
 *	True if the string conatins only characters for the given
 *	integer in the given radix.
 */
int
TextIsInteger(const char *s, int radix)
{
	char *digit;
	static char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";

	if (*s == '\0' || radix < 2 || 36 < radix)
		return 0;

	for ( ; *s != '\0'; ++s) {
		digit = strchr(digits, *s);
		if (digit == NULL || radix <= digit - digits)
			return 0;
	}

	return 1;
}

/**
 * <p>
 * Expand all the tab characters into spaces. Tabstops are assumed
 * to be every 8th column.
 * </p>
 *
 * @param s
 *	A string buffer containing tab characters to be expanded.
 *
 * @param col
 *	Initial column position
 *
 * @return
 * 	A string buffer where all tab characters have been converted
 *	to spaces.
 */
Buf *
TextExpand(Buf *s, long col)
{
	size_t i;
	unsigned char byte;
	Buf *t = BufCreate(100);

	for (i = 0; i < BufLength(s); ++i, ++col) {
		byte = BufGetByte(s, i);
		if (byte == '\t') {
			do {
				BufAddByte(t, ' ');
			} while ((++col & 7) != 0);
		} else {
			BufAddByte(t, byte);
		}
	}

	return t;
}

/**
 * <p>
 * Convert a byte array into a hex encoded string.
 * </p>
 *
 * @param b
 *	A byte array.
 *
 * @return
 *	A string representing the byte array as hexdecimal values.
 */
char *
TextHexEncode(Buf *b)
{
	size_t i;
	Buf *s = BufCreate(BufLength(b) * 2 + 1);
	static char xdigits[] = "0123456789ABCDEF";

	for (i = 0; i < BufLength(b); ++i) {
		unsigned char a = BufGetByte(b, i);
		BufAddByte(s, xdigits[(a >> 4) & 0xf]);
		BufAddByte(s, xdigits[a & 0xf]);
	}

	BufAddByte(s, '\0');

	return (char *) BufAsBytes(s);
}

/**
 * <p>
 * Find the first occurence of a substring.
 * </p>
 *
 * @param text
 *	The string to search.
 *
 * @param sub
 *	The substring to find.
 *
 * @return
 * 	Return -1 on no match, otherwise the offset of the substring.
 */
long
TextSensitiveFind(const char *text, const char *sub)
{
	const char *t, *stop;
	size_t textLength, subLength;

	/* Impossible to match undefined strings. */
	if (text == NULL || sub == NULL)
		return -1;

	subLength = strlen(sub);
	textLength = strlen(text);

	/* Empty string matches end of string. */
	if (subLength == 0)
		return textLength;

	stop = &text[textLength - subLength + 1];

	for (t = text; t < stop; t++) {
		if (TextSensitiveCompareN(t, sub, subLength) == 0)
			return (long) (t - text);
	}

	/* No match found. */
	return -1;
}

/**
 * <p>
 * Find the first occurence of a case-insensitive substring.
 * </p>
 *
 * @param text
 *	The string to search.
 *
 * @param sub
 *	The substring to find.
 *
 * @return
 * 	Return -1 on no match, otherwise the offset of the substring.
 */
long
TextInsensitiveFind(const char *text, const char *sub)
{
	const char *t, *stop;
	size_t textLength, subLength;

	/* Impossible to match undefined strings. */
	if (text == NULL || sub == NULL)
		return -1;

	subLength = strlen(sub);
	textLength = strlen(text);

	/* Empty string matches end of string. */
	if (subLength == 0)
		return textLength;

	stop = &text[textLength - subLength + 1];

	for (t = text; t < stop; t++) {
		if (TextInsensitiveCompareN(t, sub, subLength) == 0)
			return (long) (t - text);
	}

	/* No match found. */
	return -1;
}

int
TextCountOccurences(const char *s1, const char *s2)
{
	const char *t1, *t2;
	int count = 0;

	for ( ; *s1 != '\0'; ++s1) {
		for (t1 = s1, t2 = s2; *t2 != '\0'; ++t1, ++t2) {
			if (*t1 != *t2)
				break;
		}
		count += (*t2 == '\0');
	}

	return count;
}

/***********************************************************************
 *** Testing
 ***********************************************************************/

#ifdef TEST
#include <stdio.h>

void
TestTextSplit(char *string, char *delims, int empty)
{
	int i;
	char *s;
	Vector v;

	printf("string=[%s] delims=[%s] return-empty-tokens=%d\n", string, delims, empty);
	v = TextSplit(string, delims, empty);

	if (v == NULL) {
		printf("vector is null\n");
		return;
	}

	printf("  length=%ld ", VectorLength(v));
	for (i = 0; i < VectorLength(v); i++) {
		s = VectorGet(v, i);
		printf("[%s]", s == NULL ? "" : s);
	}

	VectorDestroy(v);
	printf("\n");
}

void
TestFunction(TextLongCompareFunction func, const char *a, const char *b, long expect)
{
	long result = (*func)(a, b);

	printf("a=[%s] b=[%s] expect=%ld, result=%ld %s\n", a, b, expect, result, expect == result ? "OK" : "FAIL");
}

void
TestCompareFunction(TextCompareFunction func, const void *a, const void *b, int expect)
{
	int result;

	result = (*func)(a, b);
	if (result < 0)
		result = -1;
	else if (0 < result)
		result = 1;
	else
		result = 0;

	printf("a=[%s] b=[%s] expect=%d, result=%d %s\n", (char *) a, (char *) b, expect, result, expect == result ? "OK" : "FAIL");
}


int
main(int argc, char **argv)
{
	printf("\n--TextInsensitiveCompare--\n");
	TestCompareFunction(TextInsensitiveCompare, "a", "A", 0);
	TestCompareFunction(TextInsensitiveCompare, "b", "A", 1);
	TestCompareFunction(TextInsensitiveCompare, "a", "B", -1);

	TestCompareFunction(TextInsensitiveCompare, "ab", "AB", 0);
	TestCompareFunction(TextInsensitiveCompare, "ab", "A", 1);
	TestCompareFunction(TextInsensitiveCompare, "a", "AB", -1);

	TestCompareFunction(TextInsensitiveCompare, "ORDB.org", "ordborg", -1);
	TestCompareFunction(TextInsensitiveCompare, "ORDB.org", "ordb.org", 0);
	TestCompareFunction(TextInsensitiveCompare, "bitbucket@ORDB.org", "bitbucket@ordb.org", 0);

	printf("\n--TextInsensitiveEndsWith--\n");
	TestFunction((TextLongCompareFunction) TextInsensitiveEndsWith, "", "ordb.org", -1);
	TestFunction((TextLongCompareFunction) TextInsensitiveEndsWith, ".org", "ordb.org", -1);
	TestFunction((TextLongCompareFunction) TextInsensitiveEndsWith, "ORDB.org", "ordb.org", sizeof("ordb.org")-1);
	TestFunction((TextLongCompareFunction) TextInsensitiveEndsWith, "bitbucket@ORDB.org", "ordb.org", sizeof("ordb.org")-1);

	printf("\n--TextSplit--\n");

	/* Empty string and empty delimeters. */
	TestTextSplit("", "", 1);		/* length=1 [] */
	TestTextSplit("", "", 0);		/* length=0 */

	/* Empty string. */
	TestTextSplit("", ",", 1);		/* length=1 [] */
	TestTextSplit("", ",", 0);		/* length=0 */

	/* Empty delimiters. */
	TestTextSplit("a,b,c", "", 1);		/* length=1 [a,b,c] */
	TestTextSplit("a,b,c", "", 0);		/* length=1 [a,b,c] */

	/* Assorted combinations of empty tokens. */
	TestTextSplit(",", ",", 1);		/* length=2 [][] */
	TestTextSplit(",", ",", 0);		/* length=0 */
	TestTextSplit("a,,", ",", 1);		/* length=3 [a][][] */
	TestTextSplit("a,,", ",", 0);		/* length=1 [a] */
	TestTextSplit(",b,", ",", 1);		/* length=3 [][b][] */
	TestTextSplit(",b,", ",", 0);		/* length=1 [b] */
	TestTextSplit(",,c", ",", 1);		/* length=3 [][][c] */
	TestTextSplit(",,c", ",", 0);		/* length=1 [c] */
	TestTextSplit("a,,c", ",", 1);		/* length=3 [a][][c] */
	TestTextSplit("a,,c", ",", 0);		/* length=2 [a][c] */
	TestTextSplit("a,b,c", ",", 1);		/* length=3 [a][b][c] */
	TestTextSplit("a,b,c", ",", 0);		/* length=3 [a][b][c] */

	/* Quoting of tokens. */
	TestTextSplit("a,b\\,c", ",", 1);	/* length=2 [a][b,c] */
	TestTextSplit("a,b\\,c", ",", 0);	/* length=2 [a][b,c] */
	TestTextSplit("a,'b,c'", ",", 1);	/* length=2 [a][b,c] */
	TestTextSplit("a,'b,c'", ",", 0);	/* length=2 [a][b,c] */
	TestTextSplit("\"a,b\",c", ",", 1);	/* length=2 [a,b][c] */
	TestTextSplit("\"a,b\",c", ",", 0);	/* length=2 [a,b][c] */
	TestTextSplit("a,'b,c'd,e", ",", 1);	/* length=3 [a][b,cd][e] */
	TestTextSplit("a,'b,c'd,e", ",", 0);	/* length=3 [a][b,cd][e] */
	TestTextSplit("a,'',e", ",", 1);	/* length=3 [a][][e] */
	TestTextSplit("a,'',e", ",", 0);	/* length=3 [a][][e] */
	TestTextSplit("a,b''d,e", ",", 1);	/* length=3 [a][bd][e] */
	TestTextSplit("a,b''d,e", ",", 0);	/* length=3 [a][bd][e] */

	printf("\n--DONE--\n");

	return 0;
}
#endif
