/*
 * b64.c
 *
 * Copyright 2006, 2013 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#define _REENTRANT	1

#include <com/snert/lib/version.h>
#include <stdlib.h>
#include <com/snert/lib/util/b64.h>

/***********************************************************************
 *** Base 64 Decoding
 ***********************************************************************/

typedef enum {
	BASE64_START,
	BASE64_DECODE_A,
	BASE64_DECODE_B,
	BASE64_DECODE_C,
	BASE64_DECODE_D,
	BASE64_DECODE_PAD3,
	BASE64_DECODE_PAD2,
	BASE64_DECODE_PAD1,

	BASE64_ENCODE_A,
	BASE64_ENCODE_B,
	BASE64_ENCODE_C,
} BASE64_STATE;

#define BASE64_PAD_CHARACTER		'='
#define BASE64_DECODESET_LENGTH		256

#define BASE64_INPUT_BLOCK		57
#define BASE64_OUTPUT_BLOCK		(BASE64_INPUT_BLOCK / 3 * 4)

static int decodeSet[BASE64_DECODESET_LENGTH];

/* RFC 2045
 *
 * These are variant on EBCDIC systems:
 *
 *	!"#$@[\]^`{|}~
 *
 * While these are invariant:
 *
 *	A-Z  a-z  0-9  SPACE  "%&'()*+,-./:;<=>?_
 */
static const unsigned char encodeSet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/**
 * Initialise the Base64 decoding tables.
 */
void
b64Init(void)
{
	if (decodeSet['/'] != 63) {
		int i;

		for (i = 0; i < BASE64_DECODESET_LENGTH; i++)
			decodeSet[i] = BASE64_NEXT;

		for (i = 0; i < sizeof (encodeSet); i++)
			decodeSet[encodeSet[i]] = i;
	}
}

/**
 * @param b64
 *	Reset the B64 state structure.
 */
void
b64Reset(B64 *b64)
{
	b64->_state = BASE64_START;
	b64->_quantum[4] = '\0';
}

/**
 * @param b64
 *	A pointer to B64 state structure.
 *
 * @param chr
 *	A character to decode from Base64 to an octet.
 *
 * @return
 *	A decoded octet, BASE64_NEXT if more input is required,
 *	BASE64_ERROR for an invalid input value or decode state,
 *	otherwise BASE64_EOF if the EOF marker has been reached.
 */
int
b64Decode(B64 *b64, int x)
{
	int value;

	if (x < 0)
		x = BASE64_PAD_CHARACTER;

	if (BASE64_DECODESET_LENGTH <= x)
		/* Invalid range for a character octet. */
		return BASE64_ERROR;

	if (x == BASE64_PAD_CHARACTER)
		value = 64;

	else if ((value = decodeSet[x]) == BASE64_NEXT) {
		/* Invalid Base64 characters are ignored. */
		return BASE64_NEXT;
	}

	switch (b64->_state) {
	case BASE64_START:
	case BASE64_DECODE_A:
		/* If we found the padding character, then we have just
		 * just decoded a full quantum and have reached the end.
		 * However, it looks like we have a full quantum of
		 * padding characters to consume.
		 */
		if (value == 64) {
			b64->_state = BASE64_DECODE_PAD3;
			return BASE64_NEXT;
		}

		b64->_state = BASE64_DECODE_B;
		b64->_hold = value;
		x = BASE64_NEXT;
		break;
	case BASE64_DECODE_B:
		/* If we found the padding character, then an error
		 * occured since we don't have sufficient bits to
		 * decode the first octet.
		 */
		if (value == 64)
			return BASE64_ERROR;

		x = (unsigned char)((b64->_hold << 2) | (value >> 4));
		b64->_state = BASE64_DECODE_C;
		b64->_hold = value;
		break;
	case BASE64_DECODE_C:
		/* If we found the padding character, then we have just
		 * just decoded 1 octet and have reached the end. There
		 * should be two padding characters to consume.
		 */
		if (value == 64) {
			b64->_state = BASE64_DECODE_PAD1;
			return BASE64_NEXT;
		}

		x = (unsigned char)((b64->_hold << 4) | (value >> 2));
		b64->_state = BASE64_DECODE_D;
		b64->_hold = value;
		break;
	case BASE64_DECODE_D:
		/* If we found the padding character, then we have just
		 * just decoded 2 octets and have reached the end. There
		 * should be one padding character to consume.
		 */
		if (value == 64) {
			return b64->_state = BASE64_EOF;
		}

		x = (unsigned char)((b64->_hold << 6) | value);
		b64->_state = BASE64_DECODE_A;
		break;
	case BASE64_DECODE_PAD3:
		if (value == 64) {
			b64->_state = BASE64_DECODE_PAD2;
			return BASE64_NEXT;
		}
		return BASE64_ERROR;
	case BASE64_DECODE_PAD2:
		if (value == 64) {
			b64->_state = BASE64_DECODE_PAD1;
			return BASE64_NEXT;
		}
		return BASE64_ERROR;
	case BASE64_DECODE_PAD1:
		if (value == 64) {
			return b64->_state = BASE64_EOF;
		}
		return BASE64_ERROR;
	case BASE64_EOF:
		/* To reuse this object, must reset. */
		return BASE64_EOF;
	default:
		/* Flipped from encoding to decoding mid-stream? */
		return BASE64_ERROR;
	}

	return x;
}

