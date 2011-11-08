/*
 * cipher.c
 *
 * An API implementing pen & paper cipher techniques.
 *
 * http://users.telenet.be/d.rijmenants/
 *
 * Copyright 2010, 2011 by Anthony Howe. All rights reserved.
 */

#define TRANSPOSE

#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Note that both CT28 and CT37 alphabets are a subset of the
 * Base64 invariant character set by design. This allows for
 * encrypted messages to appear as though they were Base64
 * encoded.
 */
#define CT28			"ABCDEFGHIJKLMNOPQRSTUVWXYZ+/"
#define CT37			"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/"

/*
 * Eight most frequent characters in English are "SENORITA".
 * Allows for inclusion of two punctuation characters in the
 * key table, one of which is used for numeric shift. Used
 * with CT28.
 */
#define FREQUENT8		"SENORITA"

/*
 * Seven most frequent characters in English are "ESTONIA".
 * Allows for inclusion of decimal digits and one punctuation
 * character in the key table. Used with CT37.
 */
#define FREQUENT7		"ESTONIA"

#if !defined(NUMERIC_SEED)
# define NUMERIC_SEED		"3141592653"	/* PI to 9 decimal places. */
#endif

/**
 * 1st row is the alphabet seeded with frequent letters and the key.
 * 2nd and 3rd rows are the ASCII digit codes for each glyph in the
 * straddling checkerboard.
 */
typedef char (cipher_ct)[3][sizeof (CT37)];

/***********************************************************************
 *** Dump Functions
 ***********************************************************************/

static int debug;

/**
 * @param fp
 *	An output FILE pointer.
 *
 * @param table
 *	A conversion table for CT28 or CT37.
 */
void
cipher_dump_alphabet(FILE *fp, cipher_ct table)
{
	int i;
	size_t half, length;

	length = strlen(table[0]);
	half = length/2;

	fprintf(fp, "Conversion Table %d\n\n\t", length);

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
	fprintf(fp, "\n");
}

/**
 * @param fp
 *	An output FILE pointer.
 *
 * @param table
 *	A conversion table CT28 or CT37 to output in straddling
 *	checkerboard format.
 */
void
cipher_dump_ct(FILE *fp, cipher_ct table)
{
	int i, j, k;
	size_t length;
	char row[2][21];

	length = strlen(table[0]);
	memset(row, ' ', sizeof (row));
	row[0][20] = row[1][20] = '\0';

	fprintf(fp, "CT%d Straddling Checkerboard\n\n", length);

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
//	fprintf(fp, "\n");
}

/**
 * @param fp
 *	An output FILE pointer.
 *
 * @param chain
 *	A chain addition table.
 */
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

/**
 * @param fp
 *	An output FILE pointer.
 *
 * @param text
 *	A numeric C string to output in space separated
 *	groups of 5 characters.
 */
void
cipher_dump_grouping(FILE *fp, int grouping, const char *text)
{
	int group, col;

	if (grouping < 1)
		grouping = 1;

	while (*text != '\0') {
		group = 0;
		fputc('\t', fp);
		for (col = 0; col < 60 && *text != '\0'; text++) {
			fputc(*text, fp);
			col++;

			if ((++group % grouping) == 0) {
				fputc(' ', fp);
				group = 0;
				col++;
			}
		}
		fputc('\n', fp);
	}
}

/**
 * @param fp
 *	An output FILE pointer.
 *
 * @param num_key
 *	A numeric C string representing the transposition key.
 *
 * @param source
 *	A numeric C string representing the transposition table.
 */
void
cipher_dump_transposition(FILE *fp, const char *num_key, const char *source)
{
	int col;
	const char *sp;
	size_t key_len;

	key_len = strlen(num_key);
	fprintf(stderr, "\t%s\n\t", num_key);
	for (col = 0; col < key_len; col++)
		fputc('-', fp);
	fputc('\n', fp);

	col = 0;
	for (sp = source; *sp != '\0'; sp++) {
		if (col++ == 0)
			fputc('\t', fp);
		fputc(*sp, fp);
		if (key_len <= col) {
			fputc('\n', fp);
			col = 0;
		}
	}

	fputc('\n', fp);
}

/***********************************************************************
 *** Number Generation Functions
 ***********************************************************************/

