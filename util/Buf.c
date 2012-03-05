/*
 * Buf.h
 *
 * Variable length buffer.
 *
 * Copyright 2001, 2012 by Anthony Howe. All rights reserved.
 */

#ifndef BUF_GROWTH
#define BUF_GROWTH		512
#endif

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <com/snert/lib/util/Buf.h>

static void
bounds(Buf *a, size_t *offp, size_t *lenp)
{
	size_t offset = *offp;
	long len = *lenp;

	if (a->length < offset)
		offset = a->length;

	if (a->length < offset + len)
		len = a->length - offset;

	*offp = offset;
	*lenp = len;
}

#ifdef BUF_FIELD_FUNCTIONS
unsigned char *
BufBytes(Buf *a)
{
	return a->bytes;
}

size_t
BufSize(Buf *a)
{
	return a->size;
}

size_t
BufLength(Buf *a)
{
	return a->length;
}

size_t
BufOffset(Buf *a)
{
	return a->offset;
}

void
BufSetOffset(Buf *a, size_t offset)
{
	a->offset = offset;
}
#endif

int
BufSetSize(Buf *a, size_t length)
{
	size_t capacity;
	unsigned char *bytes;

	if (a == NULL)
		return -1;

	if (a->size < length) {
		capacity = (length / BUF_GROWTH + 1) * BUF_GROWTH;
		if ((bytes = realloc(a->bytes, capacity)) == NULL)
			return -1;

		a->size = capacity;
		a->bytes = bytes;
	}

	return 0;
}

void
BufFini(void *_buf)
{
	Buf *buf = _buf;

	if (buf != NULL) {
		free(buf->bytes);
		buf->bytes = NULL;
	}
}

int
BufInit(Buf *a, size_t size)
{
	if ((a->bytes = malloc(size)) == NULL)
		return -1;

	a->free = BufFini;
	a->size = size;
	a->length = 0;
	a->offset = 0;

	return 0;
}

void
BufFree(void *_buf)
{
	BufFini(_buf);
	free(_buf);
}

Buf *
BufCreate(size_t size)
{
	Buf *a;

	if ((a = malloc(sizeof *a)) == NULL)
		return NULL;

	if (BufInit(a, size)) {
		free(a);
		return NULL;
	}

	a->free = BufFree;

	return a;
}

unsigned char *
BufAsBytes(Buf *a)
{
	unsigned char *bytes;

	bytes = a->bytes;
	free(a);

	return bytes;
}

int
BufSetLength(Buf *a, size_t len)
{
	if (BufSetSize(a, len + 1))
		return -1;

	/* Keep the buffer null terminated. See BufAddByte(). */
	a->bytes[len] = 0;
	a->length = len;

	return 0;
}

int
BufCompareBuf(Buf *aa, size_t ax, Buf *bb, size_t bx, size_t len)
{
	if (len == 0)
		return 0;

	if (aa == NULL && bb == NULL)
		/* null == null */
		return 0;

	if (aa == NULL && bb != NULL)
		/* null < b */
		return -1;

	if (aa != NULL && bb == NULL)
		/* a > null */
		return 1;

	if (aa->length < ax)
		ax = aa->length;
	if (bb->length < bx)
		bx = bb->length;

	if (aa->length < ax + len && bx + len <= bb->length)
		/* Region A too short. */
		return -1;

	if (ax + len <= aa->length && bb->length < bx + len)
		/* Region B too short. */
		return 1;

	/* Region A and B within bounds, now compare them. */
	return memcmp(aa->bytes + ax, bb->bytes + bx, len);
}

int
BufCompare(Buf *aa, Buf *bb)
{
	if (aa->length != bb->length)
		return aa->length - bb->length;

	return BufCompareBuf(aa, 0, bb, 0, aa->length);
}

void
BufReverse(Buf *a, size_t offset, size_t len)
{
	unsigned char *x, *y, byte;

	bounds(a, &offset, &len);

	x = y = a->bytes + offset;

	for (y += len; x < --y; ++x) {
		byte = *y;
		*y = *x;
		*x = byte;
	}
}

void
BufToLower(Buf *a, size_t offset, size_t len)
{
	unsigned char *x;

	bounds(a, &offset, &len);

	for (x = a->bytes + offset; 0 < len; --len, ++x) {
		if (isupper(*x))
			*x = (unsigned char) tolower(*x);
	}
}

void
BufToUpper(Buf *a, size_t offset, size_t len)
{
	unsigned char *x;

	bounds(a, &offset, &len);

	for (x = a->bytes + offset; 0 < len; --len, ++x) {
		if (islower(*x))
			*x = (unsigned char) toupper(*x);
	}
}

