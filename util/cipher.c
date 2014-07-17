/*
 * cipher.c
 *
 * Copyright 2014 by Anthony Howe. All rights released.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#define GROUPING		5
#define WRAP_WIDTH		((GROUPING+1)*10)
#define BUFFER_SIZE		(GROUPING*100)
#define MAX_BUFFER_SIZE		10000		/* Include space for terminating NUL byte. */
#define NUMERIC_SEED		"3141592653"	/* PI to 9 decimal places. */
#define MEMSET(p,b,n)

typedef struct {
	int length;
	char *set;
	char *code[2];
} cipher_ct;

extern cipher_ct cipher_ct0;
extern cipher_ct cipher_ct28;
extern cipher_ct cipher_ct37;
extern cipher_ct cipher_ct46;
extern cipher_ct cipher_ct106;

/**
 * CT0
 *
 * Place holder used for pass-through without conversion.
 */
cipher_ct cipher_ct0 = {
	0,
	NULL,
	{
	NULL,
	NULL
	}
};

/**
 * CT28 Straddling Checkerboard
 *
 *         0  1  2  3  4  5  6  7  8  9
 *       -------------------------------
 *       | S  E  N  O  R  I  T  A
 *     8 | B  C  D  F  G  H  J  K  L  M
 *     9 | P  Q  U  V  W  X  Y  Z  +  /
 *
 */
cipher_ct cipher_ct28 = {
	28,
	"SENORITABCDFGHJKLMPQUVWXYZ+/",
	{
	"0123456788888888889999999999",
	"        01234567890123456789"
	}
};

/**
 * CT37 Straddling Checkerboard
 *
 *         0  1  2  3  4  5  6  7  8  9
 *       -------------------------------
 *       | E  S  T  O  N  I  A
 *     7 | B  C  D  F  G  H  J  K  L  M
 *     8 | P  Q  R  U  V  W  X  Y  Z  /
 *     9 | 0  1  2  3  4  5  6  7  8  9
 */
cipher_ct cipher_ct37 = {
	37,
	"ESTONIABCDFGHJKLMPQRUVWXYZ0123456789/",
	{
	"0123456777777777788888888889999999999",
	"       012345678901234567890123456789"
	}
};

/**
 * CT46 Straddling Checkerboard
 *
 *         0  1  2  3  4  5  6  7  8  9
 *       -------------------------------
 *       | R  E  A  N  O  I
 *     6 | B  C  D  F  G  H  J  K  L  M
 *     7 | P  Q  S  T  U  V  W  X  Y  Z
 *     8 | SP .  ,  :  ?  /  (  )  "  #
 *     9 | 0  1  2  3  4  5  6  7  8  9
 */
cipher_ct cipher_ct46 = {
	46,
	"REANOIBCDFGHJKLMPQSTUVWXYZ .,:?/()\"#0123456789",
	{
	"0123456666666666777777777788888888889999999999",
	"      0123456789012345678901234567890123456789"
	}
};

/**
 * CT106 ASCII Straddling Checkerboard
 *
 * Based on US QWERTY keyboard layout of printable ASCII and whitespace.
 * Assumes lower case alpha is predominate, with most frequent English
 * lower case letters using single digits, including space and linefeed.
 * Remaining double digit layout corresponds to QWERTY unshifted with
 * controls, shifted, ASCII bell, and other whitespace.
 *
 *	    0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
 *	  -------------------------------------------------
 *	  | s  e  n  o  r  i  t  a  SP LF
 *	A | ES `  1  2  3  4  5  6  7  8  9  0  -  =  BS HT
 *	B | q  w  y  u  p  [  ]  \  d  f  g  h  j  k  l  ;
 *	C | '  CR z  x  c  v  b  m  ,  .  /  ~  !  @  #  $
 *	D | %  ^  &  *  (  )  _  +  Q  W  E  R  T  Y  U  I
 *	E | O  P  {  }  |  A  S  D  F  G  H  J  K  L  :  "
 *	F | Z  X  C  V  B  N  M  <  >  ?  BE VT FF
 */
