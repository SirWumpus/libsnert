/*
 * Base64.c
 *
 * Copyright 2002, 2013 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <stdlib.h>

#include <com/snert/lib/version.h>
#include <com/snert/lib/util/Base64.h>

#ifdef DEBUG_MALLOC
# include <com/snert/lib/util/DebugMalloc.h>
#endif

/***********************************************************************
 *** Global Variables & Constants
 ***********************************************************************/

#define START			0

#define DECODE_A		1
#define DECODE_B		2
#define DECODE_C		3
#define DECODE_D		4
#define DECODE_PAD3		5
#define DECODE_PAD2		6
#define DECODE_PAD1		7
#define DECODE_EOF		8

#define ENCODE_A		10
#define ENCODE_B		11
#define ENCODE_C		12

#define DECODESET_LENGTH	256

static int decodeSet[DECODESET_LENGTH];

static unsigned char encodeSet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";


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

/***********************************************************************
 *** Instance methods
 ***********************************************************************/

void
Base64Destroy(void *selfless)
{
	Base64 self = selfless;

	if (self != NULL) {
		free(self);
	}
}

void
Base64Reset(Base64 self)
{
	self->_state = 0;
}

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
int
Base64SetPadding(Base64 self, int pad)
{
	/* Make sure padding character is in range. */
	if (pad < 0 && DECODESET_LENGTH <= pad)
		return -1;

	/* Make sure padding character is not a Base64 character. */
	if (0 <= decodeSet[pad] && decodeSet[pad] < 64)
		return -1;

	switch (pad) {
	case '"': case '%': case '&': case '\'':
	case '(': case ')': case '*': case '+':
	case ',': case '-': case '.': case '/':
	case ':': case ';': case '<': case '=':
	case '>': case '?': case '_':
		self->_pad = pad;
		return 0;
	}

	return -1;
}

/**
 * @param self
 *	This object.
 *
 * @param chr
 *	A character to decode from Base64 to an octet.
 *
 * @return
 *	A decoded octet, BASE64_NEXT if more input is required,
 *	BASE64_ERROR for an invalid input value or decode state,
 *	otherwise BASE64_EOF if the EOF marker has been reached.
 *
 */
int
Base64Decode(Base64 self, int x)
{
	int value;

	if (x < 0)
		x = self->_pad;

	if (DECODESET_LENGTH <= x)
		/* Invalid range for a character octet. */
		return BASE64_ERROR;

	if (x == self->_pad)
		value = 64;

	else if ((value = decodeSet[x]) == BASE64_NEXT) {
		/* Invalid Base64 characters are ignored. */
		return BASE64_NEXT;
	}

	switch (self->_state) {
	case START:
	case DECODE_A:
		/* If we found the padding character, then we have just
		 * just decoded a full quantum and have reached the end.
		 * However, it looks like we have a full quantum of
		 * padding characters to consume.
		 */
		if (value == 64) {
			self->_state = DECODE_PAD3;
			return BASE64_NEXT;
		}

		self->_state = DECODE_B;
		self->_hold = value;
		x = BASE64_NEXT;
		break;
	case DECODE_B:
		/* If we found the padding character, then an error
		 * occured since we don't have insufficient bits to
		 * decode the first octet.
		 */
		if (value == 64)
			return BASE64_ERROR;

		x = (unsigned char)((self->_hold << 2) | (value >> 4));
		self->_state = DECODE_C;
		self->_hold = value;
		break;
	case DECODE_C:
		/* If we found the padding character, then we have just
		 * just decoded 1 octet and have reached the end. There
		 * should be two padding characters to consume.
		 */
		if (value == 64) {
			self->_state = DECODE_PAD1;
			return BASE64_NEXT;
		}

		x = (unsigned char)((self->_hold << 4) | (value >> 2));
		self->_state = DECODE_D;
		self->_hold = value;
		break;
	case DECODE_D:
		/* If we found the padding character, then we have just
		 * just decoded 2 octets and have reached the end. There
		 * should be one padding character to consume.
		 */
		if (value == 64) {
			self->_state = DECODE_EOF;
			return BASE64_EOF;
		}

		x = (unsigned char)((self->_hold << 6) | value);
		self->_state = DECODE_A;
		break;
	case DECODE_PAD3:
		if (value == 64) {
			self->_state = DECODE_PAD2;
			return BASE64_NEXT;
		}
		return BASE64_ERROR;
	case DECODE_PAD2:
		if (value == 64) {
			self->_state = DECODE_PAD1;
			return BASE64_NEXT;
		}
		return BASE64_ERROR;
	case DECODE_PAD1:
		if (value == 64) {
			self->_state = DECODE_EOF;
			return BASE64_EOF;
		}
		return BASE64_ERROR;
	case DECODE_EOF:
		/* To reuse this object, must rset. */
		return BASE64_EOF;
	default:
		/* Flipped from encoding to decoding mid-stream? */
		return BASE64_ERROR;
	}

	return x;
}

