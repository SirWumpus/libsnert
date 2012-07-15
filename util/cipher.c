/*
 * cipher.c
 *
 * An API implementing pen & paper cipher techniques.
 *
 * http://users.telenet.be/d.rijmenants/
 * http://en.wikipedia.org/wiki/VIC_cipher
 * http://www.quadibloc.com/crypto/pp1324.htm
 *
 * Copyright 2010, 2012 by Anthony Howe. All rights reserved.
 */

#define WIPE_MEMORY

#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STRLEN(s)		(sizeof (s)-1)
#define QUOTE(x)		QUOTE_THIS(x)
#define QUOTE_THIS(x)		#x

#ifdef WIPE_MEMORY
#define MEM_WIPE(p, n)		if ((p) != NULL) (void) memset((p), 0, n)
#define STR_WIPE(p)		if ((p) != NULL) { char *_p = (char *)(p); while (*_p != '\0') *_p++ = 0; }
#else
#define MEM_WIPE(p, n)
#define STR_WIPE(p)
#endif

#ifndef DEFAULT_CT
#define DEFAULT_CT		46
#endif

#ifndef NUMERIC_SEED
#define NUMERIC_SEED		"3141592653"	/* PI to 9 decimal places. */
#endif

#ifndef TAB_WIDTH
#define TAB_WIDTH		8
#endif

#ifndef GROUP_WIDTH
#define GROUP_WIDTH		5
#endif

#ifndef OUTPUT_WIDTH
#define OUTPUT_WIDTH		(50+50/GROUP_WIDTH)
#endif

#ifndef BLOCK_SIZE
#define BLOCK_SIZE		500
#endif

/*
 * CT-28
 *
 * The actual straddling checkerboard codes used are determined
 * by the key specification rules applied.
 *
 *		0  1  2  3  4  5  6  7  8  9
 *		S  E  N  O  R  I  T  A
 *	     8  B  C  D  F  G  H  J  K  L  M
 *	     9  P  Q  U  V  W  X  Y  Z  +  /
 */
#define FREQUENT8		"SENORITA"
#define CT28			FREQUENT8 "BCDFGHJKLMPQUVWXYZ+/"

/*
 * CT-37
 *
 * The actual straddling checkerboard codes used are determined
 * by the key specification rules applied.
 *
 *		0  1  2  3  4  5  6  7  8  9
 *		E  S  T  O  N  I  A
 *	     7  B  C  D  F  G  H  J  K  L  M
 *	     8  P  Q  R  U  V  W  X  Y  Z  /
 *	     9  0  1  2  3  4  5  6  7  8  9
 */
#define FREQUENT7		"ESTONIA"
#define CT37			FREQUENT7 "BCDFGHJKLMPQRUVWXYZ0123456789/"

/*
 * Based on reading of other Checkerboard variants and previous
 * CT28 "normalisation" idea for digits, these are alternative
 * conversion tables that allow for more variety of characters.
 */

/*
 * CT-28 ASCII Subset
 *
 * The initial shift state is the SI table. The original source
 * messsage does not specify SI or SO; they are added during
 * encryption and removed during decryption.
 *
 * The actual straddling checkerboard codes used are determined
 * by the key specification rules applied.
 *
 *	SI	0  1  2  3  4  5  6  7  8  9
 *		A  T  O  E  I  N  SP SO
 *	     8  B  C  D  F  G  H  J  K  L  M
 *	     9  P  Q  R  S  U  V  W  X  Y  Z
 *
 *
 *	SO	0  1  2  3  4  5  6  7  8  9
 *		.  ,  ;  :  ?  "  @  SI
 *	     8  (  )  *  /  +  -  <  =  >  %	(BOMDAS, relations, percent)
 *	     9  0  1  2  3  4  5  6  7  8  9
 */
#define FREQUENT8_SI		"ATOEIN \016"
#define CT28_SI			FREQUENT8_SI "BCDFGHJKLMPQRSUVWXYZ"

#define FREQUENT8_SO		".,;:?\"@\017"
#define CT28_SO			FREQUENT8_SO "()*/+-<=>%0123456789"