/**
 * @param seed_number
 *	A numeric C string.
 *
 * @param buffer
 *	A pointer to a buffer of size bytes. The seed will be
 *	copied into buffer and then chain addition MOD 10 will
 *	be used to fill the remainder of the buffer. The buffer
 *	will be NUL terminated.
 *
 * @param size
 *	The size in bytes of buffer.
 *
 * @return
 *	Zero (0) on succes or non-zero on error.
 */
int
cipher_chain_add(const char *seed_number, char *buffer, size_t size)
{
	char *bp, *ep;
	size_t length, n;

	if (buffer == NULL)
		return 1;

	if (seed_number == NULL)
		seed_number = NUMERIC_SEED;

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

/**
 * @param chain
 *	A numeric C string representing the chain addition to be
 *	inverted, in place.
 */
void
cipher_chain_invert(char *chain)
{
	char *out;

	for (out = chain; *out != '\0'; out++)
		*out = (10 - *out + '0') % 10 + '0';

	if (debug) {
		fprintf(stderr, "Inverted Chain Addition Table\n\n");
		cipher_dump_chain(stderr, chain);
		fputc('\n', stderr);
	}
}

/**
 * @param source
 *	A numeric C string of 10 digits.
 *
 * @param out
 *	An output buffer of at least 11 bytes, that will contain
 *	the digits from 0 to 9 inclusive corresponding to the
 *	order of digits from source string. The buffer will be
 *	NUL terminated.
 */
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
		fprintf(stderr, "\t%s\n", source);
		fprintf(stderr, "\t----------\n");
		fprintf(stderr, "\t%s\n\n", out);
	}
}

/**
 * @param source
 *	A C string of 10 alphabetic letters.
 *
 * @param out
 *	An output buffer of at least 11 bytes, that will contain
 *	the digits from 0 to 9 inclusive corresponding to the
 *	order of letters from source string. The buffer will be
 *	NUL terminated.
 */
void
cipher_alpha_order(const char source[10], char out[11])
{
	const char *sp;
	int alpha, count;

	/* Assumes ASCII character set order. */
	for (count = alpha = 'A'; alpha <= 'Z'; alpha++) {
		for (sp = source; *sp != '\0'; sp++) {
			if (*sp == alpha) {
				out[sp - source] = count;
				count++;
			}
		}
	}
	out[10] = '\0';

	if (debug) {
		fprintf(stderr, "Alpa Order 0..9\n\n");
		fprintf(stderr, "\t%s\n", source);
		fprintf(stderr, "\t----------\n");
		fprintf(stderr, "\t%s\n\n", out);
	}
}

/***********************************************************************
 *** Encoding & Decoding Functions
 ***********************************************************************/

/**
 * @param key
 *	A numeric C string representing the transposition key.
 *
 * @param in
 *	A C string containing the message.
 *
 * @return
 *	A dynamic C string of the message after applying a
 *	simple column transposition encoding. It is the caller's
 *	responsibility to free this memory when done.
 */
char *
cipher_simple_transposition_encode(const char *key, const char *in)
{
	char *out, *op;
	int digit, col, i;
	size_t key_len, in_len;

	if (key == NULL || in == NULL)
		return NULL;

	if (debug) {
		fprintf(stderr, "Simple Columnar Transposition\n\n");
		cipher_dump_transposition(stderr, key, in);
		fputc('\n', stderr);
	}

	in_len = strlen(in);
	if ((out = malloc(in_len+1)) == NULL)
		return NULL;

	op = out;
	key_len = strlen(key);
	for (digit = '0'; digit <= '9'; digit++) {
		for (col = 0; key[col] != '\0'; col++) {
			if (key[col] == digit) {
				for (i = col; i < in_len; i += key_len)
					*op++ = in[i];
			}
		}
	}
	*op = '\0';

	if (debug) {
		fprintf(stderr, "Message read by column by transposition key order:\n\n");
		cipher_dump_grouping(stderr, 5, out);
		fputc('\n', stderr);
	}

	return out;
}

/**
 * @param key
 *	A numeric C string representing the transposition key.
 *
 * @param in
 *	A C string containing the message.
 *
 * @return
 *	A dynamic C string of the message after applying a
 *	simple column transposition decoding. It is the caller's
 *	responsibility to free this memory when done.
 */