/**
 * @param self
 *	This object.
 *
 * @param s
 *	The encoded source buffer.
 *
 * @param slength
 *	The length of the source buffer.
 *
 * @param t
 *	A pointer to an allocated buffer of decoded octets is
 *	passed back. The buffer passed back must be freed by
 *	the caller.
 *
 * @param tlength
 *	The length of the decoded buffer that is passed back.
 *
 * @return
 *	Zero if a decoding ended with a full quantum, BASE64_NEXT
 *	if more input is expected to continue decoding, BASE64_EOF
 *	if the EOF marker was seen, otherwise BASE64_ERROR for a
 *	buffer allocation error.
 */
int
Base64DecodeBuffer(Base64 self, const char *s, long slength, char **t, long *tlength)
{
	long i;
	int octet;
	char *octets;

	if (s == NULL || slength < 4 || t == NULL || tlength == NULL)
		return BASE64_ERROR;

	*t = NULL;
	*tlength = 0;

	if ((octets = malloc(slength - slength/4 + 1)) == NULL)
		return BASE64_ERROR;

	*t = octets;

	for (i = 0; i < slength; i++) {
		octet = Base64Decode(self, s[i]);
		switch (octet) {
		case BASE64_EOF:
			return BASE64_EOF;
		case BASE64_NEXT:
		case BASE64_ERROR:
			break;
		default:
			octets[(*tlength)++] = (char) octet;
			break;
		}
	}

	/* We allocated one octet extra to null terminate
	 * this buffer just in case its a C string.
	 */
	octets[*tlength] = '\0';

	return self->_state == DECODE_A ? 0 : BASE64_NEXT;
}

/**
 * @param self
 *	This object.
 *
 * @param s
 *	The source buffer to encode. NULL if no further input
 *	is available and the encoder must terminate the remaining
 *	octets.
 *
 * @param slength
 *	The length of the source buffer.
 *
 * @param t
 *	A pointer to an allocated buffer of encoded octets is
 *	passed back. The buffer passed back must be freed by
 *	the caller.
 *
 * @param tlength
 *	The length of the encoded buffer that is passed back.
 *
 * @param eof
 *	True if buffer should be terminated with remaining octets
 *	and padding.
 *
 * @return
 *	Zero if a encoding ended with a full quantum, BASE64_NEXT
 *	if more input is expected to continue encoding; otherwise
 *	BASE64_ERROR for a buffer allocation error.
 */
