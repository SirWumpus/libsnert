/*
 * cipher.c
 *
 * http://users.telenet.be/d.rijmenants/
 *
 * Copyright 2010, 2011 by Anthony Howe. All rights reserved.
 */

#define NDEBUG

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Seven most frequent characters in English are "ESTONIA".
 * Allows for inclusion of decimal digits and one punctuation
 * character in the key table.
 */
#if !defined(FREQUENT7)
# define FREQUENT7		"ESTONIA"
#endif
#if !defined(FREQUENT8)
# define FREQUENT8		"SENORITA"
#endif

/*
 * Note that both alphabets are a subset of the Base64 invariant
 * character set by design.
 */
#if !defined(ALPHABET28)
# define ALPHABET28		"ABCDEFGHIJKLMNOPQRSTUVWXYZ/+"
#endif
#if !defined(ALPHABET37)
# define ALPHABET37		"ABCDEFGHIJKLMNOPQRSTUVWXYZ/0123456789"
#endif

#if !defined(NUMERIC_SEED)
# define NUMERIC_SEED		"3141592653"
#endif

typedef char (cipher_ct)[3][sizeof (ALPHABET37)];

typedef struct {
	/* Public */

	/* Private */
	size_t ct_size;		/* Conversion alphabet size to use 28 or 37. */
	size_t chain_size;
	char *chain;		/* Chain addition table based on seed; 10x10 + NUL */
	char order[11];		/* Digit order based on last row of chain table, plus NUL. */
	cipher_ct table;	/* 1st row is the alphabet seeded with key.
				 * 2nd and 3rd rows are the ASCII digit codes
				 * for each glyph in the straddling checkerboard.
				 */
} Cipher;

static int debug;

void
cipher_dump_alphabet(FILE *fp, cipher_ct table)
{
	int i;
	size_t half, length;

	length = strlen(table[0]);
	half = length/2;

	fprintf(fp, "Conversion Table\n\n\t");

	for (i = 0; i < half; i++)
		fprintf(fp, "%c  ", table[0][i]);
	fprintf(fp, "\n\t");
	for (i = 0; i < half; i++)
		fprintf(fp, "%c%c ", table[1][i], table[2][i]);
	fprintf(fp, "\n\t");
	for (i = half; i < length; i++)
		fprintf(fp, "%c  ", table[0][i]);
	fprintf(fp, "\n\t");
	for (i = half; i < length; i++)
		fprintf(fp, "%c%c ", table[1][i], table[2][i]);
	fprintf(fp, "\n\n");
}

