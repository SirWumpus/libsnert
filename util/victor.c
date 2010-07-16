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
# define ALPHABET		"ABCDEFGHIJKLMNOPQRSTUVWXYZ.0123456789"
#endif

/*
 * Seven most frequent characters in English are "ES TON I A".
 * Allows for inclusion of decimal digits and one punctuation
 * character in the key table.
 */
#if !defined(FREQUENT7)
# define FREQUENT7		"ESTONIA"
#endif

typedef char (victor_table)[3][38];

typedef struct {
	/* Public */
	char *key;		/* Alpha-numeric upto 36 characters. */
	char *seed;		/* ASCII number string, "1953". */
	char *freq7;		/* Seven most "frequent" alpha-numeric, eg "ESTONIA". */

	/* Private */
	char chain[51];		/* Chain addition table based on seed; 5x10 */
	char columns[11];	/* Digit order based on last row of chain table. */
	char table[3][38];	/* 1st row is the alphabet seeded with key.
				 * 2nd and 3rd rows are the ASCII digit codes
				 * for each glyph in the straddling checkerboard.
				 */
} Victor;

static int debug;
static const char alphabet[] = ALPHABET;

void
victor_dump_alphabet(FILE *fp, char table[3][38])
{
	int i;

	for (i = 0; i < 19; i++)
		fprintf(fp, "%c  ", table[0][i]);
	fprintf(fp, "\n");
	for (i = 0; i < 19; i++)
		fprintf(fp, "%c%c ", table[1][i], table[2][i]);
	fprintf(fp, "\n");
	for (i = 19; i < 37; i++)
		fprintf(fp, "%c  ", table[0][i]);
	fprintf(fp, "\n");
	for (i = 19; i < 37; i++)
		fprintf(fp, "%c%c ", table[1][i], table[2][i]);
	fprintf(fp, "\n");
}

