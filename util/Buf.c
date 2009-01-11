/*
 * Buf.h
 *
 * Variable length buffer.
 *
 * Copyright 2001, 2008 by Anthony Howe. All rights reserved.
 */

#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <com/snert/lib/io/posix.h>
#include <com/snert/lib/util/Buf.h>

#define BUF_GROWTH	512

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

static int
enlarge(Buf *a, size_t length)
{
	size_t capacity;
	unsigned char *bytes;

	if (a->capacity < length) {
		capacity = (length / BUF_GROWTH + 1) * BUF_GROWTH;
		if ((bytes = realloc(a->bytes, capacity)) == NULL)
			return -1;

		a->capacity = capacity;
		a->bytes = bytes;
	}

	return 0;
}


Buf *
BufCreate(size_t capacity)
{
	Buf *a;

	if ((a = malloc(sizeof *a)) == NULL)
		return NULL;

	if ((a->bytes = malloc(capacity)) == NULL) {
		free(a);
		return NULL;
	}

	a->destroy = BufDestroy;
	a->capacity = capacity;
	a->length = 0;

	return a;
}

Buf *
BufAssignString(char *s)
{
	Buf *a;

	if (s == NULL)
		return NULL;

	if ((a = malloc(sizeof (*a))) != NULL) {
		a->bytes = (unsigned char *) s;
		a->length = strlen(s);
		a->capacity = a->length+1;
	}

	return a;
}

Buf *
BufCopyBytes(unsigned char *bytes, size_t offset, size_t len)
{
	Buf *copy;

	if (bytes == NULL || len < 0)
		return NULL;

	if ((copy = BufCreate(len + BUF_GROWTH)) != NULL) {
		memcpy(copy->bytes, bytes + offset, len);
		copy->bytes[len] = 0;
		copy->length = len;
	}

	return copy;
}

Buf *
BufCopyBuf(Buf *a, size_t offset, size_t len)
{
	bounds(a, &offset, &len);
	return BufCopyBytes(a->bytes, offset, len);
}

Buf *
BufCopyString(const char *s)
{
	return BufCopyBytes((unsigned char *) s, 0, strlen(s));
}

void
BufDestroy(void *_buf)
{
	if (_buf != NULL) {
		free(((Buf *) _buf)->bytes);
		free(_buf);
	}
}

unsigned char *
BufAsBytes(Buf *a)
{
	unsigned char *bytes;

	bytes = a->bytes;
	free(a);

	return bytes;
}

size_t
BufLength(Buf *a)
{
	return a->length;
}

void
BufSetLength(Buf *a, size_t len)
{
	if (!enlarge(a, len + 1))
		a->length = len;

	/* Keep the buffer null terminated. See BufAddByte(). */
	a->bytes[a->length] = 0;
}

size_t
BufCapacity(Buf *a)
{
	return a->capacity;
}

unsigned char *
BufBytes(Buf *a)
{
	return a->bytes;
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
	if (enlarge(a, a->length + len + 1) < 0)
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
	if (enlarge(a, a->length + 2) < 0)
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
BufAddBytes(Buf *a, unsigned char *bytes, size_t offset, size_t len)
{
	if (len == 0)
		return 0;

	/* Make sure we have enough room for the data and a null byte. */
	if (enlarge(a, a->length + len + 1) < 0)
		return -1;

	memcpy(a->bytes + a->length, bytes + offset, len);
	a->length += len;

	/* Keep the buffer null terminated. See BufAddByte(). */
	a->bytes[a->length] = 0;

	return 0;
}

int
BufAddString(Buf *a, char *s)
{
	return BufAddBytes(a, (unsigned char *) s, 0, strlen(s));
}

int
BufAddBuf(Buf *a, Buf *b, size_t offset, size_t len)
{
	bounds(b, &offset, &len);
	return BufAddBytes(a, b->bytes, offset, len);
}

void
BufTrim(Buf *a)
{
	while (isspace(a->bytes[a->length-1]))
		a->length--;
	a->bytes[a->length] = 0;
}

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

int
BufAddReadLine(Buf *a, int fd, long max)
{
	unsigned char byte;
	size_t start = a->length;

	if (max < 0)
		max = ULONG_MAX;

	while (a->length - start < max) {
		if (read(fd, &byte, 1) != 1) {
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