cipher_ct cipher_ct106 = {
	106,
	"senorita \n\33`1234567890-=\b\tqwyup[]\\dfghjkl;'\rzxcvbm,./~!@#$"
	"%^&*()_+QWERTYUIOP{}|ASDFGHJKL:\"ZXCVBNM<>?\a\v\f...",
	{
	"0123456789AAAAAAAAAAAAAAAABBBBBBBBBBBBBBBBCCCCCCCCCCCCCCCC"
	"DDDDDDDDDDDDDDDDEEEEEEEEEEEEEEEEFFFFFFFFFFFFFFFF",
	"          0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"
	"0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"
	}
};

static int debug;

void
cipher_set_debug(int level)
{
	debug = level;
}

void
cipher_dump_grouped(FILE *fp, int width, const char *text, int skip_ws)
{
	int group, col;

	for ( ; *text != '\0'; ) {
		group = 0;
		for (col = 0; col < width && *text != '\0'; text++) {
			if (!skip_ws || ' ' < *text) {
				fputc(*text, fp);
				col++;

				if ((++group % GROUPING) == 0) {
					fputc(' ', fp);
					group = 0;
					col++;
				}
			}
		}
		if (width <= col)
			fputc('\n', fp);
	}
	if (debug && col < width)
		fputc('\n', fp);
}

/**
 * @param fp
 *      An output FILE pointer.
 *
 * @param key
 *      A numeric C string representing the transposition key.
 *
 * @param text
 *      A numeric C string representing the transposition table.
 */
void
cipher_dump_transposition(FILE *fp, const char *key, const char *text)
{
	size_t key_len;

	if (*key == '\0')
		key = "A";
	key_len = strlen(key);
	key_len += key_len / GROUPING;
	cipher_dump_grouped(fp, key_len, key, 0);
#ifndef NDEBUG
{
	int col;
	for (col = 0; col < key_len; col++)
		fputc('=', fp);
	fputc('\n', fp);
}
#endif /* NDEBUG */
	cipher_dump_grouped(fp, key_len, text, 0);
	fputc('\n', fp);
}

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

	if (debug)
		cipher_dump_grouped(stderr, WRAP_WIDTH, buffer, 1);

	return 0;
}

/**
 * @param in
 *      A C string of up to 255 bytes in length.
 *
 * @param out
 *      An output buffer that starts with the length N of the
 *      input string followed by N octets. Each octet in the
 *      output array contains the index by which the input
 *      string should be read according to the ordinal order
 *      of the input.
 *
 *      Examples assuming ASCII:
 *
 *          B A B Y L O N 5             input
 *          2 1 3 7 4 6 5 0             ordinal order
 *          7 1 0 2 4 6 5 3             index of ordinal
 *
 *          H E L L O W O R L D         input
 *          2 1 3 4 6 9 7 8 5 0         ordinal order
 *          9 1 0 2 3 8 4 6 7 5         index of ordinal
 *
 * @return
 *	Zero on success.
 */
void
cipher_index_order(const char *in, int out[256])
{
	int octet, *op;
	const char *ip;

	if (*in == '\0')
		in = " ";
	octet = 1;
	op = out+1;
	do {
		for (ip = in; *ip != '\0'; ip++) {
			if (*ip == octet)
				*op++ = ip - in;
		}
		*out = ip - in;
		octet++;
	} while (op - out <= *out);
#ifndef NDEBUG
	if (debug) {
		for (op = out+1; in++ < ip; op++)
			fprintf(stderr, "%02X ", *op);
		fputs("\n\n", stderr);
	}
#endif /* NDEBUG */
}

int
cipher_seq_write(char *out, char *in, int i, int j)
{
	out[j++] = in[i];

	/* For disrupted transposition decoding, erase the input
	 * as it is used. Simplifies debug output of intermediate
	 * triangle regions.
	 *
	 * Added benefit is that it ensures that the intermediate
	 * results are erased as they are used.
	 */
	in[i] = '_';

	return j;
}

int
cipher_seq_read(char *out, char *in, int i, int j)
{
	out[i] = in[j];
	in[j++] = '_';
	return j;
}

