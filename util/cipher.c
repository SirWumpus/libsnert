/*
 * cipher.c
 *
 * An API implementing pen & paper cipher techniques.
 *
 * http://users.telenet.be/d.rijmenants/
 * http://en.wikipedia.org/wiki/VIC_cipher
 * http://www.quadibloc.com/crypto/pp1324.htm
 *
 * Copyright 2010, 2011 by Anthony Howe. All rights reserved.
 */

#define WIPE_MEMORY

#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STRLEN(s)		(sizeof (s)-1)

#ifdef WIPE_MEMORY
#define MEM_WIPE(p, n)		(void) memset(p, 0, n)
#define STR_WIPE(p)		while (*(p) != '\0') *(char *)(p)++ = 0
#else
#define MEM_WIPE(p, n)
#define STR_WIPE(p)
#endif

#if !defined(NUMERIC_SEED)
#define NUMERIC_SEED		"3141592653"	/* PI to 9 decimal places. */
#endif

#ifndef __com_snert_lib_util_cipher_h__
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

/*
 * Note that both CT28 and CT37 alphabets are a subset of the
 * Base64 invariant character set by design. This allows for
 * encrypted messages to appear as though they were Base64
 * encoded.
 */
#define CT28			FREQUENT8 "BCDFGHJKLMPQUVWXYZ+/"
#define CT37			FREQUENT7 "BCDFGHJKLMPQRUVWXYZ0123456789/"

/**
 * 1st row is the alphabet seeded with frequent letters and alphabet.
 * 2nd and 3rd rows are the ASCII digit codes for each glyph in the
 * straddling checkerboard.
 */
typedef char cipher_ct[3][sizeof (CT37)];
#endif

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
	int i, j, k;
	size_t length;

	length = strlen(table[0]);
	fprintf(fp, "CT%lu\n\n\t", (unsigned long)length);

	for (j = 0, k = length/2; j < length; j += k, k = length) {
		for (i = j; i < k; i++)
			fprintf(fp, "%c  ", table[0][i]);
		fprintf(fp, "\n\t");
		for (i = j; i < k; i++)
			fprintf(fp, "%c%c ", table[1][i], table[2][i]);
		fprintf(fp, "\n\t");
	}

	fprintf(fp, "\n\n");
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

	length = strlen(table[0]);
	fprintf(fp, "CT%lu Straddling Checkerboard\n\n\t  ", (unsigned long)length);
	k = length == STRLEN(CT28) ? STRLEN(FREQUENT8) : STRLEN(FREQUENT7);

	for (i = length-10; i < length; i++)
		fprintf(fp, " %c", table[2][i]);
	fprintf(fp, "\n\n\t  ");

	for (i = 0; i < k; i++)
		fprintf(fp, " %c", table[0][i]);

	for (j = k; j < 10; j++, k += 10) {
		fprintf(fp, "\n\t%c ", table[1][k]);
		for (i = 0; i < 10; i++)
			fprintf(fp, " %c", table[0][i+k]);
	}
	fprintf(fp, "\n\n");
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
	fputc('\n', fp);
}

/**
 * @param fp
 *	An output FILE pointer.
 *
 * @param text
 *	A C string to output in space separated groups.
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
	fputc('\n', fp);
}

/**
 * @param fp
 *	An output FILE pointer.
 *
 * @param key
 *	A numeric C string representing the transposition key.
 *
 * @param data
 *	A numeric C string representing the transposition table.
 */
void
cipher_dump_transposition(FILE *fp, const char *key, const char *data)
{
	int col;
	const char *dp;
	size_t key_len;

	if (key == NULL)
		key = NUMERIC_SEED;

	key_len = strlen(key);
	fprintf(fp, "\t%s\n\t", key);
	for (col = 0; col < key_len; col++)
		fputc('-', fp);
	fputc('\n', fp);

	col = 0;
	for (dp = data; *dp != '\0'; dp++) {
		if (col++ == 0)
			fputc('\t', fp);
		fputc(*dp, fp);
		if (key_len <= col) {
			fputc('\n', fp);
			col = 0;
		}
	}

	if (col < key_len)
		fputc('\n', fp);
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
		fprintf(stderr, "Chain Addition seed=%s length=%lu\n\n", seed_number, (unsigned long) size-1);
		cipher_dump_chain(stderr, buffer);
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
	}
}