/**
 * @param b64
 *	A pointer to B64 state structure.
 *
 * @param input
 *	The input buffer to decode.
 *
 * @param in_length
 *	The length of the input buffer.
 *
 * @param output
 *	A pointer to an output buffer to recieved the decoded octets.
 *	The buffer will be null terminated.
 *
 * @param out_size
 *	The size of the output buffer to fill.
 *
 * @param out_length
 *	The current length of the contents in the output buffer. The
 *	updated length of the encoded output is passed back, which
 *	does not include the null termination octet.
 *
 * @return
 *	Zero if an decoding ended with a full quantum or reached EOF.
 *	BASE64_NEXT if more input is expected to continue decoding.
 */
int
b64DecodeBuffer(B64 *b64, const char *input, size_t in_length, unsigned char *output, size_t out_size, size_t *out_length)
{
	int octet;
	size_t ilen, olen;

	if (b64 == NULL || output == NULL || out_size == 0)
		return BASE64_ERROR;

	if (input == NULL)
		in_length = 0;

	/* Leave room for a terminating null byte, just in case its a C string. */
	olen = (out_length == NULL ? 0 : *out_length);
	out_size--;

	for (ilen = 0; ilen < in_length; ilen++) {
		if (out_size <= olen)
			break;

		if ((octet = b64Decode(b64, input[ilen])) == BASE64_NEXT)
			continue;
		if (octet == BASE64_ERROR)
			return BASE64_ERROR;
		if (octet == BASE64_EOF)
			break;

		output[olen++] = (unsigned char) octet;
	}

	/* Null terminate the buffer just in case its a C string. */
	output[olen] = '\0';

	if (out_length != NULL)
		*out_length = olen;

	return (b64->_state == BASE64_DECODE_A || b64->_state == BASE64_EOF) ? 0 : BASE64_NEXT;
}

/**
 * @param input_length
 *	The input length.
 *
 * @return
 *	The size of the output buffer required to encode the input
 *	plus a terminating null byte.
 */
size_t
b64EncodeGetOutputSize(size_t input_length)
{
	return (input_length + 3) / 3 * 4 + 1;
}

static char *
_b64Encode(B64 *b64, int in, char *quantum)
{
	switch (b64->_state) {
	case BASE64_START:
	case BASE64_ENCODE_A:
		if (in < -1) {
			quantum[0] = BASE64_PAD_CHARACTER;
			quantum[1] = BASE64_PAD_CHARACTER;
			quantum[2] = BASE64_PAD_CHARACTER;
			quantum[3] = BASE64_PAD_CHARACTER;
			return quantum;
		} else if (in < 0) {
			quantum[0] = '\0';
			return quantum;
		}

		quantum[0] = encodeSet[(in >> 2) & 0x3f];
		b64->_hold = (in << 4) & 0x30;
		b64->_state = BASE64_ENCODE_B;
		break;

	case BASE64_ENCODE_B:
		if (in < 0) {
			quantum[1] = encodeSet[b64->_hold];
			quantum[2] = BASE64_PAD_CHARACTER;
			quantum[3] = BASE64_PAD_CHARACTER;
			return quantum;
		}

		quantum[1] = encodeSet[b64->_hold | ((in >> 4) & 0xf)];
		b64->_hold = (in << 2) & 0x3c;
		b64->_state = BASE64_ENCODE_C;
		break;

	case BASE64_ENCODE_C:
		if (in < 0) {
			quantum[2] = encodeSet[b64->_hold];
			quantum[3] = BASE64_PAD_CHARACTER;
			return quantum;
		}

		quantum[2] = encodeSet[b64->_hold | ((in >> 6) & 0x3)];
		quantum[3] = encodeSet[in & 0x3f];
		b64->_state = BASE64_ENCODE_A;
		return quantum;
	}

	return NULL;
}

