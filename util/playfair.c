/*
 * playfair.c
 *
 * http://en.wikipedia.org/wiki/Playfair_cipher
 *
 * Copyright 2010 by Anthony Howe. All rights reserved.
 */

#define NDEBUG
#undef ALPHABET36_SIMPLE
#undef ORDER8_ALTERNATE_CASE

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <com/snert/lib/util/playfair.h>

#ifndef PLAYFAIR_UNCOMMON
#define PLAYFAIR_UNCOMMON	'Q'		/* 'X' or 'Q' */
#endif

/* Classic Playfair alphabet where I and J are equivalent. */
#if !defined(ALPHABET25)
# define ALPHABET25		"ABCDEFGHIKLMNOPQRSTUVWXYZ"
#endif

/* Various alpha-numeric alphabets using different orderings. */
#if !defined(ALPHABET36)
# if defined(ALPHABET36_SIMPLE)
#  define ALPHABET36		"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
# else /* defined(ALPHABET36_SPREAD) */
#  define ALPHABET36		"A1BC2DEF3GHI4JKL5MN6OPQ7RST8UVW9XY0Z"
# endif
#endif

/* Playfair 64 using the Base64 character set. Note the Base64
 * padding character, equal-sign (=), is not used. This set is
 * fine for electronic communications, but the mixed case alphabet
 * not ideal for audio radio (think classic Number Stations).
 */
#if !defined(ALPHABET64)
# define ALPHABET64		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
#endif

typedef char *(*playfair_fn)(Playfair *, const char *);

void
playfair_print(FILE *fp, const char *message)
{
	for ( ; *message != '\0'; message += 2)
		fprintf(fp, "%.2s ", message);
	fputc('\n', fp);
}

void
playfair_dump(FILE *fp, Playfair *pf)
{
	int row, col, order;

	switch (strlen(pf->table)) {
	case 25: order = 5; break;
	case 36: order = 6; break;
	case 64: order = 8; break;
	default: order = 0; break;
	}

	for (row = 0; row < order; row++) {
		for (col = 0; col < order; col++) {
			fprintf(fp, "%c ", pf->table[row * order + col]);
		}
		fputc('\n', fp);
	}
}

int
playfair_init(Playfair *pf, const char *alphabet, const char *key)
{
	int ch, map[2];
	size_t i, set_length;
	char set[sizeof (ALPHABET64)], *member, *key_table;

	if (key == NULL)
		key = "";
	if (alphabet == NULL)
		return 1;

	set_length = strlen(alphabet);
	if (set_length != 25 && set_length != 36 && set_length != 64)
		return 1;

	/* Copy the alphabet. */
	for (i = 0; i < set_length; i++) {
		ch = alphabet[i];
		if (ch == '\0')
			return 1;
		if (set_length != 64)
			ch = toupper(ch);
		if (strchr(set, ch) != NULL)
			return 2;
		set[i] = ch;
		set[i+1] = '\0';
	}
	if (alphabet[i] != '\0')
		return 1;

	map[0] = map[1] = 0;
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
	key_table = pf->table;
	for (i = 0; i < set_length && *key != '\0'; key++) {
		ch = *key;
		if (set_length != 64)
			ch = toupper(ch);

		if (set_length == 25 && ch == map[0])
			ch = map[1];

		if ((member = strchr(set, ch)) != NULL) {
			*key_table++ = ch;
			*member = 0x7F;
		}
	}

	/* Copy remaining unused alphabet to key_table. */
	for (i = 0; i < set_length; i++) {
		if (set[i] != 0x7F)
			*key_table++ = set[i];
	}
	*key_table = '\0';

	if (pf->opt_show_table) {
		playfair_dump(stdout, pf);
		fputc('\n', stdout);
	}

	return 0;
}

static int
playfair_is_alphabet(int order, int ch)
{
	switch (order) {
	case 8:
		if (ch == '+' || ch == '/')
			return 1;
		/*@fallthrough@*/
	case 6:
		if (isdigit(ch))
			return 1;
		/*@fallthrough@*/
	case 5:
		if (isalpha(ch))
			return 1;
	}

	return 0;
}

