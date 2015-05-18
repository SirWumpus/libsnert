/*
 * ulong.c
 *
 * Functions to generate printf-like number strings.
 * 
 * Copyright 1991, 2015 by Anthony Howe.  All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <ctype.h>
#include <stdlib.h>

/**
 * @param value
 *	Unsigned long value to convert to numerical string.
 *
 * @param base
 *	Number base between 2 and 36.
 *
 * @param width
 *	A number shorter than the minimum field width is padded.
 *	Positive value right justisfies and a negative value left
 *	justifies.
 *
 * @param prec
 *	A number shorter than the precision width is zero padded.
 *
 * @param pad
 *	Padding character for minimum field width.  Non-printable
 *	values are replaced by space.
 *
 * @param sign
 *	Prepend the given sign character when not zero.
 *
 * @param buffer
 *	Buffer to save formatted number string.
 *
 * @param size
 *	Size of buffer in bytes.
 *
 * @return
 *	The length of the target string, excluding the terminating null.
 *	The string will have been truncated if the length exceeds size.
 *
 * @note
 *	This function is signal safe.
 */
size_t
ulong_format(unsigned long value, int base, int width, int prec, int pad, int sign, char *buffer, size_t size)
{
	int digit;
	size_t length;
	char *x, *y, ch;
	static char digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

	if (base < 2 || 36 < base)
		return 0;
	if (buffer == NULL)
		size = 0;
	if (iscntrl(pad))
		pad = ' ';

	/* Convert number into a string.  The string will be
	 * in reverse order, 1's first then 10's, 100's, ...
	 */
	length = 0;
	x = buffer;
	do {
		digit = (int)(value % base);
		if (length++ < size)
			*x++ = digits[digit];
		value /= base;
	} while (0 < value);

	/* The minimum number of digits to appear in the number. */
	while (length < prec) {
		if (length++ < size)
			*x++ = '0';
	}

	if (sign != 0 && length++ < size)
		*x++ = sign;

	/* Positive width to right justify. */
	if (0 < width) {
		while (length < width) {
			if (length++ < size)
				*x++ = pad;
		}
	}

	/* Reverse string. */
	for (x = buffer, y = buffer+(length < size ? length : size); x < --y; x++) {
		ch = *y;
		*y = *x;
		*x = ch;
	}

	/* Negative width to left justify. */
	if (width < 0) {
		width = -width;
		for (x = buffer+length; length < width; length++) {
			if (length < size)
				*x++ = pad;
		}
	}

	if (0 < size)
		buffer[length < size ? length : size-1] = '\0';

	return length;
}

/**
 * @param value
 *	Unsigned long value to convert to numerical string.
 *
 * @param base
 *	Number base between 2 and 36.
 *
 * @param width
 *	A number shorter than the minimum field width is padded.
 *	Positive value right justisfies and a negative value left
 *	justifies.
 *
 * @param prec
 *	A number shorter than the precision width is zero padded.
 *
 * @param pad
 *	Padding character for minimum field width.  Non-printable
 *	values are replaced by space.
 *
 * @param sign
 *	Always prefix the value's sign when either '+' or '-' given.
 *	Otherwise prefix a minus sign only if value is negative.
 *
 * @param buffer
 *	Buffer to save formatted number string.
 *
 * @param size
 *	Size of buffer in bytes.
 *
 * @return
 *	The length of the target string, excluding the terminating null.
 *	The string will have been truncated if the length exceeds size.
 *
 * @note
 *	This function is signal safe.
 */
size_t
slong_format(long value, int base, int width, int prec, int pad, int sign, char *buffer, size_t size)
{
	unsigned long number;
	
	if (buffer == NULL)
		size = 0;

	/* Minus sign has special meaning, assume they want either +/- sign. */
	if (sign == '-')
		sign = '+';
	if (sign != '+')
		sign = 0;

	/* Negating a long can overflow on a two's complement machine.
	 * Instead convert to unsigned long and negate the new form,
	 * which cannot overflow.  So long as an arbitrary unsigned long
	 * can be safely converted to long and back again, this works fine.
	 */
	number = (unsigned long)value;

	if (value < 0 && base == 10) {
		number = -number;
		sign = sign == '+' || base == 10 ? '-' : 0;
	}
	
	return ulong_format(number, base, width, prec, pad, sign, buffer, size);	
}