/*
 * CT-37 Printable ASCII
 *
 * Non-standard variant on CT37 that allows for the encryption of
 * printable ASCII text files using shift in/out and prefix codes.
 * This set of tables cover the full set of printable ASCII and
 * common white space.
 *
 * The initial shift state is the SI table. The original source
 * file does not specify SI, SO, or SU; they are added during
 * encryption and removed during decryption.
 *
 * The actual straddling checkerboard codes used are determined
 * by the key specification rules applied.
 *
 *
 *	SI	0  1  2  3  4  5  6  7  8  9
 *		E  S  T  O  N  I  A
 *	     7  B  C  D  F  G  H  J  K  L  M
 *	     8  P  Q  R  U  V  W  X  Y  Z  SO
 *	     9  SP HT CR LF .  ,  ;  :  ?  SU
 *
 *
 *	SO	0  1  2  3  4  5  6  7  8  9
 *		e  s  t  o  n  i  a
 *	     7  b  c  d  f  g  h  j  k  l  m
 *	     8  p  q  r  u  v  w  x  y  z  SI
 *	     9  SP HT CR LF .  ,  ;  :  ?  SU
 *
 *
 *	SU	0  1  2  3  4  5  6  7  8  9
 *		!  "  #  &  '  @  |
 *	     7  (  )  *  /  +  -  <  =  >  %	(BOMDAS, relations, percent)
 *	     8  {  }  ^  \  `  _  [  ~  ]  $
 *	     9  0  1  2  3  4  5  6  7  8  9
 */
#define FREQUENT7_SI		"ESTONIA"
#define CT37_SI			FREQUENT7_SI "BCDFGHJKLMPQRUVWXYZ\016 \t\r\n.,;:?\032"

#define FREQUENT7_SO		"estonia"
#define CT37_SO			FREQUENT7_SO "bcdfghjklmpqruvwxyz\017 \t\r\n.,;:?\032"

#define FREQUENT7_SU		"!\"#&'@|"
#define CT37_SU			FREQUENT7_SU "()*/+-<=>%{}^\\`_[~]$0123456789"

/*
 * CT-46
 *
 * The actual straddling checkerboard codes used are determined
 * by the key specification rules applied.
 *
 *
 *		0  1  2  3  4  5  6  7  8  9
 *		R  E  A  N  O  I
 *	     6  B  C  D  F  G  H  J  K  L  M
 *	     7  P  Q  S  T  U  V  W  X  Y  Z
 *	     8  SP .  ,  :  ?  /  (  )  "  #
 *	     9  0  1  2  3  4  5  6  7  8  9
 *
 * In the CT46 described by Dirk Rijmenants, the hash-sign (#) is 
 * actually a CODE prefix used to replace common words or phrases.
 * It would work similarly to how SU is used in the CT37 Printable
 * ASCII. Here we simply treat it as another character.
 */
#define FREQUENT6		"REANOI"
#define CT46			FREQUENT6 "BCDFGHJKLMPQSTUVWXYZ .,:?/()\"#0123456789"

#define FREQUENT6_ALT		"ATOEIN"
#define CT46_ALT		FREQUENT6_ALT "BCDFGHJKLMPQRSUVWXYZ .,:?/()\"#0123456789"

typedef struct {
	const char *name;
	int length;
	int freq_length;
	int shift[3];		/* 0 = SI, 1 = SO, 2 = SU */
	const char *ct[3];	/* 0 = SI, 1 = SO, 2 = SU */
	char code[2][47];	/* Two ASCII digit strings of CT length. */
} cipher_ct;

static const char empty[] = "";

static const char ct28_name[] = "CT28";
static cipher_ct ct28 = {
	ct28_name, STRLEN(CT28), STRLEN(FREQUENT8),
	{ -1, -1, -1 }, { CT28, empty, empty }
};

static const char ct37_name[] = "CT37";
static cipher_ct ct37 = {
	ct37_name, STRLEN(CT37), STRLEN(FREQUENT7),
	{ -1, -1, -1 }, { CT37, empty, empty }
};

static const char ct46_name[] = "CT46";
static cipher_ct ct46 = {
	ct46_name, STRLEN(CT46), STRLEN(FREQUENT6),
	{ -1, -1, -1 }, { CT46, empty, empty }
};

static const char ct28_ascii_name[] = "CT28 ASCII Subset";
static cipher_ct ct28_ascii = {
	ct28_ascii_name, STRLEN(CT28_SI), STRLEN(FREQUENT8_SI),
	{ 7, 7, -1 }, { CT28_SI, CT28_SO, empty }
};