/**
 * @param in
 *	A C string of up to 255 bytes in length.
 *
 * @return
 *	An output buffer that starts with the length N of the
 *	input string followed by N octets. Each octet in the
 *	output array corresponds to the character set ordering
 *	of the input array.
 *
 * 	Examples assuming ASCII:
 *
 *	    B A B Y L O N 5		input
 *	    2 1 3 7 4 6 5 0		ordinal order
 *
 *	    H E L L O W O R L D		input
 *	    2 1 3 4 6 9 7 8 5 0		ordinal order
 *
 * @see
 *	cipher_index_order()
 */
unsigned char *
cipher_ordinal_order(const char *in)
{
	size_t length;
	const char *ip;
	int octet, count;
	unsigned char *out;

	if (256 <= (length = strlen(in))) {
		errno = EINVAL;
		return NULL;
	}

	if ((out = malloc(length)) == NULL)
		return NULL;

	ip = in;
	count = 0;
	out[0] = length;
	for (octet = 1; count < length; octet++) {
		for (ip = in; *ip != '\0'; ip++) {
			if (*ip == octet)
				out[ip - in + 1] = count++;
		}
	}

	if (debug) {
		fprintf(stderr, "Ordinal Order (length=%u)\n\n\t", out[0]);
		for ( ; in < ip; in++)
			fprintf(stderr, "%c  ", *in);
		fprintf(stderr, "\n\t");
		for (ip = (const char *)out+1, in = ip+length; ip < in; ip++)
			fprintf(stderr, "%02X ", *ip);
		fprintf(stderr, "\n\n");
	}

	return out;
}

/**
 * @param in
 *	A C string of up to 255 bytes in length.
 *
 * @return
 *	An output buffer that starts with the length N of the
 *	input string followed by N octets. Each octet in the
 *	output array contains the index by which the input
 *	string should be read according to the ordinal order
 *	of the input.
 *
 * 	Examples assuming ASCII:
 *
 *	    B A B Y L O N 5		input
 *	    2 1 3 7 4 6 5 0		ordinal order
 *	    7 1 0 2 4 6 5 3		index of ordinal
 *
 *	    H E L L O W O R L D		input
 *	    2 1 3 4 6 9 7 8 5 0 	ordinal order
 *	    9 1 0 2 3 8 4 6 7 5		index of ordinal
 *
 * @see
 *	cipher_ordinal_order()
 */
unsigned char *
cipher_index_order(const char *in)
{
	int octet;
	size_t length;
	const char *ip;
	unsigned char *op, *out;

	if (256 <= (length = strlen(in))) {
		errno = EINVAL;
		return NULL;
	}

	if ((out = malloc(length)) == NULL)
		return NULL;

	ip = in;
	op = out;
	*op++ = length;
	for (octet = 1; op-out <= length; octet++) {
		for (ip = in; *ip != '\0'; ip++) {
			if (*ip == octet)
				*op++ = ip - in;
		}
	}

	if (debug) {
		fprintf(stderr, "Index Order (length=%u)\n\n\t", out[0]);
		for ( ; in < ip; in++)
			fprintf(stderr, "%c  ", *in);
		fprintf(stderr, "\n\t");
		for (ip = (const char *)out+1; ip < (const char *)op; ip++)
			fprintf(stderr, "%02X ", *ip);
		fprintf(stderr, "\n\n");
	}

	return out;
}

/***********************************************************************
 *** Encoding & Decoding Functions
 ***********************************************************************/

static int
columnar_encode(char *out, const char *in, int i, int j)
{
	out[j++] = in[i];
	return j;
}

static int
columnar_decode(char *out, const char *in, int i, int j)
{
	while (isspace(in[j]))
		j++;
	out[i] = in[j++];
	return j;
}