char *
playfair_encode(Playfair *pf, const char *message)
{
	size_t length;
	char *out, *op;
	div_t pos1, pos2;
	int m1, m2, span1, span2, order, map[2];
#ifdef ORDER8_ALTERNATE_CASE
	int is_odd;
#endif

	if (pf == NULL || message == NULL)
		return NULL;

	map[0] = map[1] = 0;

	switch (strlen(pf->table)) {
	case 25: order = 5; break;
	case 36: order = 6; break;
	case 64: order = 8; break;
	default: order = 0; break;
	}

	if (order == 5) {
		if (strchr(pf->table, 'I') == NULL && strchr(pf->table, 'J') != NULL) {
			map[0] = 'I';
			map[1] = 'J';
		} else if (strchr(pf->table, 'I') != NULL && strchr(pf->table, 'J') == NULL) {
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
#ifdef ORDER8_ALTERNATE_CASE
	is_odd = 0;
#endif
	while (*message != '\0') {
		m1 = *message++;
		if (order != 8)
			m1 = toupper(m1);
#ifdef ORDER8_ALTERNATE_CASE
		else if (is_odd)
			m1 = tolower(m1);
		else
			m1 = toupper(m1);
		is_odd = !is_odd;
#endif

		/* Ignore punctuation and spacing. */
		if (!playfair_is_alphabet(order, m1))
			continue;

		if (order == 5 && m1 == map[0])
			m1 = map[1];

		/* Ignore punctuation and spacing. */
		for ( ; *message != '\0'; message++) {
			if (playfair_is_alphabet(order, *message))
				break;
		}

		m2 = *message++;
		if (order != 8)
			m2 = toupper(m2);
#ifdef ORDER8_ALTERNATE_CASE
		else if (is_odd)
			m2 = tolower(m2);
		else
			m2 = toupper(m2);
		is_odd = !is_odd;
#endif
		if (order == 5 && m2 == map[0])
			m2 = map[1];

		/* Handle character pairs or an odd length message. */
		if (m1 == m2 || m2 == '\0') {
			m2 = PLAYFAIR_UNCOMMON;
			message--;
		}

		span1 = strchr(pf->table, m1) - pf->table;
		span2 = strchr(pf->table, m2) - pf->table;
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
			*op++ = pf->table[pos1.quot * order + (pos1.rem+1) % order];
			*op++ = pf->table[pos2.quot * order + (pos2.rem+1) % order];
		}

		/* Same column? */
		else if (pos1.rem == pos2.rem) {
			*op++ = pf->table[(pos1.quot+1) % order * order + pos1.rem];
			*op++ = pf->table[(pos2.quot+1) % order * order + pos2.rem];
		}

		/* Opposing corners. */
		else {
			*op++ = pf->table[pos1.quot * order + pos2.rem];
			*op++ = pf->table[pos2.quot * order + pos1.rem];
		}
	}
	*op = '\0';

#ifndef NDEBUG
	fputc('\n', stdout);
#endif
	return out;
}

char *
playfair_decode(Playfair *pf, const char *message)
{
	size_t length;
	char *out, *op;
	div_t pos1, pos2;
	int m1, m2, span1, span2, order;

	if (pf == NULL || message == NULL)
		return NULL;

	switch (strlen(pf->table)) {
	case 25: order = 5; break;
	case 36: order = 6; break;
	case 64: order = 8; break;
	default: order = 0; break;
	}

	length = strlen(message);
	if ((out = malloc(length+1)) == NULL)
		return NULL;

	op = out;
	while (*message != '\0') {
		m1 = *message++;
		if (order != 8)
			m1 = toupper(m1);

		/* Ignore punctuation and spacing. */
		if (!playfair_is_alphabet(order, m1))
			continue;

		/* Ignore punctuation and spacing. */
		for ( ; *message != '\0'; message++) {
			if (playfair_is_alphabet(order, *message))
				break;
		}

		m2 = *message++;
		if (order != 8)
			m2 = toupper(m2);

		span1 = strchr(pf->table, m1) - pf->table;
		span2 = strchr(pf->table, m2) - pf->table;

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
			*op++ = pf->table[pos1.quot * order + (pos1.rem+order-1) % order];
			*op++ = pf->table[pos2.quot * order + (pos2.rem+order-1) % order];
		}

		/* Same column? */
		else if (pos1.rem == pos2.rem) {
			*op++ = pf->table[(pos1.quot+order-1) % order * order + pos1.rem];
			*op++ = pf->table[(pos2.quot+order-1) % order * order + pos2.rem];
		}

		/* Opposing corners. */
		else {
			*op++ = pf->table[pos1.quot * order + pos2.rem];
			*op++ = pf->table[pos2.quot * order + pos1.rem];
		}
	}

	/* Discard trailing padding of the uncommon character. */
	if (pf->opt_undo_uncommon && out < op && op[-1] == PLAYFAIR_UNCOMMON)
		op--;
	*op = '\0';

	if (pf->opt_undo_uncommon && out < op) {
		/* Discard uncommon character separating double letters. */
		length = op-out;
		for (op = out+1; *op != '\0'; op++, length--) {
			if (*op == PLAYFAIR_UNCOMMON && op[-1] == op[1])
				memcpy(op, op+1, --length);
		}
	}
#ifndef NDEBUG
	fputc('\n', stdout);
#endif
	return out;
}

