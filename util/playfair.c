/*
 * playfair.c
 *
 * http://en.wikipedia.org/wiki/Playfair_cipher
 *
 * Copyright 2010 by Anthony Howe. All rights reserved.
 */

#define NDEBUG
#undef ALPHABET36_SIMPLE

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <com/snert/lib/util/playfair.h>

#ifndef PLAYFAIR_UNCOMMON
#define PLAYFAIR_UNCOMMON	'X'
#endif

#if !defined(ALPHABET25)
# define ALPHABET25		"ABCDEFGHIKLMNOPQRSTUVWXYZ"
#endif

#if !defined(ALPHABET36)
# if defined(ALPHABET36_SIMPLE)
#  define ALPHABET36		"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
# elif defined(ALPHABET36_BETTER)
#  define ALPHABET36		"A1B2C3D4E5F6G7H8I9J0KLMNOPQRSTUVWXYZ"
# else /* defined(ALPHABET36_SPREAD) */
#  define ALPHABET36		"A1BC2DEF3GHI4JKL5MN6OPQ7RST8UVW9XY0Z"
# endif
#endif

typedef char *(*playfair_fn)(const playfair_table, const char *);

void
playfair_print(FILE *fp, const char *message)
{
	for ( ; *message != '\0'; message += 2)
		fprintf(fp, "%.2s ", message);
	fputc('\n', fp);
}

void
playfair_dump(FILE *fp, const playfair_table key_table)
{
	int row, col, order;

	order = strlen(key_table) == 25 ? 5 : 6;

	for (row = 0; row < order; row++) {
		for (col = 0; col < order; col++) {
			fprintf(fp, "%c ", key_table[row * order + col]);
		}
		fputc('\n', fp);
	}
}

int
playfair_build(const char *alphabet, const char *key, playfair_table key_table)
{
	int i, ch, map[2];
	size_t set_length;
	char set[sizeof (ALPHABET36)], *member;

	if (key == NULL)
		key = "";
	if (alphabet == NULL)
		return 1;

	set_length = strlen(alphabet);
	if (set_length != 25 && set_length != 36)
		return 1;

	/* Copy the alphabet. */
	for (i = 0; i < set_length; i++) {
		if (alphabet[i] == '\0')
			return 1;
		ch = toupper(alphabet[i]);
		if (strchr(set, ch) != NULL)
			return 2;
		set[i] = ch;
		set[i+1] = '\0';
	}
	if (alphabet[i] != '\0')
		return 1;

	if (set_length == 25) {
		if (strchr(set, 'I') == NULL && strchr(set, 'J') != NULL) {
			map[0] = 'I';
			map[1] = 'J';
		} else if (strchr(set, 'I') != NULL && strchr(set, 'J') == NULL) {
			map[0] = 'J';
			map[1] = 'I';
		}
	}

	/* Copy the key into key_table, removing key characters
	 * from the set of unused alphabet characters.
	 */
	for (i = 0; i < set_length && key[i] != '\0'; i++) {
		ch = toupper(key[i]);

		if (set_length == 25 && ch == map[0])
			ch = map[1];

		if ((member = strchr(set, ch)) != NULL) {
			*key_table++ = ch;
			*member = ' ';
		}
	}

	/* Copy remaining unused alphabet to key_table. */
	for (i = 0; i < set_length; i++) {
		if (set[i] != ' ')
			*key_table++ = set[i];
	}
	*key_table = '\0';

	return 0;
}

char *
playfair_encode(const playfair_table key_table, const char *message)
{
	size_t length;
	char *out, *op;
	div_t pos1, pos2;
	int m1, m2, span1, span2, order, map[2];

	if (key_table == NULL || message == NULL)
		return NULL;

	order = strlen(key_table) == 25 ? 5 : 6;

	if (order == 5) {
		if (strchr(key_table, 'I') == NULL && strchr(key_table, 'J') != NULL) {
			map[0] = 'I';
			map[1] = 'J';
		} else if (strchr(key_table, 'I') != NULL && strchr(key_table, 'J') == NULL) {
			map[0] = 'J';
			map[1] = 'I';
		}
	}

	/* Allow enough space for a run of double letter "EEFFGG",
	 * which converts to the digraph string "EX EF FG GX" before
	 * being transformed.
	 */
	length = strlen(message) * 2;
	if ((out = malloc(length+1)) == NULL)
		return NULL;

	op = out;
	while (*message != '\0') {
		m1 = *message++;
		m1 = toupper(m1);

		/* Ignore punctuation and spacing. */
		if (order == 5) {
			if (!isalpha(m1))
				continue;
		} else {
			if (!isalnum(m1))
				continue;
		}

		if (order == 5 && m1 == map[0])
			m1 = map[1];

		/* Ignore punctuation and spacing. */
		if (order == 5) {
			while (*message != '\0' && !isalpha(*message))
				message++;
		} else {
			while (*message != '\0' && !isalnum(*message))
				message++;
		}

		m2 = *message++;
		m2 = toupper(m2);
		if (order == 5 && m2 == map[0])
			m2 = map[1];

		/* Handle character pairs or an odd length message. */
		if (m1 == m2 || m2 == '\0') {
			m2 = PLAYFAIR_UNCOMMON;
			message--;
		}

		span1 = strchr(key_table, m1) - key_table;
		span2 = strchr(key_table, m2) - key_table;
		pos1 = div(span1, order);
		pos2 = div(span2, order);

#ifndef NDEBUG
		printf(
			"%c%c %d,%d %d,%d\n", toupper(m1), toupper(m2),
			pos1.quot, pos1.rem, pos2.quot, pos2.rem
		);
#endif
		/* Same row? */
		if (pos1.quot == pos2.quot) {
			*op++ = key_table[pos1.quot * order + (pos1.rem+1) % order];
			*op++ = key_table[pos2.quot * order + (pos2.rem+1) % order];
		}

		/* Same column? */
		else if (pos1.rem == pos2.rem) {
			*op++ = key_table[(pos1.quot+1) % order * order + pos1.rem];
			*op++ = key_table[(pos2.quot+1) % order * order + pos2.rem];
		}

		/* Opposing corners. */
		else {
			*op++ = key_table[pos1.quot * order + pos2.rem];
			*op++ = key_table[pos2.quot * order + pos1.rem];
		}
	}
	*op = '\0';

#ifndef NDEBUG
	fputc('\n', stdout);
#endif
	return out;
}