int
Base64EncodeBuffer(Base64 self, const char *s, long slength, char **t, long *tlength, int eof)
{
	char *octets;
	long i, length;

	if (slength < 0 || t == NULL || tlength == NULL)
		return BASE64_ERROR;

	*t = NULL;
	*tlength = 0;

	if (s == NULL) {
		slength = 0;
		eof = 1;
	}

	if ((octets = malloc((slength + 3) / 3 * 4 + 1)) == NULL)
		return BASE64_ERROR;

	length = 0;

	for (i = 0; i < slength; i++) {
		switch (self->_state) {
		case START:
		case ENCODE_A:
			octets[length++] = encodeSet[(s[i] >> 2) & 0x3f];
			self->_hold = (s[i] << 4) & 0x30;
			self->_state = ENCODE_B;
			break;

		case ENCODE_B:
			octets[length++] = encodeSet[self->_hold | ((s[i] >> 4) & 0xf)];
			self->_hold = (s[i] << 2) & 0x3c;
			self->_state = ENCODE_C;
			break;

		case ENCODE_C:
			octets[length++] = encodeSet[self->_hold | ((s[i] >> 6) & 0x3)];
			octets[length++] = encodeSet[s[i] & 0x3f];
			self->_state = ENCODE_A;
			break;
		}
	}

	if (eof) {
		/* Terminate encoding with remaining octets and padding. */
		switch (self->_state) {
		case START:
		case ENCODE_A:
			if (s == NULL) {
				octets[length++] = (char) self->_pad;
				octets[length++] = (char) self->_pad;
				octets[length++] = (char) self->_pad;
				octets[length++] = (char) self->_pad;
			}
			break;
		case ENCODE_B:
			octets[length++] = encodeSet[self->_hold];
			octets[length++] = (char) self->_pad;
			octets[length++] = (char) self->_pad;
			break;
		case ENCODE_C:
			octets[length++] = encodeSet[self->_hold];
			octets[length++] = (char) self->_pad;
			break;
		}

		self->_state = ENCODE_A;
	}

	octets[length] = '\0';
	*tlength = length;
	*t = octets;

	return (self->_state == START || self->_state == ENCODE_A) ? 0 : BASE64_NEXT;
}

/***********************************************************************
 *** Class methods
 ***********************************************************************/

static char Base64Name[] = "Base64";

Base64
Base64Create(void)
{
	Base64 self;
	static struct base64 model;

	if (decodeSet['/'] != 63) {
		int i;

		for (i = 0; i < DECODESET_LENGTH; i++)
			decodeSet[i] = BASE64_NEXT;

		for (i = 0; i < sizeof (encodeSet); i++)
			decodeSet[encodeSet[i]] = i;
	}

	if ((self = malloc(sizeof (*self))) == NULL)
		return NULL;

	if (model.objectName != Base64Name) {
		/* Setup the super class. */
		ObjectInit(&model);

		/* Overrides. */
		model.objectSize = sizeof (struct base64);
		model.objectName = Base64Name;
		model.destroy = Base64Destroy;

		/* Methods */
		model.reset = Base64Reset;
		model.decode = Base64Decode;
		model.setPadding = Base64SetPadding;
		model.decodeBuffer = Base64DecodeBuffer;
		model.encodeBuffer = Base64EncodeBuffer;
		model.objectMethodCount += 5;

		/* Instance variables. */
		model._state = START;
		model._hold = 0;
		model._pad = '=';
	}

	*self = model;

	return self;
}

/***********************************************************************
 *** Test
 ***********************************************************************/

#ifdef TEST

#define WITHOUT_SYSLOG			1

#include <stdio.h>
#include <string.h>
#include <com/snert/lib/version.h>

#ifdef USE_DEBUG_MALLOC
# define WITHOUT_SYSLOG			1
# include <com/snert/lib/io/Log.h>
# include <com/snert/lib/util/DebugMalloc.h>
#endif

void
isNotNull(void *ptr)
{
	if (ptr == NULL) {
		printf("...NULL\n");
		exit(1);
	}

	printf("...OK\n");
}