#ifdef TEST
#include <getopt.h>

static char usage[] =
"usage: playfair [-568dku][-a set] key [message]\n"
"\n"
"-5\t\tclassic playfair 25 character alphabet, where I=J (default)\n"
"-6\t\tmodified playfair 36 character alphabet and digits\n"
"-8\t\tmodified playfair 64 character alphabet (Base64 set)\n"
"-a set\t\tset alphabet order\n"
"-d\t\tdecode message\n"
"-k\t\tdump key table\n"
"-u\t\twhen decoding remove uncommon padding between double letters;\n"
"\t\tthe default is to leave them and let the user do this manually\n"
"\n"
"If message is omitted from the command line, then read the message\n"
"from standard input.\n"
"\n"
"Copyright 2010 by Anthony Howe.  All rights reserved.\n"
;

static char input[256];

int
main(int argc, char **argv)
{
	int ch;
	Playfair pf;
	playfair_fn fn;
	char *out, *alphabet;

	pf.opt_show_table = 0;
	pf.opt_undo_uncommon = 0;
	fn = playfair_encode;
	alphabet = ALPHABET25;

	while ((ch = getopt(argc, argv, "568dkua:")) != -1) {
		switch (ch) {
		case '5':
			alphabet = ALPHABET25;
			break;
		case '6':
			alphabet = ALPHABET36;
			break;
		case '8':
			alphabet = ALPHABET64;
			break;
		case 'd':
			fn = playfair_decode;
			break;
		case 'k':
			pf.opt_show_table = 1;
			break;
		case 'u':
			pf.opt_undo_uncommon = 1;
			break;
		case 'a':
			alphabet = optarg;
			break;
		default:
			optind = argc;
			break;
		}
	}

	if (argc < optind + 1) {
		fprintf(stderr, "missing key\n%s", usage);
		return EXIT_FAILURE;
	}

	if (playfair_init(&pf, alphabet, argv[optind])) {
		fprintf(stderr, "alphabet invalid\n");
		return EXIT_FAILURE;
	}

	if (argv[optind+1] != NULL) {
		if ((out = (*fn)(&pf, argv[optind+1])) == NULL) {
			fprintf(stderr, "out of memory\n");
			return EXIT_FAILURE;
		}

		playfair_print(stdout, out);
		free(out);
	} else {
		while (fgets(input, sizeof (input), stdin) != NULL) {
			if ((out = (*fn)(&pf, input)) == NULL) {
				fprintf(stderr, "out of memory\n");
				return EXIT_FAILURE;
			}
			playfair_print(stdout, out);
			free(out);
		}
	}

	return EXIT_SUCCESS;
}

#endif /* TEST */