char *
playfair_decode(const playfair_table key_table, const char *message)
{
	size_t length;
	char *out, *op;
	div_t pos1, pos2;
	int m1, m2, span1, span2, order;

	if (key_table == NULL || message == NULL)
		return NULL;

	order = strlen(key_table) == 25 ? 5 : 6;

	length = strlen(message);
	if ((out = malloc(length+1)) == NULL)
		return NULL;

	op = out;
	while (*message != '\0') {
		m1 = *message++;
		m1 = toupper(m1);

		/* Ignore punctuation and spacing. */
		if (order == 5) {
			if (!isalpha(m1))
				continue;
		} else {
			if (!isalnum(m1))
				continue;
		}

		/* Ignore punctuation and spacing. */
		if (order == 5) {
			while (*message != '\0' && !isalpha(*message))
				message++;
		} else {
			while (*message != '\0' && !isalnum(*message))
				message++;
		}

		m2 = *message++;
		m2 = toupper(m2);

		span1 = strchr(key_table, m1) - key_table;
		span2 = strchr(key_table, m2) - key_table;

		pos1 = div(span1, order);
		pos2 = div(span2, order);

#ifndef NDEBUG
		printf(
			"%c%c %d,%d %d,%d\n", toupper(m1), toupper(m2),
			pos1.quot, pos1.rem, pos2.quot, pos2.rem
		);
#endif
		/* Same row? */
		if (pos1.quot == pos2.quot) {
			*op++ = key_table[pos1.quot * order + (pos1.rem+order-1) % order];
			*op++ = key_table[pos2.quot * order + (pos2.rem+order-1) % order];
		}

		/* Same column? */
		else if (pos1.rem == pos2.rem) {
			*op++ = key_table[(pos1.quot+order-1) % order * order + pos1.rem];
			*op++ = key_table[(pos2.quot+order-1) % order * order + pos2.rem];
		}

		/* Opposing corners. */
		else {
			*op++ = key_table[pos1.quot * order + pos2.rem];
			*op++ = key_table[pos2.quot * order + pos1.rem];
		}
	}
	*op = '\0';

#ifndef NDEBUG
	fputc('\n', stdout);
#endif
	return out;
}

#ifdef TEST
static char usage[] =
"usage: playfair [-56dk][-a set] key message\n"
"\n"
"-5\t\tclassic playfair 25 character alphabet, where I=J (default)\n"
"-6\t\tmodified playfair 36 character alphabet and digits\n"
"-a set\t\tset alphabet order\n"
"-d\t\tdecode message\n"
"-k\t\tdump key table\n"
"\n"
"Copyright 2010 by Anthony Howe.  All rights reserved.\n"
;

int
main(int argc, char **argv)
{
	playfair_fn fn;
	char *out, *alphabet;
	playfair_table key_table;
	int argi, show_key_table;

	show_key_table = 0;
	fn = playfair_encode;
	alphabet = ALPHABET25;

	for (argi = 1; argi < argc; argi++) {
		if (argv[argi][0] != '-' || (argv[argi][1] == '-' && argv[argi][2] == '\0'))
			break;

		switch (argv[argi][1]) {
		case '5':
			alphabet = ALPHABET25;
			break;
		case '6':
			alphabet = ALPHABET36;
			break;
		case 'd':
			fn = playfair_decode;
			break;
		case 'k':
			show_key_table = 1;
			break;
		case 'a':
			alphabet = argv[argi][2] == '\0' ? argv[++argi] : &argv[argi][2];
			break;
		default:
			fprintf(stderr, "invalid option -%c\n%s", argv[argi][1], usage);
			return EXIT_FAILURE;
		}
	}

	if (argc < argi + 2) {
		fprintf(stderr, "missing key and/or message\n%s", usage);
		return EXIT_FAILURE;
	}

	if (playfair_build(alphabet, argv[argi], key_table)) {
		fprintf(stderr, "alphabet invalid\n");
		return EXIT_FAILURE;
	}

	if ((out = (*fn)(key_table, argv[argi+1])) == NULL) {
		fprintf(stderr, "out of memory\n");
		return EXIT_FAILURE;
	}

	if (show_key_table) {
		playfair_dump(stdout, key_table);
		fputc('\n', stdout);
	}

	playfair_print(stdout, out);
	free(out);

	return EXIT_SUCCESS;
}

#endif /* TEST */