char *
cipher_simple_transposition_decode(const char *key, const char *in)
{
	char *out;
	int digit, col, i;
	size_t key_len, in_len;

	if (key == NULL || in == NULL)
		return NULL;

	in_len = strlen(in);
	if ((out = malloc(in_len+1)) == NULL)
		return NULL;

	key_len = strlen(key);
	for (digit = '0'; digit <= '9'; digit++) {
		for (col = 0; key[col] != '\0'; col++) {
			if (key[col] == digit) {
				for (i = col; i < in_len; i += key_len)
					out[i] = *in++;
			}
		}
	}
	out[in_len] = '\0';

	if (debug) {
		fprintf(stderr, "Simple Columnar Transposition Table\n\n");
		cipher_dump_transposition(stderr, key, out);
		fputc('\n', stderr);
	}

	return out;
}

/**
 * @param key
 *	A numeric C string representing the transposition key.
 *
 * @param in
 *	A C string containing the message.
 *
 * @return
 *	A dynamic C string of the message after applying a
 *	disrupted column transposition encoding.
 */
char *
cipher_disrupted_transposition_encode(const char *key, const char *in)
{
/**** TODO ****/
	return NULL;
}

/**
 * @param key
 *	A numeric C string representing the transposition key.
 *
 * @param in
 *	A C string containing the message.
 *
 * @return
 *	A dynamic C string of the message after applying a
 *	disrupted column transposition decoding.
 */
char *
cipher_disrupted_transposition_decode(const char *key, const char *in)
{
/**** TODO ****/
	return NULL;
}

/**
 * @param ct_size
 *	The conversion table size, 28 or 37. Used to select
 *	both the alphabet and frequent English letter list.
 *
 * @param key
 *	A C string for the key in the conversion table alphabet.
 *	Used to scramble alphabet order. Can be NULL for the
 *	default English order.
 *
 * @parma ct_out
 *	A buffer at least ct_size+1 bytes in size to hold the
 *	reordered conversion table alphabet.
 *
 * @return
 *	Zero on success, otherwise non-zero on error.
 *
 * @note
 *	Use cipher_alphabet_fill() to initialise the 1st row
 *	of a cipher_ct table and cipher_ct_fill() to initialise
 *	the remainder of the table based on the alphabet.
 */
int
cipher_alphabet_fill(int ct_size, const char *key, char *ct_out)
{
	int i, j, ch;
	char alphabet[sizeof (CT37)], *member, *freq;

	if (key == NULL)
		key = "";

	if (ct_size == sizeof (CT37)-1) {
		freq = FREQUENT7;
		strcpy(alphabet, CT37);
	} else if (ct_size == sizeof (CT28)-1) {
		freq = FREQUENT8;
		strcpy(alphabet, CT28);
	} else {
		return errno = EINVAL;
	}

	/* Copy the frequently used english letters into the table,
	 * removing the letters from the set of unused alphabet
	 * characters. This will alter the sequential order of the
	 * alphabet.
	 */
	for (i = j = 0; i < ct_size && freq[i] != '\0'; i++) {
		ch = freq[i];
		if ((member = strchr(alphabet, ch)) != NULL) {
			ct_out[j++] = ch;
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
			ct_out[j++] = ch;
			*member = 0x7F;
		}
	}

	/* Copy remaining unused alphabet to table. */
	for (i = 0; i < ct_size; i++) {
		if (alphabet[i] != 0x7F)
			ct_out[j++] = alphabet[i];
	}
	ct_out[j] = '\0';

	return errno = 0;
}

/**
 * @param order
 *	A numeric C string of 10 digits 0 to 9 used as a key
 *	to initialise the conversion table.
 *
 * @param table
 *	A pointer to a cipher_ct, where 1st row is the alphabet
 *	seeded with frequent English letters and a key. The 2nd
 *	and 3rd rows will be initialised with the ASCII digit
 *	codes (or space) for each glyph based on a straddling
 *	checkerboard.
 *
 * @return
 *	Zero on success, otherwise non-zero on error.
 *
 * @note
 *	Use cipher_alphabet_fill() to initialise the 1st row
 *	of the table.
 */
