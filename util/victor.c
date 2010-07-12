/*
 * victor.c
 *
 * http://en.wikipedia.org/wiki/VIC_cipher
 *
 * Copyright 2010 by Anthony Howe. All rights reserved.
 */

#define NDEBUG

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(ALPHABET)
# define ALPHABET		"ABCDEFGHIJKLMNOPQRSTUVWXYZ/0123456789"
#endif

/*
 * Eight most frequent characters in English are "ASIN TO ER".
 * Seven most frequent characters in English are "ES TON I A".
 * The latter allows for inclusion of decimal digits and one
 * punctuation / shift character in the key table.
 */
#if !defined(FREQUENT7)
# define FREQUENT7		"T.IE.ON.AS"
#endif

typedef char (victor_table)[3][38];

static const char alphabet[] = ALPHABET;

void
victor_dump_table(FILE *fp, victor_table key_table)
{
	int i, j, k;
	char row[2][21];

	memset(row, ' ', sizeof (row));
	row[0][20] = row[1][20] = '\0';

	for (i = '0'; i <= '9'; i++) {
		for (j = 0; j < sizeof (alphabet)-1; j++) {
			if (key_table[1][j] == i && key_table[2][j] == ' ') {
				k = (i-'0')*2;
				row[0][k] = key_table[0][j];
				row[1][k] = key_table[1][j];
				break;
			}
			if (key_table[1][j] == i && key_table[2][j] != ' ') {
				break;
			}
		}
	}
	fprintf(fp, "  %s\n  %s\n", row[1], row[0]);

	memset(row, ' ', sizeof (row));
	row[0][30] = row[1][30] = '\0';

	for (j = 0; j < sizeof (alphabet)-1; j++) {
		if (key_table[2][j] != ' ') {
			i = key_table[2][j];
			k = (i-'0')*2;
			row[0][k] = key_table[0][j];
		}
		if (i == '0') {
			fprintf(fp, "%c ", key_table[1][j]);
			i = 0;
		} else if (i == '9')
			fprintf(fp, "%s\n", row[0]);
	}
}

void
victor_dump(FILE *fp, victor_table key_table)
{
	int i;

	for (i = 0; i < 19; i++)
		fprintf(fp, "%c  ", key_table[0][i]);
	fprintf(fp, "\n");
	for (i = 0; i < 19; i++)
		fprintf(fp, "%c%c ", key_table[1][i], key_table[2][i]);
	fprintf(fp, "\n");
	for (i = 19; i < 37; i++)
		fprintf(fp, "%c  ", key_table[0][i]);
	fprintf(fp, "\n");
	for (i = 19; i < 37; i++)
		fprintf(fp, "%c%c ", key_table[1][i], key_table[2][i]);
	fprintf(fp, "\n");
}