/*
 * @param value
 *	Unsigned long value to convert to numerical string.
 *
 * @param base
 *	Number base between 2 and 36.
 *
 * @return
 *	An allocated C numerical string.  Caller responsible
 *	for free()ing.
 *
 * @note
 *	Not signal safe.
 */
char *
ulong_tostring(unsigned long value, int base)
{
	size_t size;
	char *number;
	
	size = ulong_format(value, base, 0, 0, 0, 0, NULL, 0) + 1;
	if ((number = malloc(size)) != NULL)
		(void) ulong_format(value, base, 0, 0, 0, 0, number, size);
	return number;
}

/*
 * @param value
 *	Unsigned long value to convert to numerical string.
 *
 * @param base
 *	Number base between 2 and 36.
 *
 * @return
 *	An allocated C numerical string.  Caller responsible
 *	for free()ing.
 *
 * @note
 *	Not signal safe.
 */
char *
slong_tostring(unsigned long value, int base)
{
	size_t size;
	char *number;
	
	size = slong_format(value, base, 0, 0, 0, 0, NULL, 0) + 1;
	if ((number = malloc(size)) != NULL)
		(void) slong_format(value, base, 0, 0, 0, 0, number, size);
	return number;
}
		
#ifdef TEST
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

typedef struct {
	long before;
	int radix;
	int width;
	int prec;
	int pad;
	int sign;
	char *after;
} t_table;