int
cipher_ct_fill(const char *order, cipher_ct table)
{
	size_t ct_size;
	char *freq, *member;
	int i, j, k, freq_len;

	ct_size = strlen(table[0]);

	if (ct_size == sizeof (CT37)-1) {
		freq = FREQUENT7;
		freq_len = sizeof (FREQUENT7)-1;
	} else if (ct_size == sizeof (CT28)-1) {
		freq = FREQUENT8;
		freq_len = sizeof (FREQUENT8)-1;
	} else {
		return errno = EINVAL;
	}

	memset(table[1], ' ', ct_size);
	memset(table[2], ' ', ct_size);

	/* The VIC cipher is a similar to a Huffman encoding
	 * with the seven most frequent letters having a single
	 * digit encoding, and the less frequent letters and
	 * decimal digits having double digit encoding.
	 */

	if (debug) {
		fprintf(
			stderr,
			"Most frequent English letters \"%s\" assigned\n"
			"to columns based on digit order %s.\n",
			freq, order
		);
		fputc('\n', stderr);
	}

	/* Assign single digit code for seven most frequent
	 * letters based on the "frequent" set order.
	 */
	for (i = 0; i < freq_len; i++) {
		member = strchr(table[0], freq[i]);
		table[1][member - table[0]] = order[i];
	}

	/* Assign double digit code for the remaining letters and
	 * digits, based on the blank positions in the "frequent"
	 * string.
	 */
	k = 0;
	for (i = freq_len; i < 10; i++) {
		for (j = 0; j < 10; k++) {
			if (table[1][k] == ' ') {
				table[1][k] = order[i];
				table[2][k] = j+'0';
				j++;
			}
		}
	}

	if (debug) {
		/* http://en.wikipedia.org/wiki/Straddling_checkerboard	*/
		cipher_dump_ct(stderr, table);
		fputc('\n', stderr);
	}

	return errno = 0;
}

/**
 * @param ct_size
 *	The conversion table size, 28 or 37. Used to select
 *	both the alphabet and frequent English letter list.
 *
 * @param key
 *	A C string for the key in the conversion table alphabet.
 *	Used to scramble alphabet order. Can be NULL for the
 *	default English order.
 *
 * @param order
 *	A numeric C string of 10 digits 0 to 9 used as a key
 *	to initialise the conversion table.
 *
 * @param table
 *	A pointer to a cipher_ct, where 1st row is the alphabet
 *	seeded with frequent English letters and a key. The 2nd
 *	and 3rd rows will be initialised with the ASCII digit
 *	codes (or space) for each glyph based on a straddling
 *	checkerboard.
 *
 * @return
 *	Zero on success, otherwise non-zero on error.
 */
int
cipher_ct_init(int ct_size, const char *key, const char *order, cipher_ct table)
{
	if (key == NULL)
		key = "";
	if (order == NULL)
		order = "1234567890";
	if (cipher_alphabet_fill(ct_size, key, table[0]))
		return -1;
	if (cipher_ct_fill(order, table))
		return -1;
	return 0;
}

/**
 * @param table
 *	Conversion table, either CT28 or CT37.
 *
 * @param message
 *	A C string of the message.
 *
 * @return
 *	A dynamic C string of the message converted to a numeric
 *	string. It is the caller's responsibility to free this
 *	memory when done.
 */
char *
cipher_char_to_code(cipher_ct table, const char *message)
{
	int index, i;
	size_t length;
	const char *mp;
	char *op, *out, *glyph;

	/* Make sure the output is large enough to hold
	 * complete 5-digit groups.
	 */
	length = strlen(message) * 2;
	length = (length + 4) / 5 * 5;

	if ((out = malloc(length+1)) == NULL)
		return NULL;

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
		fputc('\n', stderr);
		fprintf(stderr, "Using conversion table convert message to a numeric form.\n\n");
		cipher_dump_grouping(stderr, 2, message);
		fputc('\n', stderr);
		cipher_dump_grouping(stderr, 5, out);
		fputc('\n', stderr);
	}

	return out;
}

/**
 * @param key_mask
 *	A numeric C string.
 *
 * @param out
 *	A numeric C string that is modified in place using
 *	column addition MOD 10.
 */
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
		cipher_dump_grouping(stderr, 5, out);
		fputc('\n', stderr);
	}
}

/**
 * @param table
 *	Conversion table for CT28 or CT37.
 *
 * @param out
 *	A numeric C string that is converted back into an
 *	alpha-numeric string in place.
 */