int
BufGetByte(Buf *a, size_t offset)
{
	if (a->length <= offset)
		return EOF;
	return a->bytes[offset];
}

unsigned char *
BufGetBytes(Buf *a, size_t offset, size_t len)
{
	unsigned char *bytes;

	bounds((Buf *) a, &offset, &len);

	bytes = malloc((size_t) len);
	memcpy(bytes, a->bytes + offset, len);

	return bytes;
}

char *
BufToString(Buf *a)
{
	char *string;

	if ((string = malloc(a->length + 1)) != NULL) {
		memcpy(string, a->bytes, a->length);
		string[a->length] = '\0';
	}

	return string;
}

void
BufSetByte(Buf *a, size_t offset, int byte)
{
	if (a->length <= offset)
		BufAddByte(a, byte);
	else
		a->bytes[offset] = (unsigned char) byte;
}

void
BufSetBytes(Buf *a, size_t ax, unsigned char *b, size_t bx, size_t len)
{
	bounds(a, &ax, &len);
	memcpy(a->bytes + ax, b + bx, len);
}

int
BufInsertBytes(Buf *a, size_t target, unsigned char *bytes, size_t source, size_t len)
{
	if (len == 0)
		return 0;

	/* Make sure we have enough room for the data and a null byte. */
	if (BufSetSize(a, a->length + len + 1) < 0)
		return -1;

	memmove(a->bytes + target + len, a->bytes + target, a->length - target);

	memcpy(a->bytes + target, bytes + source, len);
	a->length += len;

	/* Keep the buffer null terminated. See BufAddByte(). */
	a->bytes[a->length] = 0;

	return 0;
}

int
BufAddByte(Buf *a, int byte)
{
	/* Make sure we have enough room for the byte and a null byte. */
	if (BufSetSize(a, a->length + 2) < 0)
		return -1;

	a->bytes[a->length++] = (unsigned char) byte;

	/* Keep the buffer null terminated so that we can use it with
	 * functions that work on C strings. The null byte is NOT part
	 * of the data nor does it affect the buffer length. Note though
	 * that if the data contains a null byte, then C string functions
	 * will have undefined results, but then you should know that.
	 */
	a->bytes[a->length] = 0;

	return 0;

}

int
BufAddBytes(Buf *a, unsigned char *bytes, size_t len)
{
	if (len == 0)
		return 0;

	/* Make sure we have enough room for the data and a null byte. */
	if (BufSetSize(a, a->length + len + 1) < 0)
		return -1;

	memcpy(a->bytes + a->length, bytes, len);
	a->length += len;

	/* Keep the buffer null terminated. See BufAddByte(). */
	a->bytes[a->length] = 0;

	return 0;
}

int
BufAddString(Buf *a, const char *s)
{
	if (s == NULL)
		return 0;
	return BufAddBytes(a, (unsigned char *) s, strlen(s));
}

int
BufAddBuf(Buf *a, Buf *b, size_t offset, size_t len)
{
	bounds(b, &offset, &len);
	return BufAddBytes(a, b->bytes + offset, len);
}

int
BufAddUnsigned(Buf *a, unsigned long value, int base)
{
	static char digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

	if (0 < value && BufAddUnsigned(a, value / base, base))
		return -1;

	if (base < 2 && 36 < base)
		return -1;

	return BufAddByte(a, digits[value % base]);
}

int
BufAddSigned(Buf *a, long value, int base)
{
	if (base == 10 && value < 0) {
		(void) BufAddByte(a, '-');
		value = -value;
	}

	return BufAddUnsigned(a, (unsigned long) value, base);
}

void
BufTrim(Buf *a)
{
	while (isspace(a->bytes[a->length-1]))
		a->length--;
	a->bytes[a->length] = 0;
}

/*** DEPRICATED ***/
int
BufAddInputLine(Buf *a, FILE *fp, long max)
{
	int byte;
	size_t start = a->length;

	if (max < 0)
		max = LONG_MAX;

	while (a->length - start < max) {
		if ((byte = fgetc(fp)) == EOF) {
			if (start == a->length)
				/* EOF or error. */
				return -1;
			/* EOF before newline. */
			break;
		}

		BufAddByte(a, byte);

		if (byte == '\n')
			break;
	}

	/* Keep the buffer null terminated. See BufAddByte(). */
	a->bytes[a->length] = 0;

	return a->length - start;
}

/***********************************************************************
 *** END
 ***********************************************************************/