t_table table[] = {
	{ 0, 10, 0, 0, 0, 0, "0" },
	{ 0, 10, 5, 0, 0, 0, "    0" },
	{ 0, 10, 0, 5, 0, 0, "00000" },
	{ 0, 10, 5, 3, 0, 0, "  000" },
	{ 0, 10, 5, 5, 0, 0, "00000" },
	{ 0, 10, 5, 7, 0, 0, "0000000" },
	{ 0, 10, -5, 0, 0, 0, "0    " },
	{ 0, 10, -5, 3, 0, 0, "000  " },
	{ 0, 10, 0, 0, 0, '-', "-0" },
	{ 0, 10, 0, 0, 0, '+', "+0" },
	{ 0, 10, 0, 0, 0, '?', "?0" },

	{ 1L, 10, 0, 0, '.', 0, "1" },
	{ 1L, 10, 5, 0, '.', 0, "....1" },
	{ 1L, 10, 0, 5, '.', 0, "00001" },
	{ 1L, 10, 5, 3, '.', 0, "..001" },
	{ 1L, 10, 5, 5, '.', 0, "00001" },
	{ 1L, 10, 5, 7, '.', 0, "0000001" },
	{ 1L, 10, -5, 0, '.', 0, "1...." },
	{ 1L, 10, -5, 3, '.', 0, "001.." },
	{ 1L, 10, -5, 5, '.', 0, "00001" },
	{ 1L, 10, -5, 7, '.', 0, "0000001" },

	{ 1L, 10, 0, 0, '.', '+', "+1" },
	{ 1L, 10, 5, 0, '.', '+', "...+1" },
	{ 1L, 10, 0, 5, '.', '+', "+00001" },
	{ 1L, 10, 5, 3, '.', '+', ".+001" },
	{ 1L, 10, 5, 5, '.', '+', "+00001" },
	{ 1L, 10, 5, 7, '.', '+', "+0000001" },
	{ 1L, 10, -5, 0, '.', '+', "+1..." },
	{ 1L, 10, -5, 3, '.', '+', "+001." },
	{ 1L, 10, -5, 5, '.', '+', "+00001" },
	{ 1L, 10, -5, 7, '.', '+', "+0000001" },

	{ 1L, 10, 0, 0, '.', '?', "?1" },
	{ 1L, 10, 5, 0, '.', '?', "...?1" },
	{ 1L, 10, 0, 5, '.', '?', "?00001" },
	{ 1L, 10, 5, 3, '.', '?', ".?001" },
	{ 1L, 10, 5, 5, '.', '?', "?00001" },
	{ 1L, 10, 5, 7, '.', '?', "?0000001" },
	{ 1L, 10, -5, 0, '.', '?', "?1..." },
	{ 1L, 10, -5, 3, '.', '?', "?001." },
	{ 1L, 10, -5, 5, '.', '?', "?00001" },
	{ 1L, 10, -5, 7, '.', '?', "?0000001" },

	{ -1L, 10, 0, 0, '.', 0, "-1" },
	{ -1L, 10, 5, 0, '.', 0, "...-1" },
	{ -1L, 10, 0, 5, '.', 0, "-00001" },
	{ -1L, 10, 5, 3, '.', 0, ".-001" },
	{ -1L, 10, 5, 5, '.', 0, "-00001" },
	{ -1L, 10, 5, 7, '.', 0, "-0000001" },
	{ -1L, 10, -5, 0, '.', 0, "-1..." },
	{ -1L, 10, -5, 3, '.', 0, "-001." },
	{ -1L, 10, -5, 5, '.', 0, "-00001" },
	{ -1L, 10, -5, 7, '.', 0, "-0000001" },

	{ -1L, 10, 0, 0, '.', '?', "-1" },
	{ -1L, 10, 5, 0, '.', '?', "...-1" },
	{ -1L, 10, 0, 5, '.', '?', "-00001" },
	{ -1L, 10, 5, 3, '.', '?', ".-001" },
	{ -1L, 10, 5, 5, '.', '?', "-00001" },
	{ -1L, 10, 5, 7, '.', '?', "-0000001" },
	{ -1L, 10, -5, 0, '.', '?', "-1..." },
	{ -1L, 10, -5, 3, '.', '?', "-001." },
	{ -1L, 10, -5, 5, '.', '?', "-00001" },
	{ -1L, 10, -5, 7, '.', '?', "-0000001" },

	{ -1L, 10, 0, 0, '.', '+', "-1" },
	{ -1L, 10, 5, 0, '.', '+', "...-1" },
	{ -1L, 10, 0, 5, '.', '+', "-00001" },
	{ -1L, 10, 5, 3, '.', '+', ".-001" },
	{ -1L, 10, 5, 5, '.', '+', "-00001" },
	{ -1L, 10, 5, 7, '.', '+', "-0000001" },
	{ -1L, 10, -5, 0, '.', '+', "-1..." },
	{ -1L, 10, -5, 3, '.', '+', "-001." },
	{ -1L, 10, -5, 5, '.', '+', "-00001" },
	{ -1L, 10, -5, 7, '.', '+', "-0000001" },

	{ 12L, 10, 0, 0, 0, 0, "12" },
	{ -12L, 10, 0, 0, 0, 0, "-12" },

	{ 123L, 10, 0, 0, '.', 0, "123" },
	{ 123L, 10, 7, 0, '.', 0, "....123" },
	{ 123L, 10, 0, 7, '.', 0, "0000123" },
	{ 123L, 10, 7, 5, '.', 0, "..00123" },
	{ 123L, 10, 7, 7, '.', 0, "0000123" },
	{ 123L, 10, 7, 9, '.', 0, "000000123" },
	{ 123L, 10, -7, 0, '.', 0, "123...." },
	{ 123L, 10, -7, 5, '.', 0, "00123.." },
	{ 123L, 10, -7, 7, '.', 0, "0000123" },
	{ 123L, 10, -7, 9, '.', 0, "000000123" },

	{ -123L, 10, 0, 0, '.', 0, "-123" },
	{ -123L, 10, 7, 0, '.', 0, "...-123" },
	{ -123L, 10, 0, 7, '.', 0, "-0000123" },
	{ -123L, 10, 7, 5, '.', 0, ".-00123" },
	{ -123L, 10, 7, 7, '.', 0, "-0000123" },
	{ -123L, 10, 7, 9, '.', 0, "-000000123" },
	{ -123L, 10, -7, 0, '.', 0, "-123..." },
	{ -123L, 10, -7, 5, '.', 0, "-00123." },
	{ -123L, 10, -7, 7, '.', 0, "-0000123" },
	{ -123L, 10, -7, 9, '.', 0, "-000000123" },

	{ 1234L, 10, 0, 0, 0, 0, "1234" },
	{ -1234L, 10, 0, 0, 0, 0, "-1234" },
	{ 32767L, 10, 0, 0, 0, 0, "32767" },
	{ 32767L, 2, 0, 0, 0, 0, "111111111111111" },
	{ -32768L, 10, 0, 0, 0, 0, "-32768" },
	{ -32768L, 2, 0, 0, 0, 0, "1000000000000000" },
	{ 2147483647L, 10, 0, 0, 0, 0, "2147483647" },
	{ 2147483647L, 16, 0, 0, 0, 0, "7FFFFFFF" },
	{ -2147483648L, 10, 0, 0, 0, 0, "-2147483648" },
	{ -2147483648L, 16, 0, 0, 0, 0, "80000000" },
	{ 2147483647L, 16, 0, 0, 0, '+', "+7FFFFFFF" },
	{ -2147483647L, 16, 0, 0, 0, '+', "-7FFFFFFF" },
	{ 0 }
};