void
cipher_code_to_char(cipher_ct table, char *out)
{
	int i;
	char *op, *in;
	size_t length;

	if (debug) {
		fprintf(stderr, "Using conversion table reverse the numeric form into a string.\n\n");
		cipher_dump_grouping(stderr, 5, out);
		fputc('\n', stderr);
	}

	length = strlen(table[0]);
	for (op = in = out; *in != '\0'; in++) {
		for (i = 0; i < length; i++) {
			if (*in == table[1][i]) {
				if (table[2][i] == ' ') {
					*op++ = table[0][i];
					break;
				} else if (in[1] == table[2][i]) {
					*op++ = table[0][i];
					in++;
					break;
				}
			}
		}
	}
	*op = '\0';

	if (debug) {
		cipher_dump_grouping(stderr, 2, out);
		fputc('\n', stderr);
	}
}

/**
 * @param alphabet
 *	A C string for a CT28 alphabet, possibly reordered.
 *
 * @param text
 *	A C string of the message text.
 *
 * @return
 *	A dynamic C string where spaces have been converted to
 *	plus-sign (+) and numbers have been converted into to
 *	alpha, using slash (/) as a numeric shift. It is the
 *	responsibility of the caller to free this memory when
 *	done.
 */
char *
cipher_ct28_normalise(const char *alphabet, const char *text)
{
	int is_number;
	size_t length;
	const char *tp;
	char *normalised, *np;

	length = strlen(text);
	length *= 2;

	if ((normalised = malloc(length+1)) == NULL)
		return NULL;

	is_number = 0;
	np = normalised;
	for (tp = text; *tp != '\0'; tp++) {
		if (is_number && !isdigit(*tp)) {
			/* Shift to alpha. */
			is_number = 0;
			*np++ = '/';
		} else if (!is_number && isdigit(*tp)) {
			/* Shift to figures. */
			is_number++;
			*np++ = '/';
		}
		if (is_number)
			/* Use the, possibly scrambled, alphabet to
			 * represent digits. Digits index from the
			 * start of the alphabet.
			 */
			*np++ = alphabet[*tp-'0'];
		else if (isspace(*tp))
			/* Use spare puntatuation for whietspace. */
			*np++ = '+';
		else
			*np++ = *tp;
	}
	*np = '\0';

	if (debug)
		fprintf(stderr, "Normalise string for CT28.\n\n\t%s\n\n", normalised);

	return normalised;
}

/**
 * @param alphabet
 *	A C string for a CT28 alphabet, possibly reordered.
 *
 * @param text
 *	A normalised C string of the message text. The text
 *	is de-normalised, in place, converting plus-sign (+)
 *	to space and slash (/) delimited alpha back to numeric.
 *
 * @return
 *	Return the C string text argument.
 */
char *
cipher_ct28_denormalise(const char *alphabet, char *text)
{
	int is_number;
	char *tp, *glyph;

	is_number = 0;
	for (tp = text; *tp != '\0'; tp++) {
		if (is_number && *tp == '/') {
			is_number = 0;
			*tp = ' ';
			continue;
		} else if (!is_number && *tp == '/') {
			is_number++;
			*tp = ' ';
			continue;
		}
		if (is_number) {
			if ((glyph = strchr(alphabet, toupper(*tp))) == NULL)
				continue;
			*tp = glyph - alphabet + '0';
		} else if (*tp == '+') {
			*tp = ' ';
		}
	}

	if (debug)
		fprintf(stderr, "De-normalise string from CT28.\n\n\t%s\n\n", text);

	return text;
}

/***********************************************************************
 ***
 ***********************************************************************/

#ifdef TEST
typedef struct {
	/* Public */
	size_t ct_size;		/* Conversion table 28 or 37. */
	const char *key;
	const char *seed;
	size_t chain_size;

	/* Private */
	char *chain;
	char order[11];		/* Digit order based on last row of chain table, plus NUL. */
	cipher_ct table;
} Cipher;