void
cipher_dump_ct(FILE *fp, cipher_ct table)
{
	int i, j, k;
	size_t length;
	char row[2][21];

	fprintf(fp, "Straddling Checkerboard\n\n");

	length = strlen(table[0]);
	memset(row, ' ', sizeof (row));
	row[0][20] = row[1][20] = '\0';

	for (i = '0'; i <= '9'; i++) {
		for (j = 0; j < length; j++) {
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
	fprintf(fp, "\t   %s\n\t +---------------------\n\t | %s\n", row[1], row[0]);

	memset(row, ' ', sizeof (row));
	row[0][20] = row[1][20] = '\0';

	for (j = 0; j < length; j++) {
		if (table[2][j] != ' ') {
			i = table[2][j];
			k = (i-'0')*2;
			row[0][k] = table[0][j];
		}
		if (i == '0') {
			fprintf(fp, "\t%c| ", table[1][j]);
			i = 0;
		} else if (i == '9')
			fprintf(fp, "%s\n", row[0]);
	}
	fprintf(fp, "\n");
}

void
cipher_dump_chain(FILE *fp, const char *chain)
{
	int i;

	while (*chain != '\0') {
		fputc('\t', fp);
		for (i = 0; i < 10 && *chain != '\0'; i++)
			fputc(*chain++, fp);
		fputc('\n', fp);
	}
}

void
cipher_dump_numbers(FILE *fp, const char *numbers)
{
	int c;

	while (*numbers != '\0') {


		fputc('\t', fp);
		for (c = 0; c < 50 && *numbers != '\0'; numbers++) {
			if (isdigit(*numbers)) {
				fputc(*numbers, fp);
				if ((++c % 5) == 0)
					fputc(' ', fp);
			}
		}
		fputc('\n', fp);
	}
}

int
cipher_chain_add(const char *seed_number, char *buffer, size_t size)
{
	char *bp, *ep;
	size_t length, n;

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

	for (n = size - length - 1; 0 < n; n--) {
		*ep++ = (bp[0]-'0' + bp[1]-'0') % 10 + '0';
		bp++;
	}
	*ep = '\0';

	if (debug) {
		fprintf(stderr, "Chain Addition MOD 10 (seed=%s length=%lu)\n\n", seed_number, (unsigned long) size-1);
		cipher_dump_chain(stderr, buffer);
		fputc('\n', stderr);
	}

	return 0;
}

void
cipher_digit_order(const char source[10], char out[11])
{
	const char *sp;
	int digit, count;

	for (count = digit = '0'; digit <= '9'; digit++) {
		for (sp = source; *sp != '\0'; sp++) {
			if (*sp == digit) {
				out[sp - source] = count;
				count++;
			}
		}
	}
	out[10] = '\0';

	if (debug) {
		fprintf(stderr, "Digit Order 0..9\n\n");
		fprintf(stderr, "\t%s\n\n", out);
	}
}

int
cipher_init(Cipher *ctx, int ct_size, const char *key, const char *seed)
{
	size_t freq_len;
	int i, j, k, ch;
	char alphabet[sizeof (ALPHABET37)], *member, *freq;

	if (key == NULL)
		key = "";
	if (seed == NULL)
		seed = NUMERIC_SEED;

	if (ct_size == sizeof (ALPHABET37)-1) {
		freq = FREQUENT7;
		freq_len = sizeof (FREQUENT7)-1;
		strcpy(alphabet, ALPHABET37);
	} else {
		freq = FREQUENT8;
		freq_len = sizeof (FREQUENT8)-1;
		ct_size = sizeof (ALPHABET28)-1;
		strcpy(alphabet, ALPHABET28);
	}

	if (cipher_chain_add(seed, ctx->chain, ctx->chain_size))
		return 1;

	cipher_digit_order(ctx->chain+ctx->chain_size-11, ctx->order);

	ctx->ct_size = ct_size;
	memset(ctx->table, ' ', sizeof (ctx->table));

	for (i = j = 0; i < ct_size && freq[i] != '\0'; i++) {
		ch = freq[i];
		if ((member = strchr(alphabet, ch)) != NULL) {
			ctx->table[0][j++] = ch;
			*member = 0x7F;
		}
	}

	/* Copy the key into the table, removing key characters
	 * from the set of unused alphabet characters. This will
	 * alter the sequential order of the alphabet.
	 */
	for (i = 0; i < ct_size && key[i] != '\0'; i++) {
		ch = toupper(key[i]);
		if ((member = strchr(alphabet, ch)) != NULL) {
			ctx->table[0][j++] = ch;
			*member = 0x7F;
		}
	}

	/* Copy remaining unused alphabet to table. */
	for (i = 0; i < ct_size; i++) {
		if (alphabet[i] != 0x7F)
			ctx->table[0][j++] = alphabet[i];
	}
	ctx->table[0][j] = '\0';

	/* The VIC cipher is a similar to a Huffman encoding
	 * with the seven most frequent letters having a single
	 * digit encoding, and the less frequent letters and
	 * decimal digits having double digit encoding.
	 */

	if (debug) {
		fprintf(
			stderr,
			"Most frequent English symbols (%s) assigned a column\n"
			"based on digit order of last row of chain addition table.\n",
			freq
		);
		fputc('\n', stderr);
	}

	/* Assign single digit code for seven most frequent
	 * letters based on the "frequent" set order.
	 */
	for (i = 0; i < freq_len; i++) {
		member = strchr(ctx->table[0], freq[i]);
		ctx->table[1][member - ctx->table[0]] = ctx->order[i];
	}

	/* Assign double digit code for the remaining letters and
	 * digits, based on the blank positions in the "frequent"
	 * string.
	 */
	k = 0;
	for (i = freq_len; i < 10; i++) {
		for (j = 0; j < 10; k++) {
			if (ctx->table[1][k] == ' ') {
				ctx->table[1][k] = ctx->order[i];
				ctx->table[2][k] = j+'0';
				j++;
			}
		}
	}

	if (debug) {
		/* http://en.wikipedia.org/wiki/Straddling_checkerboard	*/
		cipher_dump_ct(stderr, ctx->table);
		fputc('\n', stdout);
	}

	return 0;
}

Cipher *
cipher_new(int ct_size, const char *key, const char *seed, int chain_length)
{
	Cipher *ctx;

	if (chain_length < 10)
		chain_length = 100;

	if ((ctx = malloc(sizeof (*ctx) + chain_length + 1)) == NULL)
		return NULL;

	ctx->chain = (char *) &ctx[1];
	ctx->chain_size = chain_length+1;

	cipher_init(ctx, ct_size, key, seed);

	return ctx;
}

void
cipher_char_to_code(cipher_ct table, const char *message, char *out)
{
	int index, i;
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

	for (i = (op - out) % 5; 0 < i && i < 5; i++)
		*op++ = '0';

	*op = '\0';

	if (debug) {
		cipher_dump_alphabet(stderr, table);
		fputc('\n', stdout);
		fprintf(stderr, "Using conversion table convert message to a numeric form.\n\n");
		fprintf(stderr, "\t\"%s\"\n", message);
		cipher_dump_numbers(stderr, out);
		fputc('\n', stderr);
	}
}

void
cipher_mask_code(const char *key_mask, char *out)
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
		fprintf(stderr, "Column add MOD 10 using chain addition table.\n\n");
//		fprintf(stderr, "\t%s\n", out);
		cipher_dump_numbers(stderr, out);
		fputc('\n', stderr);
	}
}

