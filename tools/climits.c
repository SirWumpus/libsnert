/*
 * climits.c
 *
 * Copyright 1994, 2003 by Anthony Howe.  All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>

#undef _
#if defined(__STDC__) || defined(__BORLANDC__)
# define _(x)			x
#else
# define _(x)			()
#endif

typedef struct {
	char *symbol;
	int size;
	int (*func)();
} t_limits;

static int nbits;

static char _int[] = "#define %s\t%d\n";
static char _uint[] = "#define %s\t%u\n";
static char _long[] = "#define %s\t%ld\n";
static char _ulong[] = "#define %s\t%lu\n";
static char _longlong[] = "#define %s\t%lld\n";
static char _ulonglong[] = "#define %s\t%llu\n";

static int bits _((t_limits *));
static int char_is_unsigned _((t_limits *));
static int min_char _((t_limits *));
static int minimum _((t_limits *));
static int max_char _((t_limits *));
static int maximum _((t_limits *));
static int absolute _((t_limits *));
static int int_is_short _((t_limits *));
static int ones_complement _((t_limits *));

static int
bits(ptr)
t_limits *ptr;
{
	int count;
	unsigned char byte;

	byte = 1;
	count = 0;

	do {
		++count;
		byte <<= 1;
	} while (byte != 0);

	if (ptr != NULL) {
		(void) printf(_int, ptr->symbol, count);
	}

	return count;
}

static int
char_is_unsigned(ptr)
t_limits *ptr;
{
	char min;
	int value;

	min = (char) (1 << (nbits - 1));
	value = 0 < min;

	if (ptr != NULL) {
		(void) printf(_int, ptr->symbol, value);
	}

	return value;
}

static int
min_char(ptr)
t_limits *ptr;
{
	char c;

	if (char_is_unsigned(NULL)) {
		c = 0;
	} else {
		c = (char) (1 << (nbits - 1));
	}
	(void) printf(_int, ptr->symbol, (int) c);

	return 1;
}

static int
minimum(ptr)
t_limits *ptr;
{
	int c;
	long l;
	short s;
	long long ll;

	if (ptr->size == sizeof (char)) {
		c = (char) (1 << (nbits - 1));
		if (0 < c) {
			c = 1 - c;
		}
		(void) printf(_int, ptr->symbol, c);
	} else if (ptr->size == sizeof (short)) {
		s = (short) (1 << (sizeof (short) * nbits -1));
		(void) printf(_int, ptr->symbol, (int) s);
	} else if (ptr->size == sizeof (int)) {
		l = (int) (1L << (sizeof (int) * nbits -1));
		(void) printf(_int, ptr->symbol, l);
	} else if (ptr->size == sizeof (long)) {
		l = (long) (1L << (sizeof (long) * nbits -1));
		(void) printf(_long, ptr->symbol, l);
	} else if (ptr->size == sizeof (long long)) {
		ll = (long long) (1LL << (sizeof (long long) * nbits -1));
		(void) printf(_longlong, ptr->symbol, ll);
	}

	return 1;
}

static int
max_char(ptr)
t_limits *ptr;
{
	char c;

	if (char_is_unsigned(NULL)) {
		c = ~(char) 0;
	} else {
		c = (char) ~(1 << (nbits - 1));
	}
	(void) printf(_int, ptr->symbol, (unsigned) c);

	return 1;
}

static int
maximum(ptr)
t_limits *ptr;
{
	int c;
	long l;
	short s;
	long long ll;

	if (ptr->size == sizeof (char)) {
		c = (unsigned char) ~((char) 1 << (nbits - 1));
		(void) printf(_int, ptr->symbol, c);
	} else if (ptr->size == sizeof (short)) {
		s = (unsigned short) ~((short) 1 << (sizeof (short) * nbits -1));
		(void) printf(_int, ptr->symbol, (int) s);
	} else if (ptr->size == sizeof (int)) {
		l = (unsigned int) ~((int) 1 << (sizeof (int) * nbits -1));
		(void) printf(_int, ptr->symbol, l);
	} else if (ptr->size == sizeof (long)) {
		l = (unsigned long) ~((long) 1 << (sizeof (long) * nbits -1));
		(void) printf(_long, ptr->symbol, l);
	} else if (ptr->size == sizeof (long long)) {
		ll = (unsigned long long) ~((long long) 1 << (sizeof (long long) * nbits -1));
		(void) printf(_longlong, ptr->symbol, ll);
	}

	return 1;
}

static int
absolute(ptr)
t_limits *ptr;
{
	unsigned char c;
	unsigned short s;
	unsigned int i;
	unsigned long l;
	unsigned long long ll;

	if (ptr->size == sizeof (char)) {
		c = ~(unsigned char) 0;
		(void) printf(_uint, ptr->symbol, (unsigned) c);
	} else if (ptr->size == sizeof (short)) {
		s = ~(unsigned short) 0;
		(void) printf(_uint, ptr->symbol, (unsigned) s);
	} else if (ptr->size == sizeof (int)) {
		i = ~(unsigned int) 0;
		(void) printf(_uint, ptr->symbol, (unsigned) i);
	} else if (ptr->size == sizeof (long)) {
		l = ~(unsigned long) 0;
		(void) printf(_ulong, ptr->symbol, l);
	} else if (ptr->size == sizeof (long long)) {
		ll = ~(unsigned long long) 0;
		(void) printf(_ulonglong, ptr->symbol, ll);
	}

	return 1;
}

static int
int_is_short(ptr)
t_limits *ptr;
{
	(void) printf(_int, ptr->symbol, sizeof (int) == sizeof (short));

	return 1;
}

static int
ones_complement(ptr)
t_limits *ptr;
{
	(void) printf(_int, ptr->symbol, !(-1 & 1));

	return !(-1 & 1);
}

static int
sizeof_type(ptr)
t_limits *ptr;
{
	(void) printf(_int, ptr->symbol, ptr->size);

	return 1;
}

t_limits limits[] = {
	{ "ONES_COMPLEMENT", 0, ones_complement },
	{ "CHAR_IS_UNSIGNED", 0, char_is_unsigned },
	{ "INT_IS_SHORT", 0, int_is_short },
	{ "BITS_PER_BYTE", 0, bits },
	{ "MAX_SCHAR", sizeof (signed char), maximum },
	{ "MIN_SCHAR", sizeof (signed char), minimum },
	{ "MAX_CHAR", sizeof (char), max_char },
	{ "MIN_CHAR", sizeof (char), min_char },
	{ "MAX_UCHAR", sizeof (unsigned char), absolute },
	{ "MAX_SHORT", sizeof (short), maximum },
	{ "MIN_SHORT", sizeof (short), minimum },
	{ "MAX_USHORT", sizeof (unsigned short), absolute },
	{ "MAX_INT\t", sizeof (int), maximum },
	{ "MIN_INT\t", sizeof (int), minimum },
	{ "MAX_UINT", sizeof (unsigned int), absolute },
	{ "MAX_LONG", sizeof (long), maximum },
	{ "MIN_LONG", sizeof (long), minimum },
	{ "MAX_ULONG", sizeof (unsigned long), absolute },
	{ "MAX_LLONG", sizeof (long long), maximum },
	{ "MIN_LLONG", sizeof (long long), minimum },
	{ "MAX_ULLONG", sizeof (unsigned long long), absolute },
	{ "SIZEOF_CHAR", sizeof (char), sizeof_type },
	{ "SIZEOF_SHORT", sizeof (short), sizeof_type },
	{ "SIZEOF_INT", sizeof (int), sizeof_type },
	{ "SIZEOF_VOID_PTR", sizeof (void *), sizeof_type },
	{ "SIZEOF_INTPTR_T", sizeof (intptr_t), sizeof_type },
	{ "SIZEOF_LONG", sizeof (long), sizeof_type },
	{ "SIZEOF_LONG_LONG", sizeof (long long), sizeof_type },
	{ "SIZEOF_FLOAT", sizeof (float), sizeof_type },
	{ "SIZEOF_DOUBLE", sizeof (double), sizeof_type },
	{ "SIZEOF_LONG_DOUBLE", sizeof (long double), sizeof_type },
	{ "SIZEOF_OFF_T", sizeof (off_t), sizeof_type },
	{ "SIZEOF_SIZE_T", sizeof (size_t), sizeof_type },
	{ "SIZEOF_PTRDIFF_T", sizeof (ptrdiff_t), sizeof_type },
	{ (char *) 0, 0, 0 }
};

int
main(argc, argv)
int argc;
char **argv;
{
	t_limits *ptr;

	nbits = bits(NULL);

	for (ptr = limits; ptr->symbol != (char *) 0; ++ptr) {
		(void) (*ptr->func)(ptr);
	}

	return 0;
}



