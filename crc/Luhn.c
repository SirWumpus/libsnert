/*
 * LUHN.c
 *
 * Copyright 2001, 2013 by Anthony Howe.  All rights reserved.
 */

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <com/snert/lib/version.h>
#include <com/snert/lib/crc/Luhn.h>

#ifdef DEBUG_MALLOC
# include <com/snert/lib/util/DebugMalloc.h>
#endif


/*@+boolint +charindex -predboolint -unsignedcompare @*/

/**
 * <p>
 * Routine that computes the LUHN sum.
 * </p>
 * <pre>
 *
 * The LUHN Algorithm ("mod 10 Double-Add-Double"):
 *
 * 1.   Position from right           8   7   6   5   4   3   2   1
 *                                   ------------------------------
 *      Number string                 4   8   9   5   1   3   1   3
 *      x2 even positions             2   1   2   1   2   1   2   1
 *                                   ------------------------------
 *      Add odd positions &           8  +8 +1+8 +5  +2  +3  +2  +3
 *      add digits of doubled
 *      positions
 *
 *      Sum                          40
 *
 *      Number is valid if sum is a multiple of 10.
 *
 *
 * 2.   Alpha-numeric ISIN        U     S  3  8  3  8  8  3  1  0  5  1
 *                            -----------------------------------------
 *      base 36 digits           30    28  3  8  3  8  8  3  1  0  5  1
 *                            -----------------------------------------
 *      consider digit string  3  0  2  8  3  8  3  8  8  3  1  0  5  1
 *      & x2 even digits from  2  1  2  1  2  1  2  1  2  1  2  1  2  1
 *                            -----------------------------------------
 *                             6  0  4  8  6  8  6  8 16  3  2  0 10  1
 *                            -----------------------------------------
 *      sum the digits of      6 +0 +4 +8 +6 +8 +6 +8+1+6+3 +2 +0+1+0+1
 *
 *      Sum                   60
 *
 *      Number is valid if sum is a multiple of 10.
 * </pre>
 *
 * @param s
 * 	A string consisting of alpha-numeric characters.
 *
 * @return
 *	The LUHN sum for the string or -1 on error.
 */
static long
sum(const char *s, size_t length)
{
	char *p;
	size_t i;
	int even = 0;
	long total = 0;
	static unsigned base36[256];
	static char alnum[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

	if (length < 1)
		return -1;

	if (base36['Z'] != 35) {
		for (i = 0, p = alnum; *p != '\0'; p++, i++) {
			base36[(int) *p] = (unsigned) i;
			base36[tolower(*p)] = (unsigned) i;
		}
	}

	/* Working right to left... */
	for (i = length; 0 < i--; ) {
		/* Convert base36 digit to a base10 value. */
		unsigned digit36 = base36[(int) s[i]];

		/* For each base10 digit of a base36 digit... */
		do {
			unsigned decimal = digit36 % 10;

			if (even) {
				/* Multiply even digits by 2. */
				decimal <<= 1;

				/* Sum digits of product. */
				if (9 < decimal) decimal -= 9;
			}

			/* Add digits of product to total. */
			total += decimal;

			/* Next digit. */
			digit36 /= 10;

			/* Toggle from digit to digit. */
			even = !even;
		} while (0 < digit36);
	}

	return total;
}

/**
 * <p>
 * Check if a number is valid.
 * </p>
 *
 * @param s
 *	Base36 alpha-numerical string.
 *
 * @return
 *	True if the string of base36 digits is valid.
 */
int
LuhnIsValid(const char * s)
{
	long total;
	size_t length;

	if (s == NULL)
		return 0;

	length = strlen(s);

	if (length < 2)
		return 0;

	total = sum(s, length);

	return total < 0 ? 0 : (total % 10) == 0;
}

/**
 * <p>
 * Generate the LUHN checksum digit (mod 10) used for credit-cards
 * and International Securities Identification Number (ISIN).
 * </p>
 *
 * @param s
 *	Base36 alpha-numerical string.
 *
 * @return
 *	The check digit for the string or -1 if the string is empty
 *	or contains characters other than alpha-numeric.
 */
int
LuhnGenerate(const char *s)
{
	char *copy;
	long tally;
	size_t length;

	if (s == NULL)
		return -1;

	length = strlen(s);

	if ((copy = malloc(length + 2)) == NULL)
		return -1;

	strncpy(copy, s, length);
	copy[length+1] = '\0';
	copy[length] = '0';

	tally = sum(copy, length+1);

	free(copy);

	return tally < 0 ? -1 : (10 - (int)(tally % 10)) % 10;
}

#ifdef TEST

#include <stdio.h>

/**
 * <p>
 * Test program where the command-line arguments are strings. Each
 * line of output prints the string, whether is has a valid checksum,
 * and a generated checksum.
 * </p>
 */
int
main(int argc, char **argv)
{
	if (argc < 2) {
		(void) fprintf(stderr, "usage: Luhn number ...\n");
		return 2;
	}

	for (argv++ ; *argv != (char *) 0; argv++)
		(void) printf("%s %d %d\n", *argv, LuhnIsValid(*argv), LuhnGenerate(*argv));

	return 0;
}

#endif