/**
 * @param b64
 *	A pointer to B64 state structure.
 *
 * @param chr
 *	An octet to encode from an octet to a Base64 character.
 *	Specify -1 to terminate the encoding, specify -2 to
 *	terminate an encoding that ends on a full quantum with
 *	"====" (see RFC 2045 section 6.8 paragraphs 8 and 9).
 *
 * @return
 *	A pointer to a string of 4 encoded Base64 characters or
 *	NULL if more input is required to complete the next
 *	quantum.
 */
char *
b64Encode(B64 *b64, int in)
{
	return _b64Encode(b64, in, b64->_quantum);
}

/**
 * @param b64
 *	A pointer to B64 state structure.
 *
 * @param input
 *	The input buffer to encode.
 *
 * @param in_size
 *	The size of the input buffer.
 *
 * @param output
 *	A pointer to an output buffer to recieved the encoded octets.
 *
 * @param out_size
 *	The size of the output buffer.
 *
 * @param out_length
 *	The current length of the contents in the output buffer. The
 *	updated length of the encoded output is passed back.
 *
 * @return
 *	Zero if an encoding ended with a full quantum. BASE64_NEXT
 *	if more input is expected to continue encoding.
 */
int
b64EncodeBuffer(B64 *b64, const unsigned char *input, size_t in_size, char *output, size_t out_size, size_t *out_length)
{
	size_t ilen, olen;

	if (b64 == NULL || output == NULL || out_size == 0)
		return BASE64_ERROR;

	if (input == NULL)
		in_size = 0;

#define V2
#ifdef V2
	/* Resuming a partially filled buffer, roll length back to
	 * start of last quantum in buffer for _b64Encode().
	 */
	olen = (out_length == NULL ? 0 : (*out_length -  (*out_length % 4)));
#else
	olen = (out_length == NULL ? 0 : *out_length);
#endif
	out_size--;

	for (ilen = 0; ilen < in_size; ilen++) {
#ifdef V2
		if (_b64Encode(b64, input[ilen], output+olen) != NULL) {
			olen += 4;
		}
#else
		switch (b64->_state) {
		case BASE64_START:
		case BASE64_ENCODE_A:
			output[olen++] = encodeSet[(input[ilen] >> 2) & 0x3f];
			b64->_hold = (input[ilen] << 4) & 0x30;
			b64->_state = BASE64_ENCODE_B;
			break;

		case BASE64_ENCODE_B:
			output[olen++] = encodeSet[b64->_hold | ((input[ilen] >> 4) & 0xf)];
			b64->_hold = (input[ilen] << 2) & 0x3c;
			b64->_state = BASE64_ENCODE_C;
			break;

		case BASE64_ENCODE_C:
			output[olen++] = encodeSet[b64->_hold | ((input[ilen] >> 6) & 0x3)];
			output[olen++] = encodeSet[input[ilen] & 0x3f];
			b64->_state = BASE64_ENCODE_A;
			break;
		}
#endif
	}

#ifdef V2
	/* Adjust output length for partial quantum. */
	switch (b64->_state) {
	case BASE64_ENCODE_C:
		olen++;
		/*@fallthrough@*/
	case BASE64_ENCODE_B:
		olen++;
		/*@fallthrough@*/
	case BASE64_ENCODE_A:
	case BASE64_START:
		break;
	}
#endif

	/* Null terminate the output buffer. */
	output[olen] = '\0';

	if (out_length != NULL)
		*out_length = olen;

	return (b64->_state == BASE64_START || b64->_state == BASE64_ENCODE_A) ? 0 : BASE64_NEXT;
}

/**
 * @param b64
 *	A pointer to B64 state structure.
 *
 * @param output
 *	A pointer to an output buffer to recieved the encoded octets.
 *
 * @param out_size
 *	The size of the output buffer.
 *
 * @param out_length
 *	The current length of the contents in the output buffer. The
 *	updated length of the encoded output is passed back.
 *
 * @param mark_end
 *	When true, mark end-of-data by "====" _if_ the encoding ends
 *	on a full quantum. Otherwise leave as is. See RFC 2045 section
 *	6.8 paragraphs 8 and 9.
 *
 * @return
 *	Zero or BASE64_ERROR.
 */