static const char ct37_ascii_name[] = "CT37 Printable ASCII";
static cipher_ct ct37_ascii = {
	ct37_ascii_name, STRLEN(CT37_SI), STRLEN(FREQUENT7_SI),
	{ 26, 26, 36 }, { CT37_SI, CT37_SO, CT37_SU }
};

static int ct_sizes[] = { 28, 37, 46, 56, 111, 0 };

/***********************************************************************
 *** Dump Functions
 ***********************************************************************/

static int debug;

static char *ascii_control[] = {
	"NU", "SH", "SX", "EX", "ET", "EQ", "AK", "BL",
	"BS", "HT", "LF", "VT", "FF", "CR", "SO", "SI",
	"DE", "D1", "D2", "D3", "D4", "NK", "SY", "EB",
	"CA", "EM", "SU", "ES", "FS", "GS", "RS", "US",
	"SP"
};

static void
cipher_dump_codes(FILE *fp, cipher_ct *table, int shift)
{
	int i, j, k;

	for (j = 0, k = table->length/2; j < table->length; j += k, k = table->length) {
		for (i = j; i < k; i++) {
			if (table->ct[shift][i] <= 32)
				fprintf(fp, "%s ", ascii_control[table->ct[shift][i]]);
			else
				fprintf(fp, "%c  ", table->ct[shift][i]);
		}
		fprintf(fp, "\n\t");
		for (i = j; i < k; i++)
			fprintf(fp, "%c%c ", table->code[0][i], table->code[1][i]);
		fprintf(fp, "\n\t");
	}

	fputc('\n', fp);
}

/**
 * @param fp
 *	An output FILE pointer.
 *
 * @param table
 *	A conversion table for CT28 or CT37.
 */
void
cipher_dump_alphabet(FILE *fp, cipher_ct *table)
{
	fprintf(fp, "%s\n\n", table->name);

	if (0 <= table->shift[0])
		fprintf(fp, "SI");
	fputc('\t', fp);
	cipher_dump_codes(fp, table, 0);

	if (0 <= table->shift[1]) {
		fprintf(fp, "\nSO\t");
		cipher_dump_codes(fp, table, 1);
	}

	if (0 <= table->shift[2]) {
		fprintf(fp, "\nSU\t");
		cipher_dump_codes(fp, table, 2);
	}

	fputc('\n', fp);
}