int
victor_build(const char *key, const char *frequent, victor_table key_table)
{
	int i, j, k, ch;
	char set[sizeof (alphabet)], *member;

	if (key == NULL)
		key = "";
	if (frequent == NULL)
		frequent = FREQUENT7;
	if (strlen(frequent) < 10)
		return 3;

	/* Copy the alphabet into the unused set. */
	for (i = 0; i < sizeof (alphabet)-1; i++) {
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

	/* Copy the key into key_table, removing key characters
	 * from the set of unused alphabet characters. This will
	 * alter the sequential order of the alphabet.
	 */
	for (i = j = 0; i < sizeof (alphabet)-1 && key[i] != '\0'; i++) {
		ch = toupper(key[i]);
		if ((member = strchr(set, ch)) != NULL) {
			key_table[0][j++] = ch;
			*member = ' ';
		}
	}

	/* Copy remaining unused alphabet to key_table. */
	for (i = 0; i < sizeof (alphabet)-1; i++) {
		if (set[i] != ' ')
			key_table[0][j++] = set[i];
	}
	key_table[0][j] = '\0';

	/* Set key_table codes to undefined. */
	memset(key_table[1], ' ', sizeof (key_table[1]));
	memset(key_table[2], ' ', sizeof (key_table[2]));

	/* The VIC cipher is a similar to a Huffman encoding
	 * with the seven most frequent letters having a single
	 * digit encoding, and the less frequent letters and
	 * decimal digits having double digit encoding.
	 */

	/* Assign single digit code for seven most frequent
	 * letters based on the "frequent" set order.
	 */
	for (i = 0; i < 10; i++) {
		if (isalnum(frequent[i])) {
			member = strchr(key_table[0], toupper(frequent[i]));
			key_table[1][member - key_table[0]] = i+'0';
		}
	}

	/* Assign double digit code for the remaining letters and
	 * digits, based on the blank positions in the "frequent"
	 * string.
	 */
	k = 0;
	for (i = 0; i < 10; i++) {
		if (!isalnum(frequent[i])) {
			for (j = 0; j < 10; k++) {
				if (key_table[1][k] == ' ') {
					key_table[1][k] = i+'0';
					key_table[2][k] = j+'0';
					j++;
				}
			}
		}
	}

	return 0;
}

static void
victor_char_to_code(victor_table key_table, const char *message, char *out)
{
	int index;
	char *op, *glyph;

	for (op = out; *message != '\0'; message++) {
		if ((glyph = strchr(key_table[0], toupper(*message))) == NULL)
			continue;

		index = glyph - key_table[0];

		*op++ = key_table[1][index];
		if (key_table[2][index] != ' ')
			*op++ = key_table[2][index];
	}
	*op = '\0';

#ifndef NDEBUG
	printf("%s\n", out);
#endif
}

static void
victor_mask_code(const char *key_mask, char *out)
{
	char *op;
	const char *mask;

	mask = key_mask;
	for (op = out; *op != '\0'; op++) {
		*op = (*op - '0' + *mask - '0') % 10 + '0';
		if (*++mask == '\0')
			mask = key_mask;
	}

#ifndef NDEBUG
	printf("%s\n", out);
#endif
}

static void
victor_code_to_char(victor_table key_table, char *out)
{
	int i;
	char *op;

	for (op = out; *out != '\0'; out++) {
		for (i = 0; i < sizeof (alphabet)-1; i++) {
			if (*out == key_table[1][i]) {
				if (key_table[2][i] == ' ') {
					*op++ = key_table[0][i];
					break;
				} else if (out[1] == key_table[2][i]) {
					*op++ = key_table[0][i];
					out++;
					break;
				}
			}
		}
	}
	*op = '\0';
}

char *
victor_encode(victor_table key_table, const char *key_mask, const char *message)
{
	char *out;
	size_t length;

	length = strlen(message) * 2;
	if ((out = malloc(length+1)) == NULL)
		return NULL;

	victor_char_to_code(key_table, message, out);
	victor_mask_code(key_mask, out);
	victor_code_to_char(key_table, out);

	return out;
}

char *
victor_decode(victor_table key_table, const char *key_mask, const char *message)
{
	char *out, *inverse_mask;

	inverse_mask = strdup(key_mask);
	for (out = inverse_mask; *out != '\0'; out++)
		*out = ('9'+1) - *out + '0';
#ifndef NDEBUG
	printf("%s\n", inverse_mask);
#endif
	out = victor_encode(key_table, inverse_mask, message);
	free(inverse_mask);

	return out;
}

#ifdef TEST
static char usage[] =
"usage: victor [-dk][-f set] key number message\n"
"\n"
"-f set\t\tset order of 7 most frequent alpha-numeric and 3 non\n"
"\t\talpha-numeric; eg. \"ES.TO.NI.A\" or \".AI.NOT.SE\"\n"
"-d\t\tdecode message\n"
"-k\t\tdump key table\n"
"\n"
"Copyright 2010 by Anthony Howe.  All rights reserved.\n"
;

typedef char *(*victor_fn)(victor_table, const char *, const char *);

int
main(int argc, char **argv)
{
	victor_fn fn;
	char *out, *frequent;
	victor_table key_table;
	int argi, show_key_table;

	show_key_table = 0;
	fn = victor_encode;
	frequent = FREQUENT7;

	for (argi = 1; argi < argc; argi++) {
		if (argv[argi][0] != '-' || (argv[argi][1] == '-' && argv[argi][2] == '\0'))
			break;

		switch (argv[argi][1]) {
		case 'd':
			fn = victor_decode;
			break;
		case 'f':
			frequent = argv[argi][2] == '\0' ? argv[++argi] : &argv[argi][2];
			break;
		case 'k':
			show_key_table = 1;
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

	if (victor_build(argv[argi], frequent, key_table)) {
		fprintf(stderr, "error building key table\n");
		return EXIT_FAILURE;
	}

	if ((out = (*fn)(key_table, argv[argi+1], argv[argi+2])) == NULL) {
		fprintf(stderr, "out of memory\n");
		return EXIT_FAILURE;
	}

	if (show_key_table) {
		victor_dump(stdout, key_table);
		fputc('\n', stdout);
		victor_dump_table(stdout, key_table);
		fputc('\n', stdout);
	}

	fprintf(stdout, "%s\n", out);
	free(out);

	return EXIT_SUCCESS;
}

#endif /* TEST */