void
cipher_columnar_transposition(const char *key, const char *in, char *out, size_t out_len, int (*seq_fn)())
{
	size_t key_len;
	int i, j, x, indices[256];

	if (debug) {
		cipher_dump_grouped(stderr, WRAP_WIDTH, in, 1);
		fputc('\n', stderr);
		if (seq_fn == cipher_seq_write)
			cipher_dump_transposition(stderr, key, in);
	}

	cipher_index_order(key, indices);

	x = 0;
	key_len = indices[0];
	for (j = 1; j <= key_len; j++) {
		for (i = indices[j]; i < out_len; i += key_len)
			x = (*seq_fn)(out, in, i, x);
	}
	out[x] = '\0';

	if (debug && seq_fn == cipher_seq_read)
		cipher_dump_transposition(stderr, key, out);
}

void
cipher_disrupted_transposition(const char *key, const char *in, char *out, size_t out_len, int (*seq_fn)())
{
	size_t key_len;
	int i, j, k, r, x, indices[256];

	if (debug) {
		MEMSET(out, '_', out_len);
		cipher_dump_grouped(stderr, WRAP_WIDTH, in, 1);
	}

	cipher_index_order(key, indices);

	/* Create one or more triangles in output table. */
	r = 0;
	x = 0;
	out[out_len] = '\0';
	key_len = indices[0];
	for (k = 1; r < out_len; k = k % key_len + 1) {
		/* Fill triangle area. */
		for (j = indices[k]; j <= key_len; j++, r += key_len) {
			/* Fill row of triangle. */
			for (i = 0; i < j; i++) {
				if (out_len <= r + i)
					break;
				x = (*seq_fn)(out, in, r + i, x);
			}
		}
	}

	/* Show intermediate table. */
	if (debug)
		cipher_dump_transposition(stderr, key, seq_fn == cipher_seq_read ? out : in);

	/* Fill in empty triangle space. */
	r = 0;
	for (k = 1; x < out_len; k = k % key_len + 1) {
		for (j = indices[k]; j <= key_len; j++, r += key_len) {
			for (i = j; i < key_len; i++) {
				if (out_len <= x)
					break;
				x = (*seq_fn)(out, in, r + i, x);
			}
		}
	}
}

size_t
cipher_ct_encode(cipher_ct *ct, FILE *fp, char *out, size_t length)
{
	int c, x;
	size_t i = 0;

	for ( ; i < length && (c = fgetc(fp)) != EOF; ) {
		if (debug)
			(void) fputc(c, stderr);
		if (0 < ct->length) {
			if (ct->length != 106)
				c = toupper(c);
			if (0 <= (x = strchr(ct->set, c) - ct->set)) {
				out[i++] = ct->code[0][x];
				if (ct->code[1][x] != ' ') {
					if (length <= i) {
						/* Buffer full and encoding is
						 * incomplete, so push back the
						 * character for next block.
						 * Leave the partial encoding.
						 */
						ungetc(c, fp);
						break;
					}
					out[i++] = ct->code[1][x];
				}
			}
		} else {
			/* CT 0, no conversion, simply passthrough. */
			out[i++] = c;
		}
	}
	out[i] = '\0';
	if (debug)
		(void) fputc('\n', stderr);
	return i;
}

void
cipher_ct_decode(cipher_ct *ct, FILE *fp, char *in)
{
	int x;

	for ( ; *in != '\0'; ) {
		if (0 < ct->length) {
			if (0 <= (x = strchr(ct->code[0], *in++) - ct->code[0])) {
				if (ct->code[1][x] != ' ') {
					/* Find next valid hex character. */
					for ( ; !isxdigit(*in) || islower(*in); in++)
						;
					/* Reached NUL byte with an incomplete
					 * encoding at the end of a block?
					 */
					if (*in == '\0')
						break;
					x = strchr(ct->code[1] + x, *in++) - ct->code[1];
				}
				if (x < ct->length)
					(void) fputc(ct->set[x], fp);
			}
		} else {
			/* CT 0, no conversion, simply passthrough. */
			(void) fputc(*in++, fp);
		}
	}
}