static void
cipher_dump_table(FILE *fp, cipher_ct *table, int shift)
{
	int i, j, k;

	k = table->freq_length;

	for (i = table->length-10; i < table->length; i++)
		fprintf(fp, " %c ", table->code[1][i]);
	fprintf(fp, "\n\n\t   ");

	for (i = 0; i < k; i++) {
		if (table->ct[shift][i] <= 32)
			fprintf(fp, " %s", ascii_control[table->ct[shift][i]]);
		else
			fprintf(fp, " %c ", table->ct[shift][i]);
	}

	for (j = k; j < 10; j++, k += 10) {
		fprintf(fp, "\n\t%c  ", table->code[0][k]);
		for (i = 0; i < 10; i++) {
			if (table->ct[shift][i+k] <= 32)
				fprintf(fp, " %s", ascii_control[table->ct[shift][i+k]]);
			else
				fprintf(fp, " %c ", table->ct[shift][i+k]);
		}
	}

	fputc('\n', fp);
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
cipher_dump_ct(FILE *fp, cipher_ct *table)
{
	fprintf(fp, "%s Straddling Checkerboard\n\n", table->name);

	if (0 <= table->shift[0])
		fprintf(fp, "SI");
	fprintf(fp, "\t   ");
	cipher_dump_table(fp, table, 0);

	if (0 <= table->shift[1]) {
		fprintf(fp, "\nSO\t   ");
		cipher_dump_table(fp, table, 1);
	}

	if (0 <= table->shift[2]) {
		fprintf(fp, "\nSU\t   ");
		cipher_dump_table(fp, table, 2);
	}

	fprintf(fp, "\n");
}

void
cipher_dump_indent(FILE *fp, int width)
{
	int n, col = 0;

	for (n = width / 8; 0 < n--; ) {
		fputc('\t', fp);
		col += 8;
	}
	for (n = width % 8; 0 < n--; ) {
		fputc(' ', fp);
		col++;
	}
}

/**
 * @param fp
 *	An output FILE pointer.
 *
 * @param text
 *	A C string to output in space separated groups.
 */
void
cipher_dump_grouped(FILE *fp, int grouping, int indent, int width, const char *text)
{
	int group, col;

	if (grouping < 1)
		grouping = 1;

	while (*text != '\0') {
		group = 0;
		cipher_dump_indent(fp, indent);
		for (col = 0; col < width && *text != '\0'; text++) {
			fputc(isspace(*text) ? '_' : *text, fp);
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
 * @param text
 *	A C string to output in space separated groups.
 */
void
cipher_dump_grouping(FILE *fp, int grouping, const char *text)
{
	cipher_dump_grouped(fp, grouping, TAB_WIDTH, OUTPUT_WIDTH, text);
	fputc('\n', fp);
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
	cipher_dump_grouped(fp, GROUP_WIDTH, TAB_WIDTH, (10+10/GROUP_WIDTH), chain);
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
	size_t key_len;

	if (key == NULL)
		key = NUMERIC_SEED;

	key_len = strlen(key);
	key_len += key_len / GROUP_WIDTH;
	cipher_dump_grouped(fp, GROUP_WIDTH, TAB_WIDTH, key_len, key);

	fputc('\t', fp);
	for (col = 0; col < key_len; col++)
		fputc('-', fp);
	fputc('\n', fp);

	cipher_dump_grouped(fp, GROUP_WIDTH, TAB_WIDTH, key_len, data);
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

	if ((out = malloc(length+1)) == NULL)
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

	if ((out = malloc(length+1)) == NULL)
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
		cipher_dump_grouping(stderr, GROUP_WIDTH, out);
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
		cipher_dump_grouping(stderr, GROUP_WIDTH, out);
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
		cipher_dump_grouping(stderr, GROUP_WIDTH, out);
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
		cipher_dump_grouping(stderr, GROUP_WIDTH, out);
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
cipher_ct_init(int ct_size, const unsigned char *order, cipher_ct *table)
{
	int i, j, k, l;

	if (table == NULL)
		return errno = EFAULT;
	if (order == NULL)
		order = (unsigned char *)"\012\001\002\003\004\005\006\007\010\011\0";
	if (order[0] < 10)
		return errno = EINVAL;
	order++;

	switch (ct_size) {
	case 28:
		*table = ct28;
		break;
	case 2*28:
		*table = ct28_ascii;
		break;
	case 37:
		*table = ct37;
		break;
	case 3*37:
		*table = ct37_ascii;
		break;
	case 46:
		*table = ct46;
		break;
	default:
		return errno = EINVAL;
	}

	memset(table->code, 0, sizeof (table->code));
	l = table->freq_length;

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
		table->code[0][i] = order[i]+'0';
		table->code[1][i] = ' ';
	}

	/* Double digit code for remaining letters and digits. */
	for (i = 0, j = l; j < 10; i += 10, j++) {
		for (k = 0; k < 10; k++) {
			table->code[0][l+i+k] = order[j]+'0';
			table->code[1][l+i+k] = order[k]+'0';
		}
	}

	if (debug) {
		/* http://en.wikipedia.org/wiki/Straddling_checkerboard	*/
		cipher_dump_ct(stderr, table);
		cipher_dump_alphabet(stderr, table);
	}

	return errno = 0;
}

static void
strupper(char *s)
{
	int ch;

	while (*s != '\0') {
		ch = (char) toupper(*s);
		*s++ = ch;
	}
}

/**
 * @param table
 *	Conversion table.
 *
 * @param ch
 *	An ASCII character code.
 *
 * @param out
 *	A three byte buffer in which to write an optional shift/escape 
 *	code and encoded character. 
 *
 * @param shift
 *	A pointer to the current shift state. The initial shift state 
 *	is zero (0).
 *
 * @return
 *	The number of bytes written to the output buffer (0..3).
 */
int
cipher_char_to_ct(cipher_ct *table, int ch, char out[3], int *shift)
{
	char *glyph;
	int i, x, offset;

	if (table->name != ct37_ascii_name)
		ch = toupper(ch);

	if ((glyph = strchr(table->ct[*shift], ch)) != NULL) {
		/* No shift change. */
		i = 0;
		offset = glyph - table->ct[*shift];
	} else if ((glyph = strchr(table->ct[!*shift], ch)) != NULL) {
		/* Invert shift state. */
		*shift = !*shift;
		i = table->shift[*shift];
		offset = glyph - table->ct[*shift];
	} else if ((glyph = strchr(table->ct[2], ch)) != NULL) {
		/* Escape prefix. */
		i = table->shift[2];
		offset = glyph - table->ct[2];
	} else {
		/* Ignore characters not found in the alphabet. */
		return 0;
	}

	/* Emit shift/escape code? */
	x = 0;
	if (0 < i) {
		out[x++] = table->code[0][i];
		if (table->code[1][i] != ' ')
			out[x++] = table->code[1][i];
	}

	/* Emit character's code. */
	out[x++] = table->code[0][offset];
	if (table->code[1][offset] != ' ')
		out[x++] = table->code[1][offset];

	return x;
}

/**
 * @param table
 *	Conversion table.
 *
 * @param in
 *	A C string of the message.
 *
 * @param pad
 *	Pad message out to a full 5 digit group.
 *
 * @return
 *	A dynamic C string of the message converted to a numeric
 *	string. It is the caller's responsibility to free this
 *	memory when done.
 */
char *
cipher_string_to_ct(cipher_ct *table, const char *in, int pad)
{
	size_t length;
	const char *ip;
	int i, shift, x;
	char *out, *copy;

	if (in == NULL)
		return NULL;

	length = strlen(in);
	if ((out = malloc(length+1)) == NULL)
		return NULL;

	if (table->name != ct37_ascii_name)
		strupper((char *) in);

	shift = 0;
	for (x = 0, ip = in; *ip != '\0'; ip++) {
		/* Enough room for a shift, code, and null byte? */
		if (length <= x+GROUP_WIDTH) {
			if ((copy = realloc(out, length += 1000)) == NULL) {
				free(out);
				return NULL;
			}
			out = copy;
		}

		x += cipher_char_to_ct(table, *ip, out+x, &shift);
	}

	if (pad && 0 < x % GROUP_WIDTH) {
		if (length <= x+GROUP_WIDTH && (out = realloc(out, length += 6)) == NULL) {
			free(out);
			return NULL;
		}

		for (i = GROUP_WIDTH - x % GROUP_WIDTH; 0 < i; i--)
			out[x++] = '0';
	}

	out[x] = '\0';

	if (debug) {
		fprintf(stderr, "To numeric:\n\n");
		cipher_dump_grouping(stderr, GROUP_WIDTH, in);
		cipher_dump_grouping(stderr, GROUP_WIDTH, out);
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
		cipher_dump_grouping(stderr, GROUP_WIDTH, out);
	}
}

/**
 * @param table
 *	Conversion table.
 *
 * @param out
 *	A numeric C string that is converted back into an
 *	alpha-numeric string in place.
 */
void
cipher_ct_to_string(cipher_ct *table, char *out)
{
	char *op, *in;
	int i, shift, last_shift;

	shift = last_shift = 0;
	for (op = in = out; *in != '\0'; in++) {
		if (isspace(*in))
			continue;
		for (i = 0; i < table->length; i++) {
			if (*in == table->code[0][i]) {
				if (in[1] == table->code[1][i]) {
					in++;
				} else if (table->code[1][i] == ' ') {
					;
				} else {
					continue;
				}

				if (shift < 2 && i == table->shift[shift]) {
					/* Invert shift state, discard code. */
					shift = last_shift = !shift;
				} else if (shift < 2 && i == table->shift[2]) {
					/* Discard escape prefix. */
					last_shift = shift;
					shift = 2;
				} else {
					/* Covert code to character. */
					*op++ = table->ct[shift][i];
					/* Restore previous shift after escape. */
					shift = last_shift;
				}
				break;
			}
		}
	}
	*op = '\0';

	if (debug) {
		fprintf(stderr, "From numeric:\n\n");
		cipher_dump_grouping(stderr, GROUP_WIDTH, out);
	}
}

/***********************************************************************
 ***
 ***********************************************************************/

#ifdef TEST

static const char base36[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const char ordinal[] = "\012\000\001\002\003\004\005\006\007\010\011";

typedef struct {
	/* Public */
	size_t ct_size;		/* Conversion table 28 or 37. */
	const char *key;
	size_t min_columns;
	size_t chain_length;

	/* Private */
	char *chain;
	unsigned char *ordinal;
	cipher_ct table;
	int shift;
} Cipher;

char *
cipher_transponse_key(Cipher *ctx, size_t *offset)
{
	char *cp, *key;
	size_t key_len;

	if (*offset < ctx->min_columns)
		*offset = ctx->min_columns;

	/* Find a column length for the key summing the tail
	 * of the chain addition table until the sum is greater
	 * than 9 (see SECOM).
	 */
	key_len = 0;
	for (cp = ctx->chain + *offset-1; ctx->chain <= cp; cp--) {
		key_len += *cp - '0';
		if (ctx->min_columns < key_len)
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
	char *k, *digit;

	if (ctx->min_columns < 10)
		ctx->min_columns = 10;

	if (ctx->chain_length < ctx->min_columns)
		ctx->chain_length = ctx->min_columns;

	/* Convert alpha-numeric key into ASCII digit form for chain addition. */
	if (ctx->key != NULL) {
		for (k = (char *)ctx->key; *k != '\0'; k++) {
			if ((digit = strchr(base36, toupper(*k))) == NULL)
				goto error0;
			*k = (digit - base36) % 10 + '0';
		}
	}

	if ((ctx->chain = malloc(ctx->chain_length+1)) == NULL)
		goto error0;
	if (cipher_chain_add(ctx->key, ctx->chain, ctx->chain_length+1))
		goto error1;
	if ((ctx->ordinal = cipher_ordinal_order(ctx->chain+ctx->chain_length-10)) == NULL)
		goto error2;
	return cipher_ct_init(ctx->ct_size, ctx->ordinal, &ctx->table);
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
		MEM_WIPE(&ctx->table, sizeof (ctx->table));
		MEM_WIPE(ctx->ordinal, *ctx->ordinal);
		free(ctx->ordinal);
		STR_WIPE(ctx->chain);
		free(ctx->chain);
	}
}

size_t
cipher_encode_input(FILE *fp, Cipher *ctx, char *buffer, size_t size)
{
	int ch, n;
	size_t length;

	if (size < 2)
		return 0;

	ctx->shift = 0;
	for (length = 0, size--; (ch = fgetc(fp)) != EOF; ) {
		n = cipher_char_to_ct(&ctx->table, ch, buffer+length, &ctx->shift);
		if (size <= length + n) {
			ungetc(ch, fp);
			break;
		}
		length += n;
	}
	buffer[length] = '\0';

	return length;
}

size_t
cipher_decode_input(FILE *fp, Cipher *ctx, char *buffer, size_t size)
{
	int ch, nl;
	size_t length;

	if (size < 2)
		return 0;

	ctx->shift = 0;
	for (length = 0, size--; (ch = fgetc(fp)) != EOF && length < size; ) {
		if (ch == '\n')
			nl++;
		if (nl == 2)
			break;
		if (!isspace(ch)) {
			nl = 0;
			buffer[length++] = ch;
		}
	}
	buffer[length] = '\0';

	return length;
}

void
cipher_encode_output(FILE *fp, const char *string)
{
	cipher_dump_grouped(stdout, GROUP_WIDTH, 0, OUTPUT_WIDTH, string);
	fputc('\n', fp);
}

void
cipher_decode_output(FILE *fp, const char *string)
{
	fputs(string, fp);
}

char *
cipher_encode0(Cipher *ctx, const char *message)
{
	size_t offset;
	char *out, *tkey;

	offset = ctx->chain_length;
	tkey = cipher_transponse_key(ctx, &offset);
	out = cipher_columnar_transposition_encode(tkey, message);

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
cipher_encode(Cipher *ctx, const char *message)
{
	char *out;

	out = cipher_string_to_ct(&ctx->table, message, 0);
	message = (char *) cipher_encode0(ctx, out);

	STR_WIPE(out);
	free(out);

	return (char *) message;
}

char *
cipher_decode0(Cipher *ctx, const char *message)
{
	size_t offset;
	char *out, *tk1, *tk2;

	offset = ctx->chain_length;
	tk1 = cipher_transponse_key(ctx, &offset);
	tk2 = cipher_transponse_key(ctx, &offset);

	out = cipher_disrupted_transposition_decode(tk2, message);
	out = cipher_columnar_transposition_decode(tk1, message = out);
	STR_WIPE(message);

	free((void *)message);
	STR_WIPE(tk2);
	free(tk2);
	STR_WIPE(tk1);
	free(tk1);

	return out;
}

char *
cipher_decode(Cipher *ctx, const char *message)
{
	char *out;

	out = cipher_decode0(ctx, message);
	cipher_ct_to_string(&ctx->table, out);

	return out;
}

#include <getopt.h>

static char options[] = "c:dvk:l:t:ACIT:U:";

static char usage[] =
"usage:\tcipher [-Cdv][-c size][-l length][-k key][-t min] < message\n"
"\tcipher -A\n"
"\tcipher -I string\n"
"\tcipher -T c|d [-k key] string\n"
"\tcipher -U c|d [-k key] string\n"
"\n"
"-c size\t\tconversion table 28, 37, 46, 56, 111; default " QUOTE(DEFAULT_CT) "\n"
"-d\t\tdecode message\n"
"-k key\t\talpha-numeric string for transpostion or chain addition\n"
"-l length\tchain addition length; default 100\n"
"-t min\t\tminimum transposition key length; default 10\n"
"-v\t\tverbose debug\n"
"\n"
"-A\t\tshow all the supported conversion tables\n"
"-C\t\tdump the chain addition and conversion table\n"
"-I\t\tdump the indices of the ordinal order of characters\n"
"-T c|d\t\tdump the encoded columnar or disrupted transposition\n"
"-U c|d\t\tdump the decoded columnar or disrupted transposition\n"
"\n"
"Copyright 2010, 2012 by Anthony Howe.  All rights reserved.\n"
;

typedef char *(*cipher_fn)(Cipher *, const char *);
typedef char *(*transpose_fn)(const char *, const char *);
typedef size_t (*input_fn)(FILE *, Cipher *, char *, size_t);
typedef void (*output_fn)(FILE *, const char *);

static char input[BLOCK_SIZE+1];

int
main(int argc, char **argv)
{
	char *out;
	Cipher ctx;
	input_fn in_fn;
	output_fn out_fn;
	cipher_fn cf;
	transpose_fn tf;
	int ch, dump, transposition;

	memset(&ctx, 0, sizeof (ctx));

	dump = 0;
	ctx.ct_size = DEFAULT_CT;
	ctx.min_columns = 10;
	ctx.chain_length = 100;
	out_fn = cipher_encode_output;
	in_fn = cipher_encode_input;
	cf = cipher_encode0;
	transposition = 0;

	while ((ch = getopt(argc, argv, options)) != -1) {
		switch (ch) {
		case 'A':
		case 'C':
			debug++;
			dump = ch;
			break;
		case 'c':
			ctx.ct_size = strtol(optarg, NULL, 10);
			break;
		case 'd':
			out_fn = cipher_decode_output;
			in_fn = cipher_decode_input;
			cf = cipher_decode;
			break;
		case 'k':
			ctx.key = optarg;
			break;
		case 'l':
			ctx.chain_length = strtol(optarg, NULL, 10);
			break;
		case 't':
			ctx.min_columns = strtol(optarg, NULL, 10);
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
	case 'A':
		for (ch = 0; ct_sizes[ch] != NULL; ch++)
			(void) cipher_ct_init(ct_sizes[ch], ordinal, &ctx.table);
		exit(EXIT_SUCCESS);
		break;
	default:
		if (cipher_init(&ctx)) {
			fprintf(stderr, "error initialising Cipher structure\n");
			return EXIT_FAILURE;
		}

		if (dump == 'C')
			break;

		while (!feof(stdin)) {
			(void) (*in_fn)(stdin, &ctx, input, sizeof (input));
			if ((out = (*cf)(&ctx, input)) == NULL) {
				cipher_fini(&ctx);
				return EXIT_FAILURE;
			}
			MEM_WIPE(input, sizeof (input));
			(*out_fn)(stdout, out);
			STR_WIPE(out);
			free(out);
		}

		cipher_fini(&ctx);
	}

	return EXIT_SUCCESS;
}

#endif /* TEST */