static char *
columnar_transposition(const char *key, const char *in, int (*fn)(char *, const char *, int, int))
{
	char *out;
	int i, x, k;
	unsigned char *indices;
	size_t key_len, out_len;

	if (in == NULL)
		return NULL;
	if (key == NULL)
		key = NUMERIC_SEED;

	if ((indices = cipher_index_order(key)) == NULL)
		return NULL;

	/* Table length without whitespace. */
	for (out_len = 0, out = (char *)in; *out != '\0'; out++)
		out_len += !isspace(*out);

	if ((out = malloc(out_len+1)) != NULL) {
		x = 0;
		key_len = indices[0];
		for (k = 1; k <= key_len; k++) {
			for (i = indices[k]; i < out_len; i += key_len)
				x = (*fn)(out, in, i, x);
		}
		out[out_len] = '\0';
	}

	MEM_WIPE(indices, indices[0]);
	free(indices);

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
 *	simple columnar transposition encoding. It is the caller's
 *	responsibility to free this memory when done.
 */
char *
cipher_columnar_transposition_encode(const char *key, const char *in)
{
	char *out = columnar_transposition(key, in, columnar_encode);
	if (debug) {
		fprintf(stderr, "Columnar Transposition\n\n");
		cipher_dump_transposition(stderr, key, in);
		fprintf(stderr, "Output string:\n\n");
		cipher_dump_grouping(stderr, 5, out);
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
cipher_columnar_transposition_decode(const char *key, const char *in)
{
	char *out = columnar_transposition(key, in, columnar_decode);
	if (debug) {
		fprintf(stderr, "Columnar Transposition\n\n");
		cipher_dump_transposition(stderr, key, out);
		fprintf(stderr, "Output string:\n\n");
		cipher_dump_grouping(stderr, 5, out);
	}
	return out;
}

static int
disrupted_encode(char *out, const char *in, int i, int j)
{
	out[i] = in[j++];
	return j;
}

static int
disrupted_decode(char *out, const char *in, int i, int j)
{
	out[j++] = in[i];
	return j;
}

static char *
disrupted_transposition(const char *key, const char *in, int (*fn)(char *, const char *, int, int))
{
	char *out;
	int i, j, k, r, x;
	unsigned char *indices;
	size_t key_len, out_len;

	if (in == NULL)
		return NULL;
	if (key == NULL)
		key = NUMERIC_SEED;

	if ((indices = cipher_index_order(key)) == NULL)
		return NULL;

	/* Table length without whitespace. */
	for (out_len = 0, out = (char *)in; *out != '\0'; out++)
		out_len += !isspace(*out);

	if ((out = malloc(out_len+1)) != NULL) {
		x = 0;
		out[out_len] = '\0';
		key_len = indices[0];

		/* Create one or more triangles in output table. */
		r = 0;
		for (k = 1; ; k = k % key_len + 1) {
			/* Fill triangle area. */
			for (j = indices[k]; j <= key_len; j++, r += key_len) {
				/* Fill row of triangle. */
				for (i = 0; i < j; i++) {
					if (out_len <= r + i)
						goto stop1;
					x = (*fn)(out, in, r + i, x);
				}
			}
		}
stop1:
		/* Fill in empty triangle space. */
		r = 0;
		for (k = 1; ; k = k % key_len + 1) {
			for (j = indices[k]; j <= key_len; j++, r += key_len) {
				for (i = j; i < key_len; i++) {
					if (out_len <= r + i)
						goto stop2;
					x = (*fn)(out, in, r + i, x);
				}
			}
		}
stop2:
		;
	}

	MEM_WIPE(indices, indices[0]);
	free(indices);

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
	char *out;
	int odebug = debug;

	out = disrupted_transposition(key, in, disrupted_encode);
	if (debug) {
		fprintf(stderr, "Disrupted Transposition\n\n");
		cipher_dump_transposition(stderr, key, out);
	}
	debug = 0;
	out = columnar_transposition(key, in = out, columnar_encode);
	if (odebug) {
		fprintf(stderr, "Output string:\n\n");
		cipher_dump_grouping(stderr, 5, out);
	}
	STR_WIPE(in);
	free((void *)in);
	debug = odebug;

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
 *	disrupted column transposition decoding.
 */
char *
cipher_disrupted_transposition_decode(const char *key, const char *in)
{
	char *out;
	int odebug = debug;

	debug = 0;
	out = columnar_transposition(key, in, columnar_decode);
	debug = odebug;
	out = disrupted_transposition(key, in = out, disrupted_decode);
	if (debug) {
		fprintf(stderr, "Disrupted Transposition\n\n");
		cipher_dump_transposition(stderr, key, in);
		fprintf(stderr, "Output string:\n\n");
		cipher_dump_grouping(stderr, 5, out);
	}
	STR_WIPE(in);
	free((void *)in);

	return out;
}

/**
 * @param ct_size
 *	The conversion table size, 28 or 37. Used to select
 *	both the alphabet and frequent English letter list.
 *
 * @param order
 *	An array of 11 octets; a length (10) followed by 10
 *	octets specifying an ordinal ordering.
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
 * @see
 *	cipher_ordinal_order()
 */
int
cipher_ct_init(int ct_size, const unsigned char *order, cipher_ct table)
{
	int i, j, k, l;

	if (table == NULL)
		return errno = EFAULT;
	if (order == NULL)
		order = (unsigned char *)"\012\001\002\003\004\005\006\007\010\011\0";
	if (order[0] < 10)
		return errno = EINVAL;
	order++;

	if (ct_size == STRLEN(CT37)) {
		l = STRLEN(FREQUENT7);
		(void) strcpy(table[0], CT37);
	} else if (ct_size == STRLEN(CT28)) {
		l = STRLEN(FREQUENT8);
		(void) strcpy(table[0], CT28);
	} else {
		return errno = EINVAL;
	}

	/* A straddling checkerboard is similar to a Huffman
	 * encoding, where the most frequent glyphs having a
	 * single digit encoding and the less frequent glyphs
	 * have a double digit encoding.
	 *
	 * The SECOM cipher, using CT37, builds the checkerboard
	 * specifying the 1st row with some predefined ordering
	 * of 10 unique digits 1..9 0. In the 2nd row columns 3,
	 * 6, and 9 are the blank columns and "ESTONIA" fills the
	 * non-blank columns. The digits above the blank columns
	 * are written down the left hand side of the table. The
	 * remaining CT37 alphabet fills the remaining rows,
	 * starting at the column given by the digit on the left
	 * hand side, left to right, and wrapping around, before
	 * repeating the procedure for the remaining two rows.
	 *
	 * For example:
	 *
	 *        9 4 8 5 2 1 3 0 6 7	(predefined ordering)
	 *	  E S   T O   N I   A
	 *	8 F G H J K L M B C D
	 *	1 P Q R U V W X Y Z /
	 *      6 6 7 8 9 0 1 2 3 4 5
	 *
	 * Our version differs in several ways. First we count as
	 * programmers do from 0..9 instead 1..9 0 (ie. 1 to 10);
	 * second the blank columns are assigned after "ESTONIA";
	 * third we simply write the remaining alphabet into the
	 * remaining rows.
	 *
	 * For example:
	 *
	 *        9 4 8 5 2 1 3 0 6 7	(predefined ordering)
	 *	  E S T O N I A
	 *	0 B C D F G H J K L M
	 *	6 P Q R U V W X Y Z 0
	 *	7 1 2 3 4 5 6 7 8 9 /
	 *
	 * This variant is simpler to do by hand, simpler to code,
	 * and yields an equally scrambled alphabet encoding.
	 *
	 * The straddling checkerboard is intended to "fractionate"
	 * an alphabet to hide the most frequently occuring letters
	 * similar to Huffman encodings used in compression.
	 *
	 * A straddling checkerboard is simply a subsitution cipher
	 * converting an alpha-numeric message into a purely numeric
	 * form and should not be relied on as the sole method of
	 * encryption.
	 *
	 * Following the conversion of a message into a numeric form,
	 * the SECOM cipher apply columnar and disrupted transpositions
	 * on the message using a predefined set of rules to determine
	 * the transpositions keys.
	 *
	 * Modern "pen & paper" field ciphers will use a subsitution
	 * with one or more transpositions. The "keys" in combination
	 * with transformation rules provide the necessary setup.
	 */

	/* Single digit code for the most frequent letters. */
	for (i = 0; i < l; i++) {
		table[1][i] = order[i]+'0';
		table[2][i] = ' ';
	}

	/* Double digit code for remaining letters and digits. */
	for (i = 0, j = l; j < 10; i += 10, j++) {
		for (k = 0; k < 10; k++) {
			table[1][l+i+k] = order[j]+'0';
			table[2][l+i+k] = order[k]+'0';
		}
	}
	table[1][ct_size] = table[1][ct_size] = '\0';

	if (debug) {
		/* http://en.wikipedia.org/wiki/Straddling_checkerboard	*/
		cipher_dump_ct(stderr, table);
		cipher_dump_alphabet(stderr, table);
	}

	return errno = 0;
}

/**
 * @param table
 *	Conversion table, either CT28 or CT37.
 *
 * @param in
 *	A C string of the message.
 *
 * @return
 *	A dynamic C string of the message converted to a numeric
 *	string. It is the caller's responsibility to free this
 *	memory when done.
 */
char *
cipher_char_to_code(cipher_ct table, const char *in)
{
	int i;
	size_t length;
	const char *ip;
	char *op, *out, *glyph;

	if (in == NULL)
		return NULL;

	/* Make sure the output is large enough to hold
	 * complete 5-digit groups.
	 */
	length = strlen(in) * 2;
	length = (length + 4) / 5 * 5;

	if ((out = malloc(length+1)) == NULL)
		return NULL;

	for (op = out, ip = in; *ip != '\0'; ip++) {
		if ((glyph = strchr(table[0], toupper(*ip))) == NULL)
			continue;

		i = glyph - table[0];

		*op++ = table[1][i];
		if (table[2][i] != ' ')
			*op++ = table[2][i];
	}

	for (i = (op - out) % 5; 0 < i && i < 5; i++)
		*op++ = '0';

	*op = '\0';

	if (debug) {
		fprintf(stderr, "To numeric:\n\n");
		cipher_dump_grouping(stderr, 2, in);
		cipher_dump_grouping(stderr, 5, out);
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
		fprintf(stderr, "Column Addition MOD 10:\n\n");
		cipher_dump_grouping(stderr, 5, out);
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
		fprintf(stderr, "From numeric:\n\n");
		cipher_dump_grouping(stderr, 2, out);
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
//			*tp = ' ';
			continue;
		} else if (!is_number && *tp == '/') {
			is_number++;
//			*tp = ' ';
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
	size_t chain_length;

	/* Private */
	char *chain;
	unsigned char *ordinal;
	cipher_ct table;
} Cipher;

char *
cipher_transponse_key(Cipher *ctx, size_t *offset)
{
	char *cp, *key;
	size_t key_len;

	/* Find a column length for the key summing the tail
	 * of the chain addition table until the sum is greater
	 * than 9 (see SECOM).
	 */
	key_len = 0;
	for (cp = ctx->chain + *offset-1; ctx->chain <= cp; cp--) {
		key_len += *cp - '0';
		if (9 < key_len)
			break;
	}

	if ((key = malloc(key_len+1)) == NULL)
		return NULL;

	/* Use a key taken from the chain addition table.
	 * Ideally this should be a completely different
	 * chain addition table (see SECOM), but for the
	 * purpose of testing, this will do.
	 */
	cp -= key_len;
	if (cp < ctx->chain)
		cp = ctx->chain;

	*offset = cp - ctx->chain;
	(void) memcpy(key, cp, key_len);
	key[key_len] = '\0';

	return key;
}

int
cipher_init(Cipher *ctx)
{
	if (ctx->chain_length < 10)
		ctx->chain_length = 10;

	if ((ctx->chain = malloc(ctx->chain_length+1)) == NULL)
		goto error0;
	if (cipher_chain_add(ctx->seed, ctx->chain, ctx->chain_length+1))
		goto error1;
	if ((ctx->ordinal = cipher_ordinal_order(ctx->chain+ctx->chain_length-10)) == NULL)
		goto error2;
	return cipher_ct_init(ctx->ct_size, ctx->ordinal, ctx->table);
error2:
	STR_WIPE(ctx->chain);
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
		if (ctx->key != NULL)
			STR_WIPE(ctx->key);
		STR_WIPE(ctx->seed);
		MEM_WIPE(ctx->table, sizeof (ctx->table));
		MEM_WIPE(ctx->ordinal, *ctx->ordinal);
		free(ctx->ordinal);
		STR_WIPE(ctx->chain);
		free(ctx->chain);
	}
}

char *
cipher_encode(Cipher *ctx, const char *message)
{
	size_t offset;
	char *out, *tkey;

	if (ctx->ct_size == 28)
		message = cipher_ct28_normalise(ctx->table[0], message);
	out = cipher_char_to_code(ctx->table, message);
	if (ctx->ct_size == 28)
		free((void *) message);

	offset = ctx->chain_length;
	tkey = cipher_transponse_key(ctx, &offset);
	out = cipher_columnar_transposition_encode(tkey, message = out);

	STR_WIPE(message);
	free((void *)message);
	STR_WIPE(tkey);
	free(tkey);

	tkey = cipher_transponse_key(ctx, &offset);
	out = cipher_disrupted_transposition_encode(tkey, message = out);

	STR_WIPE(message);
	free((void *)message);
	STR_WIPE(tkey);
	free(tkey);

	return out;
}

char *
cipher_decode(Cipher *ctx, const char *message)
{
	size_t offset;
	char *out, *tk1, *tk2;

	offset = ctx->chain_length;
	tk1 = cipher_transponse_key(ctx, &offset);
	tk2 = cipher_transponse_key(ctx, &offset);

	out = cipher_disrupted_transposition_decode(tk2, message);
	out = cipher_columnar_transposition_decode(tk1, message = out);
	STR_WIPE(message);
	cipher_code_to_char(ctx->table, out);

	free((void *)message);
	STR_WIPE(tk2);
	free(tk2);
	STR_WIPE(tk1);
	free(tk1);

	if (ctx->ct_size == 28)
		(void) cipher_ct28_denormalise(ctx->table[0], out);

	return out;
}

#include <getopt.h>

static char options[] = "cdvk:l:s:IT:U:";

static char usage[] =
"usage: cipher [-cdv][-l length][-s seed] . | < message\n"
"       cipher -I string\n"
"       cipher -T c|d [-k key] string\n"
"       cipher -U c|d [-k key] string\n"
"\n"
"-c\t\tuse conversion table 28; default 37\n"
"-d\t\tdecode message\n"
"-k key\t\talpha-numeric transpostion key\n"
"-l length\tchain addition length; default 100\n"
"-s seed\t\tnumeric seed for chain addition; default " NUMERIC_SEED "\n"
"-v\t\tverbose debug\n"
"\n"
"-I\t\tdump the indices of the ordinal order of characters\n"
"-T c|d\t\tdump the encoded columnar or disrupted transposition\n"
"-U c|d\t\tdump the decoded columnar or disrupted transposition\n"
"\n"
"Copyright 2010, 2011 by Anthony Howe.  All rights reserved.\n"
;

typedef char *(*cipher_fn)(Cipher *, const char *);
typedef char *(*transpose_fn)(const char *, const char *);

static char input[BUFSIZ];

int
main(int argc, char **argv)
{
	char *out;
	Cipher ctx;
	cipher_fn fn;
	transpose_fn tf;
	int ch, dump, transposition;

	memset(&ctx, 0, sizeof (ctx));

	dump = 0;
	ctx.ct_size = 37;
	ctx.chain_length = 100;
	fn = cipher_encode;
	transposition = 0;

	while ((ch = getopt(argc, argv, options)) != -1) {
		switch (ch) {
		case 'c':
			ctx.ct_size = 28;
			break;
		case 'd':
			fn = cipher_decode;
			break;
		case 'k':
			ctx.key = optarg;
			break;
		case 'l':
			ctx.chain_length = strtol(optarg, NULL, 10);
			break;
		case 's':
			ctx.seed = optarg;
			break;
		case 'v':
			debug++;
			break;
		case 'T': case 'U':
			dump = ch;
			transposition = *optarg;
			if (transposition == 'c' || transposition == 'd')
				break;
			/*@fallthrough@*/
		case 'I':
			if (dump == 0) {
				dump = ch;
				break;
			}
			/*@fallthrough@*/
		default:
			fprintf(stderr, usage);
			return EXIT_FAILURE;
		}
	}

	switch (dump) {
	case 'I':
		debug = 1;
		free(cipher_ordinal_order(argv[optind]));
		free(cipher_index_order(argv[optind]));
		break;
	case 'T':
		debug = 1;
		tf = transposition == 'c'
			? cipher_columnar_transposition_encode
			: cipher_disrupted_transposition_encode
		;
		free((*tf)(ctx.key, argv[optind]));
		break;
	case 'U':
		debug = 1;
		tf = transposition == 'c'
			? cipher_columnar_transposition_decode
			: cipher_disrupted_transposition_decode
		;
		out = (*tf)(ctx.key, argv[optind]);
		fprintf(stderr, "%s\n", out);
		free(out);
		break;
	default:
		if (cipher_init(&ctx)) {
			fprintf(stderr, "error initialising Cipher structure\n");
			return EXIT_FAILURE;
		}

		if (optind < argc && argv[optind][0] == '.')
			break;

		while (0 < fread(input, 1, sizeof (input), stdin)) {
			if ((out = (*fn)(&ctx, input)) == NULL) {
				cipher_fini(&ctx);
				return EXIT_FAILURE;
			}
			MEM_WIPE(input, sizeof (input));
			printf("%s\n", out);
			STR_WIPE(out);
			free(out);
		}
	}

	cipher_fini(&ctx);

	return EXIT_SUCCESS;
}

#endif /* TEST */