int
main(int argc, char **argv)
{
	Base64 a;
	int rc, ch;
	long length;
	char *buffer;

	static unsigned char binary[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
	static char *encoded[] = {
		"AA==",
		"AAE=",
		"AAEC",
		"AAEC====",
	};

	if (argc == 2 && argv[1][0] == '-' && argv[1][1] == '\0') {
		a = Base64Create();

		while ((ch = fgetc(stdin)) != EOF) {
			rc = a->decode(a, ch);
			if (0 <= rc)
				fputc(rc, stdout);
		}

		while ((rc = a->decode(a, BASE64_NEXT)) != BASE64_EOF) {
			if (0 <= rc)
				fputc(rc, stdout);
		}

		a->destroy(a);
		return 0;
	}

	printf("\n--Base64--\n");

	printf("create object");
	isNotNull((a = Base64Create()));

	rc = a->encodeBuffer(a, NULL, 0, &buffer, &length, 0);
	printf("encode NULL buffer rc=%d buffer=[%s] length=%ld...%s\n", rc, buffer, length, rc == 0 ? "OK" : "FAIL");
	free(buffer);

	a->reset(a);
	rc = a->encodeBuffer(a, binary, 0, &buffer, &length, 0);
	printf("encode buffer 0 rc=%d buffer=[%s] length=%ld...%s\n", rc, buffer, length, rc == 0 ? "OK" : "FAIL");
	free(buffer);

	a->reset(a);
	rc = a->encodeBuffer(a, binary, 1, &buffer, &length, 0);
	printf("encode buffer 1a rc=%d buffer=[%s] length=%ld...%s\n", rc, buffer, length, rc == BASE64_NEXT ? "OK" : "FAIL");
	free(buffer);

	rc = a->encodeBuffer(a, NULL, 0, &buffer, &length, 0);
	printf("encode buffer 1b rc=%d buffer=[%s] length=%ld...%s\n", rc, buffer, length, rc == 0 ? "OK" : "FAIL");
	free(buffer);

	a->reset(a);
	rc = a->encodeBuffer(a, binary, 2, &buffer, &length, 0);
	printf("encode buffer 2a rc=%d buffer=[%s] length=%ld...%s\n", rc, buffer, length, rc == BASE64_NEXT ? "OK" : "FAIL");
	free(buffer);

	rc = a->encodeBuffer(a, NULL, 0, &buffer, &length, 0);
	printf("encode buffer 2b rc=%d buffer=[%s] length=%ld...%s\n", rc, buffer, length, rc == 0 ? "OK" : "FAIL");
	free(buffer);

	a->reset(a);
	rc = a->encodeBuffer(a, binary, 3, &buffer, &length, 1);
	printf("encode buffer 3 rc=%d buffer=[%s] length=%ld...%s\n", rc, buffer, length, rc == 0 ? "OK" : "FAIL");
	free(buffer);

	a->reset(a);
	rc = a->decodeBuffer(a, encoded[0], sizeof (encoded[0]), &buffer, &length);
	printf("decode buffer 1 rc=%d length=%ld...%s\n", rc, length, rc == BASE64_EOF ? "OK" : "FAIL");
	printf("decode 1 matches sample length...%s\n", memcmp(binary, buffer, length) == 0 ? "OK" : "FAIL");
	free(buffer);

	a->reset(a);
	rc = a->decodeBuffer(a, encoded[1], sizeof (encoded[1]), &buffer, &length);
	printf("decode buffer 2 rc=%d length=%ld...%s\n", rc, length, rc == BASE64_EOF ? "OK" : "FAIL");
	printf("decode 2 matches sample length...%s\n", memcmp(binary, buffer, length) == 0 ? "OK" : "FAIL");
	free(buffer);

	a->reset(a);
	rc = a->decodeBuffer(a, encoded[2], sizeof (encoded[2]), &buffer, &length);
	printf("decode buffer 3 rc=%d length=%ld...%s\n", rc, length, rc == 0 ? "OK" : "FAIL");
	printf("decode 3 matches sample length...%s\n", memcmp(binary, buffer, length) == 0 ? "OK" : "FAIL");
	free(buffer);

	a->reset(a);
	rc = a->decodeBuffer(a, encoded[3], 8, &buffer, &length);
	printf("decode buffer 4 rc=%d length=%ld...%s\n", rc, length, rc == BASE64_EOF ? "OK" : "FAIL");
	printf("decode 4 matches sample length...%s\n", memcmp(binary, buffer, length) == 0 ? "OK" : "FAIL");
	free(buffer);

	printf("destroy object\n");
	a->destroy(a);

	printf("\n--DONE--\n");

	return 0;
}

#endif /* TEST */