#ifdef TEST
static int decode;
static unsigned bsize = BUFFER_SIZE;
static char buffer1[MAX_BUFFER_SIZE], buffer2[MAX_BUFFER_SIZE];

typedef size_t (*read_fn)(FILE *fp, char *out, size_t length);
typedef void (*write_fn)(FILE *fp, int width, const char *text, int skip_ws);

static size_t
read_all(FILE *fp, char *out, size_t length)
{
	int c;
	size_t i;

	for (i = 0; i < length && (c = fgetc(fp)) != EOF; i++)
		out[i] = c;
	out[i] = '\0';

	return i;
}

static size_t
read_digits(FILE *fp, char *out, size_t length)
{
	int c;
	size_t i;

	/* Read hex digits, discarding whitespace and invalid characters. */
	for (i = 0; i < length && (c = fgetc(fp)) != EOF; i += 0 != isxdigit(c))
		out[i] = c;
	out[i] = '\0';

	return i;
}

static void
dump_asis(FILE *fp, int width, const char *text, int skip_ws)
{
	(void) fputs(text, fp);
}

static const char usage[] =
"usage: cipher [-dv][-b size][-c ct] key1 [key2] < input\n"
"\n"
"-b size\t\tencoding block size; default 500\n"
"-c ct\t\tconversion table size: 0, 28, 37, 46, 106; default 106\n"
"-d\t\tdecode message\n"
"-v\t\tverbose debug\n"
"\n"
"key1\t\tcolumnar transposition key; any single character for identity\n"
"key2\t\tdisrupted columnar transposition key\n"
"\n"
"Copyright 2013, 2014 by Anthony Howe. All rights reserved.\n"
;

static cipher_ct *ct_list[] = {
	&cipher_ct106, &cipher_ct46, &cipher_ct37, &cipher_ct28, &cipher_ct0, NULL
};

int
main(int argc, char **argv)
{
	int ch, i;
	cipher_ct **ct;

	ct = ct_list;

	while ((ch = getopt(argc, argv, "b:c:dv")) != -1) {
		switch (ch) {
		case 'b':
			bsize = strtoul(optarg, NULL, 10);
			break;

		case 'c':
			i = (int) strtol(optarg, NULL, 10);
			for (ct = ct_list; *ct != NULL; ct++) {
				if (i == (*ct)->length)
					break;
			}
			if (*ct == NULL)
				optind = argc;
			break;

		case 'd':
			decode++;
			break;

		case 'v':
			debug++;
			break;

		default:
			optind = argc;
		}
	}
	if (argc <= optind || bsize < GROUPING || sizeof buffer1 <= bsize) {
		puts(usage);
		return 1;
	}

	if (decode) {
		for ( ; 0 < (i = read_digits(stdin, buffer1, bsize)); ) {
			cipher_columnar_transposition(argv[argc-1], buffer1, buffer2, i, cipher_seq_read);
			if (optind + 2 == argc) {
				cipher_disrupted_transposition(argv[argc-1], buffer2, buffer1, i, cipher_seq_write);
				cipher_columnar_transposition(argv[argc-2], buffer1, buffer2, i, cipher_seq_read);
			}
			cipher_ct_decode(*ct, stdout, buffer2);
		}
	} else {
		for ( ; 0 < (i = cipher_ct_encode(*ct, stdin, buffer1, bsize)); ) {
			if (optind + 2 == argc) {
				cipher_columnar_transposition(argv[argc-2], buffer1, buffer2, i, cipher_seq_write);
				cipher_disrupted_transposition(argv[argc-1], buffer2, buffer1, i, cipher_seq_read);
			}
			cipher_columnar_transposition(argv[argc-1], buffer1, buffer2, i, cipher_seq_write);
			cipher_dump_grouped(stdout, WRAP_WIDTH, buffer2, 1);
		}
		if (i < bsize)
			fputc('\n', stdout);
	}

	return 0;
}

#endif /* TEST */
