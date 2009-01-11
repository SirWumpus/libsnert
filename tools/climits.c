/*
 * climits.c
 *
 * Copyright 1994, 2003 by Anthony Howe.  All rights reserved.
 */

#include <stdio.h>

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
#ifdef NOT_USED
static char _longlong[] = "#define %s\t%lld\n";
static char _ulonglong[] = "#define %s\t%llu\n";
#endif

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

	if (ptr != NULL)
		printf(_int, ptr->symbol, count);

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

	if (ptr != NULL)
		printf(_int, ptr->symbol, value);

	return value;
}

static int
min_char(ptr)
t_limits *ptr;
{
	char c;

	if (char_is_unsigned(NULL))
		c = 0;
	else
		c = (char) (1 << (nbits - 1));
	printf(_int, ptr->symbol, (int) c);

	return 1;
}

static int
minimum(ptr)
t_limits *ptr;
{
	int c;
	long l;
	short s;

	switch (ptr->size) {
	case sizeof (char):
		c = (char) (1 << (nbits - 1));
		if (0 < c)
			c = 1 - c;
		printf(_int, ptr->symbol, c);
		break;
	case sizeof (short):
		s = (short) (1 << (sizeof (short) * nbits -1));
		printf(_int, ptr->symbol, (int) s);
		break;
	case sizeof (long):
		l = (long) (1L << (sizeof (long) * nbits -1));
		printf(_long, ptr->symbol, l);
		break;
	}

	return 1;
}

static int
max_char(ptr)
t_limits *ptr;
{
	char c;

	if (char_is_unsigned(NULL))
		c = ~(char) 0;
	else
		c = (char) ~(1 << (nbits - 1));
	printf(_int, ptr->symbol, (unsigned) c);

	return 1;
}

static int
maximum(ptr)
t_limits *ptr;
{
	int c;
	long l;
	short s;

	switch (ptr->size) {
	case sizeof (char):
		c = (unsigned char) ~((char) 1 << (nbits - 1));
		printf(_int, ptr->symbol, c);
		break;
	case sizeof (short):
		s = (unsigned short) ~((short) 1 << (sizeof (short) * nbits -1));
		printf(_int, ptr->symbol, (int) s);
		break;
	case sizeof (long):
		l = (unsigned long) ~((long) 1 << (sizeof (long) * nbits -1));
		printf(_long, ptr->symbol, l);
		break;
	}

	return 1;
}

static int
absolute(ptr)
t_limits *ptr;
{
	unsigned char c;
	unsigned long l;
	unsigned short s;

	switch (ptr->size) {
	case sizeof (char):
		c = ~(unsigned char) 0;
		printf(_uint, ptr->symbol, (unsigned) c);
		break;
	case sizeof (short):
		s = ~(unsigned short) 0;
		printf(_uint, ptr->symbol, (unsigned) s);
		break;
	case sizeof (long):
		l = ~(unsigned long) 0;
		printf(_ulong, ptr->symbol, l);
		break;
	}

	return 1;
}

static int
int_is_short(ptr)
t_limits *ptr;
{
	printf(_int, ptr->symbol, sizeof (int) == sizeof (short));

	return 1;
}

static int
ones_complement(ptr)
t_limits *ptr;
{
	printf(_int, ptr->symbol, !(-1 & 1));

	return !(-1 & 1);
}

static int
sizeof_type(ptr)
t_limits *ptr;
{
	printf(_int, ptr->symbol, ptr->size);

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
	{ "MAX_UINT", sizeof (unsigned), absolute },
	{ "MAX_LONG", sizeof (long), maximum },
	{ "MIN_LONG", sizeof (long), minimum },
	{ "MAX_ULONG", sizeof (unsigned long), absolute },
	{ "SIZEOF_CHAR", sizeof (char), sizeof_type },
	{ "SIZEOF_SHORT", sizeof (short), sizeof_type },
	{ "SIZEOF_INT", sizeof (int), sizeof_type },
	{ "SIZEOF_LONG", sizeof (long), sizeof_type },
	{ (char *) 0, 0, 0 }
};

int
main(argc, argv)
int argc;
char **argv;
{
	t_limits *ptr;

	nbits = bits(NULL);

	for (ptr = limits; ptr->symbol != (char *) 0; ++ptr)
		(void) (*ptr->func)(ptr);

	return 0;
}



