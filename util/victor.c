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

static int debug;
static const char alphabet[] = ALPHABET;

void
victor_dump_alphabet(FILE *fp, victor_table key_table)
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

void
victor_dump_checkerboard(FILE *fp, victor_table key_table)
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
	fprintf(fp, "   %s\n +---------------------\n | %s\n", row[1], row[0]);

	memset(row, ' ', sizeof (row));
	row[0][30] = row[1][30] = '\0';

	for (j = 0; j < sizeof (alphabet)-1; j++) {
		if (key_table[2][j] != ' ') {
			i = key_table[2][j];
			k = (i-'0')*2;
			row[0][k] = key_table[0][j];
		}
		if (i == '0') {
			fprintf(fp, "%c| ", key_table[1][j]);
			i = 0;
		} else if (i == '9')
			fprintf(fp, "%s\n", row[0]);
	}
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
	const char *mp;
	char *op, *glyph;

	for (op = out, mp = message; *mp != '\0'; mp++) {
		if ((glyph = strchr(key_table[0], toupper(*mp))) == NULL)
			continue;

		index = glyph - key_table[0];

		*op++ = key_table[1][index];
		if (key_table[2][index] != ' ')
			*op++ = key_table[2][index];
	}
	*op = '\0';

	if (debug) {
		printf("checkboard substitution\n");
		victor_dump_checkerboard(stdout, key_table);
		printf("\nmessage=\"%s\"\n%s\n\n", message, out);
	}
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

	if (debug) {
		printf("masking transposition\n");
		printf("%s\n", out);
		fputc('\n', stdout);
	}
}

static void
victor_dump_chain(FILE *fp, char *chain)
{
	int i;

	while (*chain != '\0') {
		for (i = 0; i < 10 && *chain != '\0'; i++)
			fputc(*chain++, fp);
		fputc('\n', fp);
	}
}

static int
victor_chain_addition(const char *seed_number, char *buffer, size_t size)
{
	char *bp, *ep;
	size_t length;

	length = strlen(seed_number);
	if (length < 2 || size <= length)
		return 1;

	(void) strncpy(buffer, seed_number, length);
	ep = &buffer[length];
	bp = buffer;

	for (size -= length+1; 0 < size; size--) {
		*ep++ = (bp[0]-'0' + bp[1]-'0') % 10 + '0';
		bp++;
	}
	*ep = '\0';

	if (debug) {
		printf("chain addition seed=%s\n", seed_number);
		victor_dump_chain(stdout, buffer);
		fputc('\n', stdout);
	}

	return 0;
}

static void
victor_digit_order(const char source[10], char out[10])
{
	const char *sp;
	int digit, count;

	for (count = digit = '0'; digit <= '9' && count <= '9'; digit++) {
		for (sp = source; *sp != '\0'; sp++) {
			if (*sp == digit) {
				out[sp - source] = count;
				count++;
			}
		}
	}

	if (debug) {
		printf("%.10s\n\n", out);
	}
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
victor_encode(victor_table key_table, const char *key_seed, const char *message)
{
	size_t length;
	char *out, chain[51], columns[10];

	length = strlen(message) * 2;
	if ((out = malloc(length+1)) == NULL)
		return NULL;

	victor_chain_addition(key_seed, chain, sizeof(chain));
	victor_digit_order(chain+40, columns);
	victor_char_to_code(key_table, message, out);
	victor_mask_code(chain, out);
	victor_code_to_char(key_table, out);

	return out;
}

char *
victor_decode(victor_table key_table, const char *key_seed, const char *message)
{
	char *out, chain[51];

	victor_chain_addition(key_seed, chain, sizeof(chain));
	for (out = chain; *out != '\0'; out++)
		*out = ('9'+1) - *out + '0';

	if (debug)
		printf("%s\n", chain);

	out = victor_encode(key_table, chain, message);

	return out;
}

#ifdef TEST
static char usage[] =
"usage: victor [-dkv][-f set] key number message\n"
"\n"
"-f set\t\tset order of 7 most frequent alpha-numeric and 3 non\n"
"\t\talpha-numeric; eg. \"ES.TO.NI.A\" or \".AI.NOT.SE\"\n"
"-d\t\tdecode message\n"
"-k\t\tdump key table\n"
"-v\t\tverbose debug\n"
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
		case 'v':
			debug++;
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
		victor_dump_alphabet(stdout, key_table);
		fputc('\n', stdout);
		victor_dump_checkerboard(stdout, key_table);
		fputc('\n', stdout);
	}

	fprintf(stdout, "%s\n", out);
	free(out);

	return EXIT_SUCCESS;
}

#endif /* TEST */