int
test(t_table *ptr)
{
	size_t length;
	char number[65];

	length = slong_format(
		ptr->before, ptr->radix, ptr->width, ptr->prec,
		ptr->pad, ptr->sign, number, sizeof (number)
	);

	printf(
		"%ld r=%d w=%d p=%d, pad=%X, s=%X, expect=\"%s\" out=%zu:\"%s\" ... ",
		ptr->before, ptr->radix, ptr->width, ptr->prec,
		ptr->pad, ptr->sign, ptr->after, length, number
	);

	if (sizeof (number) <= length || strcmp(number, ptr->after) != 0) {
		printf("FAIL\n");
		return EXIT_FAILURE;
	}

	printf("-OK-\n");
	return EXIT_SUCCESS;
}

static char usage[] =
"usage: %s [-ltu][-B ibase][-b obase][-P pad][-p prec][-w width] number ...\n"
"\n"
"-B ibase\tinput base\n"
"-b obase\toutput base\n"
"-P pad\t\tpad character; default space\n"
"-p prec\t\tmininum output precision, pad with leading zeros\n"
"-w width\tmininum output width with pad character; -width left justify\n"
"-l\t\tshow string length\n"
"-t\t\trun test suite\n"
"-u\t\toutput is unsigned\n"
;

int
main(int argc, char **argv)
{
	t_table *ptr;
	char number[80];
	size_t length;
	size_t (*fmt_fn)();
	unsigned long ulong;
	int ch, argi, ibase, obase, width, prec, pad, show_length, ex_code;

	ibase = 10;
	obase = 10;
	width = 0;
	prec = 0;
	pad = 0;
	show_length = 0;
	fmt_fn = slong_format;

	while ((ch = getopt(argc, argv, "tluB:b:w:p:P:")) != -1) {
		switch (ch) {
		case 'B':
			ibase = (int) strtol(optarg, NULL, 10);
			break;
		case 'b':
			obase = (int) strtol(optarg, NULL, 10);
			break;
		case 'p':
			prec = (int) strtol(optarg, NULL, 10);
			break;
		case 'P':
			pad = *optarg;
			break;
		case 'w':
			width = (int) strtol(optarg, NULL, 10);
			break;
		case 'l':
			show_length = 1;
			break;
		case 'u':
			fmt_fn = ulong_format;
			break;
		case 't':			
			ex_code = EXIT_SUCCESS;
			(void) setvbuf(stdout, NULL, _IOLBF, 0);
			for (ptr = table; ptr->after != NULL; ++ptr) {
				if (test(ptr)) 
					ex_code = EXIT_FAILURE;
			}
			return ex_code;
		default:
			optind = argc;
		}
	}
	
	if (argc <= optind) {
		(void) fprintf(stderr, usage, argv[0]);
		return EXIT_FAILURE;
	}
	
	for (argi = optind; argi < argc; argi++) {
		/* strtoul() handles +/- signs too, negating the value
		 * for a minus.  So -0xA == 0xFFFFFFFFFFFFFFF6 == -10.
		 */
		ulong = strtoul(argv[argi], NULL, ibase);
		length = (*fmt_fn)(ulong, obase, width, prec, pad, 0, number, sizeof (number));
		
		if (show_length)		
			(void) printf("%zu:", length);
		(void) printf("%s\n", number);				
	}
	
	return EXIT_SUCCESS;
}
#endif