void
victor_dump_table(FILE *fp, char table[3][38])
{
	int i, j, k;
	char row[2][21];

	memset(row, ' ', sizeof (row));
	row[0][20] = row[1][20] = '\0';

	for (i = '0'; i <= '9'; i++) {
		for (j = 0; j < sizeof (alphabet)-1; j++) {
			if (table[1][j] == i && table[2][j] == ' ') {
				k = (i-'0')*2;
				row[0][k] = table[0][j];
				row[1][k] = table[1][j];
				break;
			}
			if (table[1][j] == i && table[2][j] != ' ') {
				break;
			}
		}
	}
	fprintf(fp, "   %s\n +---------------------\n | %s\n", row[1], row[0]);

	memset(row, ' ', sizeof (row));
	row[0][30] = row[1][30] = '\0';

	for (j = 0; j < sizeof (alphabet)-1; j++) {
		if (table[2][j] != ' ') {
			i = table[2][j];
			k = (i-'0')*2;
			row[0][k] = table[0][j];
		}
		if (i == '0') {
			fprintf(fp, "%c| ", table[1][j]);
			i = 0;
		} else if (i == '9')
			fprintf(fp, "%s\n", row[0]);
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
victor_chain_add(const char *seed_number, char *buffer, size_t size)
{
	char *bp, *ep;
	size_t length;

	if (seed_number == NULL || buffer == NULL)
		return 1;

	for (bp = (char *) seed_number; *bp != '\0'; bp++) {
		if (!isdigit(*bp))
			return 2;
	}

	length = bp - seed_number;
	if (length < 2 || size <= length)
		return 3;

	(void) strncpy(buffer, seed_number, length);
	ep = &buffer[length];
	bp = buffer;

	for (size -= length+1; 0 < size; size--) {
		*ep++ = (bp[0]-'0' + bp[1]-'0') % 10 + '0';
		bp++;
	}
	*ep = '\0';

	if (debug) {
		printf("seed=%s\n", seed_number);
		victor_dump_chain(stdout, buffer);
		fputc('\n', stdout);
	}

	return 0;
}

static void
victor_digit_order(const char source[10], char out[11])
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
	out[10] = '\0';

	if (debug)
		printf("%s\n\n", out);
}

int
victor_init(Victor *vic)
{
	int i, j, k, ch;
	char set[sizeof (alphabet)], *member;

	if (vic->key == NULL)
		vic->key = "";
	if (vic->freq7 == NULL)
		vic->freq7 = FREQUENT7;
	if (strlen(vic->freq7) != 7)
		return 3;

	victor_chain_add(vic->seed, vic->chain, sizeof (vic->chain));
	victor_digit_order(vic->chain+40, vic->columns);

	/* Copy the alphabet into the unused set. */
	for (i = 0; i < sizeof (alphabet)-1; i++) {
		if (alphabet[i] == '\0')
			return 1;
		ch = alphabet[i];
		if (strchr(set, ch) != NULL)
			return 2;
		set[i] = ch;
		set[i+1] = '\0';
	}
	if (alphabet[i] != '\0')
		return 1;

	/* Copy the key into the table, removing key characters
	 * from the set of unused alphabet characters. This will
	 * alter the sequential order of the alphabet.
	 */
	for (i = j = 0; i < sizeof (alphabet)-1 && vic->key[i] != '\0'; i++) {
		ch = toupper(vic->key[i]);
		if ((member = strchr(set, ch)) != NULL) {
			vic->table[0][j++] = ch;
			*member = ' ';
		}
	}

	/* Copy remaining unused alphabet to table. */
	for (i = 0; i < sizeof (alphabet)-1; i++) {
		if (set[i] != ' ')
			vic->table[0][j++] = set[i];
	}
	vic->table[0][j] = '\0';

	/* Set table codes to undefined. */
	memset(vic->table[1], ' ', sizeof (vic->table[1]));
	memset(vic->table[2], ' ', sizeof (vic->table[2]));

	/* The VIC cipher is a similar to a Huffman encoding
	 * with the seven most frequent letters having a single
	 * digit encoding, and the less frequent letters and
	 * decimal digits having double digit encoding.
	 */

	/* Assign single digit code for seven most frequent
	 * letters based on the "frequent" set order.
	 */
	for (i = 0; i < 7; i++) {
		member = strchr(vic->table[0], toupper(vic->freq7[i]));
		vic->table[1][member - vic->table[0]] = vic->columns[i];
	}

	/* Assign double digit code for the remaining letters and
	 * digits, based on the blank positions in the "frequent"
	 * string.
	 */
	k = 0;
	for (i = 7; i < 10; i++) {
		for (j = 0; j < 10; k++) {
			if (vic->table[1][k] == ' ') {
				vic->table[1][k] = vic->columns[i];
				vic->table[2][k] = j+'0';
				j++;
			}
		}
	}

	return 0;
}

static void
victor_char_to_code(char table[3][38], const char *message, char *out)
{
	int index;
	const char *mp;
	char *op, *glyph;

	for (op = out, mp = message; *mp != '\0'; mp++) {
		if ((glyph = strchr(table[0], toupper(*mp))) == NULL)
			continue;

		index = glyph - table[0];

		*op++ = table[1][index];
		if (table[2][index] != ' ')
			*op++ = table[2][index];
	}
	*op = '\0';

	if (debug) {
		victor_dump_table(stdout, table);
		printf("\n\"%s\"\n%s\n\n", message, out);
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
		printf("%s\n", out);
		fputc('\n', stdout);
	}
}

static void
victor_code_to_char(char table[3][38], char *out)
{
	int i;
	char *op;

	for (op = out; *out != '\0'; out++) {
		for (i = 0; i < sizeof (alphabet)-1; i++) {
			if (*out == table[1][i]) {
				if (table[2][i] == ' ') {
					*op++ = table[0][i];
					break;
				} else if (out[1] == table[2][i]) {
					*op++ = table[0][i];
					out++;
					break;
				}
			}
		}
	}
	*op = '\0';
}

char *
victor_encode(Victor *vic, const char *message)
{
	char *out;
	size_t length;

	length = strlen(message) * 2;
	if ((out = malloc(length+1)) == NULL)
		return NULL;

	victor_char_to_code(vic->table, message, out);
	victor_mask_code(vic->chain, out);
	victor_code_to_char(vic->table, out);

	return out;
}

char *
victor_decode(Victor *vic, const char *message)
{
	char *out;

	for (out = vic->chain; *out != '\0'; out++)
		*out = (10 - *out + '0') % 10 + '0';

	if (debug) {
		victor_dump_chain(stdout, vic->chain);
		fputc('\n', stdout);
	}

	out = victor_encode(vic, message);

	return out;
}

#ifdef TEST
static char usage[] =
"usage: victor [-dkv][-f set] key number message\n"
"\n"
"-f set\t\tseven most frequent alpha-numeric\n"
"-d\t\tdecode message\n"
"-k\t\tdump key table\n"
"-v\t\tverbose debug\n"
"\n"
"Copyright 2010 by Anthony Howe.  All rights reserved.\n"
;

typedef char *(*victor_fn)(Victor *, const char *);

int
main(int argc, char **argv)
{
	char *out;
	Victor vic;
	victor_fn fn;
	int argi, show_key_table;

	show_key_table = 0;
	fn = victor_encode;
	vic.freq7 = FREQUENT7;

	for (argi = 1; argi < argc; argi++) {
		if (argv[argi][0] != '-' || (argv[argi][1] == '-' && argv[argi][2] == '\0'))
			break;

		switch (argv[argi][1]) {
		case 'd':
			fn = victor_decode;
			break;
		case 'f':
			vic.freq7 = argv[argi][2] == '\0' ? argv[++argi] : &argv[argi][2];
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

	vic.key = argv[argi];
	vic.seed = argv[argi+1];

	if (victor_init(&vic)) {
		fprintf(stderr, "error initialising Victor structure\n");
		return EXIT_FAILURE;
	}

	if ((out = (*fn)(&vic, argv[argi+2])) == NULL) {
		fprintf(stderr, "out of memory\n");
		return EXIT_FAILURE;
	}

	if (show_key_table) {
		victor_dump_alphabet(stdout, vic.table);
		fputc('\n', stdout);
	}

	fprintf(stdout, "%s\n", out);
	free(out);

	return EXIT_SUCCESS;
}

#endif /* TEST */