void
cipher_code_to_char(cipher_ct table, char *out)
{
	int i;
	char *op;
	size_t length;

	if (debug) {
		fprintf(stderr, "Using conversion table reverse the numeric form into a string.\n\n");
	}

	length = strlen(table[0]);
	for (op = out; *out != '\0'; out++) {
		for (i = 0; i < length; i++) {
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
cipher_encode(Cipher *ctx, const char *message)
{
	char *out;
	size_t length;

	length = strlen(message) * 2;
	length = (length + 4) / 5 * 5;

	if ((out = malloc(length+1)) == NULL)
		return NULL;

	cipher_char_to_code(ctx->table, message, out);
	cipher_mask_code(ctx->chain, out);
	cipher_code_to_char(ctx->table, out);

	return out;
}

char *
cipher_decode(Cipher *ctx, const char *message)
{
	char *out;

	/* Invert the chain addition table for decoding. */
	for (out = ctx->chain; *out != '\0'; out++)
		*out = (10 - *out + '0') % 10 + '0';

	if (debug) {
		fprintf(stderr, "Inverted Chain Addition Table\n\n");
		cipher_dump_chain(stderr, ctx->chain);
		fputc('\n', stderr);
	}

	out = cipher_encode(ctx, message);

	return out;
}

#ifdef TEST
static char usage[] =
"usage: cipher [-cdv][-l length] key number [message]\n"
"\n"
"-c\t\tuse a conversion table 37, instead of 28\n"
"-d\t\tdecode message\n"
"-l length\tchain addition table length; default 100\n"
"-v\t\tverbose debug\n"
"\n"
"Key is a case insensitive string written in the conversion table alphabet.\n"
"Number is numeric string used as the seed for the chain addition table.\n"
"If message is omitted from the command line, then read the message from\n"
"standard input.\n"
"\n"
"Copyright 2010, 2011 by Anthony Howe.  All rights reserved.\n"
;

typedef char *(*cipher_fn)(Cipher *, const char *);

static char input[256];

int
main(int argc, char **argv)
{
	Cipher *ctx;
	cipher_fn fn;
	char *out, *arg, *stop;
	int argi, ct_size, chain_length = 0;

	ct_size = 28;
	fn = cipher_encode;

	for (argi = 1; argi < argc; argi++) {
		if (argv[argi][0] != '-' || (argv[argi][1] == '-' && argv[argi][2] == '\0'))
			break;

		switch (argv[argi][1]) {
		case 'c':
			ct_size = 37;
			break;
		case 'd':
			fn = cipher_decode;
			break;
		case 'l':
			arg = argv[argi][2] == '\0' ? argv[++argi] : &argv[argi][2];
			if ((chain_length = strtol(arg, &stop, 10)) <= 0 || *stop != '\0')
				chain_length = 100;
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
		fprintf(stderr, "missing key and/or number\n%s", usage);
		return EXIT_FAILURE;
	}

	if ((ctx = cipher_new(ct_size, argv[argi], argv[argi+1], chain_length)) == NULL) {
		fprintf(stderr, "error initialising Cipher structure\n");
		return EXIT_FAILURE;
	}

	if (argv[argi+2] != NULL) {
		if ((out = (*fn)(ctx, argv[argi+2])) == NULL) {
			fprintf(stderr, "out of memory\n");
			free(ctx);
			return EXIT_FAILURE;
		}

		fprintf(stdout, "\t%s\n", out);
		free(out);
	} else {
		while (fgets(input, sizeof (input), stdin) != NULL) {
			if ((out = (*fn)(ctx, input)) == NULL) {
				fprintf(stderr, "out of memory\n");
				free(ctx);
				return EXIT_FAILURE;
			}
			fprintf(stdout, "\t%s\n", out);
			free(out);
		}
	}

	free(ctx);

	return EXIT_SUCCESS;
}

#endif /* TEST */