int
b64EncodeFinish(B64 *b64, char *output, size_t out_size, size_t *out_length, int mark_end)
{
	size_t olen;

	if (b64 == NULL || output == NULL || out_size == 0)
		return BASE64_ERROR;

	olen = (out_length == NULL ? 0 : *out_length);

	/* Terminate encoding with remaining output and padding. */
	switch (b64->_state) {
	case BASE64_START:
	case BASE64_ENCODE_A:
		if (out_size < olen + 5)
			return BASE64_ERROR;

		if (mark_end) {
			output[olen++] = BASE64_PAD_CHARACTER;
			output[olen++] = BASE64_PAD_CHARACTER;
			output[olen++] = BASE64_PAD_CHARACTER;
			output[olen++] = BASE64_PAD_CHARACTER;
		}
		break;
	case BASE64_ENCODE_B:
		if (out_size < olen + 3)
			return BASE64_ERROR;
		output[olen++] = encodeSet[b64->_hold];
		output[olen++] = BASE64_PAD_CHARACTER;
		output[olen++] = BASE64_PAD_CHARACTER;
		break;
	case BASE64_ENCODE_C:
		if (out_size < olen + 3)
			return BASE64_ERROR;
		output[olen++] = encodeSet[b64->_hold];
		output[olen++] = BASE64_PAD_CHARACTER;
		break;
	}

	b64->_state = BASE64_ENCODE_A;

	output[olen] = '\0';

	if (out_length != NULL)
		*out_length = olen;

	return 0;
}

#ifdef TEST
#include <com/snert/lib/version.h>
#include <stdio.h>
#include <string.h>
#include <com/snert/lib/util/getopt.h>

static char usage[] =
"usage: b64 [-det] < input\n"
"\n"
"-d\t\tbase64 decode input\n"
"-e\t\tbase64 encode input (buffer)\n"
"-t\t\tterminate an encoding ending on a full quantum by \"====\"\n"
"\n"
LIBSNERT_COPYRIGHT "\n"
;

int
main(int argc, char **argv)
{
	B64 b64;
	int ch, length, decode = 0, use_buffer_version = 0, mark_end = 0;

	while ((ch = getopt(argc, argv, "det")) != -1) {
		switch (ch) {
		case 'd':
			decode = 1;
			break;
		case 'e':
			use_buffer_version = 1;
			break;
		case 't':
			mark_end = 1;
			break;
		default:
			(void) fprintf(stderr, usage);
			exit(2);
		}
	}

	b64Init();
	b64Reset(&b64);

	if (decode) {
		while ((ch = fgetc(stdin)) != EOF) {
			if ((ch = b64Decode(&b64, ch)) == BASE64_NEXT)
				continue;

			if (ch == BASE64_EOF || ch == BASE64_ERROR)
				break;

			fputc(ch, stdout);
		}
	} else if (use_buffer_version) {
		size_t output_length = 0;
		static char output[BASE64_OUTPUT_BLOCK + 1];
		static unsigned char input[BASE64_INPUT_BLOCK];

		while (0 < (length = fread(input, 1, sizeof (input), stdin))) {
			b64EncodeBuffer(&b64, input, length, output, sizeof (output), &output_length);
			if (BASE64_OUTPUT_BLOCK <= output_length) {
				fputs(output, stdout);
				fputs("\r\n", stdout);
				output_length = 0;
			}
		}

		b64EncodeFinish(&b64, output, sizeof (output), &output_length, mark_end);
		fputs(output, stdout);
		fputs("\r\n", stdout);
	} else {
		char *encoded;

		length = 0;
		while ((ch = fgetc(stdin)) != EOF) {
			if ((encoded = b64Encode(&b64, ch)) != NULL) {
				fputs(encoded, stdout);
				length += 4;
			}
			if (BASE64_OUTPUT_BLOCK <= length) {
				fputs("\r\n", stdout);
				length = 0;
			}
		}

		encoded = b64Encode(&b64, mark_end ? -2 : -1);
		fputs(encoded, stdout);
		fputs("\r\n", stdout);
	}

	return EXIT_SUCCESS;
}

#endif /* TEST */