const char *
cipher_transponse_key(Cipher *ctx)
{
	char *cp;
	size_t key_len;

	/* Find a column length for the key summing the tail
	 * of the chain addition table until the sum is greater
	 * than 9 (see SECOM).
	 */
	key_len = 0;
	for (cp = ctx->chain + ctx->chain_size-2; ctx->chain <= cp; cp--) {
		if (9 < key_len)
			break;
		key_len += *cp - '0';
	}

	/* Use a key taken from the chain addition table.
	 * Ideally this should be a completely different
	 * chain addition table (see SECOM), but for the
	 * purpose of testing, this will do.
	 */
	cp -= key_len;
	if (cp < ctx->chain)
		cp = ctx->chain;

	return cp;
}

int
cipher_init(Cipher *ctx)
{
	if (ctx->chain_size < 10)
		ctx->chain_size = 100;
	ctx->chain_size++;

	if ((ctx->chain = malloc(ctx->chain_size)) == NULL)
		goto error0;
	if (cipher_chain_add(ctx->seed, ctx->chain, ctx->chain_size))
		goto error1;

	cipher_digit_order(ctx->chain+ctx->chain_size-11, ctx->order);

	return cipher_ct_init(ctx->ct_size, ctx->key, ctx->order, ctx->table);
error1:
	free(ctx->chain);
error0:
	return -1;
}

void
cipher_fini(void *_ctx)
{
	Cipher *ctx = _ctx;

	if (ctx != NULL) {
		free(ctx->chain);
	}
}

char *
cipher_encode(Cipher *ctx, const char *message)
{
	char *out;

	if (ctx->ct_size == 28)
		message = cipher_ct28_normalise(ctx->table[0], message);
	out = cipher_char_to_code(ctx->table, message);
	if (ctx->ct_size == 28)
		free((char *) message);
	if (out == NULL)
		return NULL;
#ifdef TRANSPOSE
{
	const char *transpose_key;
	transpose_key = cipher_transponse_key(ctx);
	message = cipher_simple_transposition_encode(transpose_key, out);
	free(out);
	if (message == NULL)
		return NULL;
	out = message;
}
#else
	cipher_mask_code(ctx->chain, out);
#endif
	return out;
}

char *
cipher_decode(Cipher *ctx, const char *message)
{
	char *out;

#ifdef TRANSPOSE
{
	const char *transpose_key;
	transpose_key = cipher_transponse_key(ctx);
	out = cipher_simple_transposition_decode(transpose_key, message);
}
#else
	cipher_chain_invert(ctx->chain);
	cipher_mask_code(ctx->chain, out);
#endif
	cipher_code_to_char(ctx->table, out);

	if (ctx->ct_size == 28)
		(void) cipher_ct28_denormalise(ctx->table[0], out);

	return out;
}

#include <getopt.h>

static char options[] = "cdvl:";

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
	int ch;
	char *out;
	Cipher ctx;
	cipher_fn fn;

	ctx.ct_size = 28;
	ctx.chain_size = 0;
	fn = cipher_encode;

	while ((ch = getopt(argc, argv, options)) != -1) {
		switch (ch) {
		case 'c':
			ctx.ct_size = 37;
			break;
		case 'd':
			fn = cipher_decode;
			break;
		case 'l':
			ctx.chain_size = strtol(optarg, NULL, 10);
			break;
		case 'v':
			debug++;
			break;
		default:
			fprintf(stderr, usage);
			return EXIT_FAILURE;
		}
	}

	if (argc < optind + 2) {
		fprintf(stderr, "missing key and/or number\n%s", usage);
		return EXIT_FAILURE;
	}

	ctx.key = (const char *) argv[optind];
	ctx.seed = (const char *) argv[optind+1];

	if (cipher_init(&ctx)) {
		fprintf(stderr, "error initialising Cipher structure\n");
		return EXIT_FAILURE;
	}

	if (argv[optind+2] != NULL) {
		if ((out = (*fn)(&ctx, argv[optind+2])) == NULL) {
			fprintf(stderr, "out of memory\n");
			cipher_fini(&ctx);
			return EXIT_FAILURE;
		}

		fprintf(stdout, "%s\n", out);
		free(out);
	} else {
		while (fgets(input, sizeof (input), stdin) != NULL) {
			if ((out = (*fn)(&ctx, input)) == NULL) {
				fprintf(stderr, "out of memory\n");
				cipher_fini(&ctx);
				return EXIT_FAILURE;
			}
			fprintf(stdout, "%s\n", out);
			free(out);
		}
	}

	cipher_fini(&ctx);

	return EXIT_SUCCESS;
}

#endif /* TEST */
