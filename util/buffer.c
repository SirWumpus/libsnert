/*
 * Copyright 2013 by Anthony Howe. All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#ifndef BUFFER_GROWTH
#define BUFFER_GROWTH   128
#endif

typedef struct {
	size_t size;
	size_t length;
	unsigned char *data;
} Buffer;

extern Buffer *buffer_create(size_t size);
extern void buffer_free(void *_b);
extern int buffer_grow(Buffer *b, size_t octets);
extern int buffer_append(Buffer *b, const void *s, size_t length);
extern int buffer_insert(Buffer *b, const void *s, size_t length, size_t offset);
extern int buffer_delete(Buffer *b, size_t offset, size_t length);
extern int buffer_vprintf(Buffer *b, const char *fmt, va_list args);
extern int buffer_printf(Buffer *b, const char *fmt, ...);

Buffer *
buffer_create(size_t size)
{
	Buffer *b;

	if ((b = malloc(sizeof (*b))) != NULL) {
		if ((b->data = malloc(size)) == NULL) {
			free(b);
			return NULL;
		}
		b->data[0] = '\0';
		b->size = size;
		b->length = 0;
	}

	return b;
}

void
buffer_free(void *_b)
{
	if (_b != NULL) {
		free(((Buffer *) _b)->data);
		free(_b);
	}
}

int
buffer_grow(Buffer *b, size_t request)
{
	unsigned char *copy;

	if (b == NULL)
		return -1;
	if (b->size <= b->length + request + 1) {
		if ((copy = realloc(b->data, b->size + request + BUFFER_GROWTH)) == NULL)
			return -1;
		b->size += request + BUFFER_GROWTH;
		b->data = copy;
	}

	return 0;
}

int
buffer_append(Buffer *b, const void *s, size_t length)
{
	if (s == NULL || buffer_grow(b, length) != 0)
		return -1;
	(void) memcpy(b->data+b->length, s, length);
	b->length += length;
	b->data[b->length] = '\0';

	return 0;
}

int
buffer_insert(Buffer *b, const void *s, size_t length, size_t offset)
{
	if (s == NULL || buffer_grow(b, length) != 0)
		return -1;
	(void) memmove(b->data+offset+length, b->data+offset, b->length-offset);
	(void) memcpy(b->data+offset, s, length);
	b->length += length;
	b->data[b->length] = '\0';

	return 0;
}

int
buffer_delete(Buffer *b, size_t offset, size_t length)
{
	if (b == NULL)
		return -1;
	if (b->length <= offset + length) {
		b->length = 0;
	} else {
		(void) memmove(
			b->data+offset, b->data+offset+length,
			b->length-offset-length
		);
		b->length -= length;
	}
	b->data[b->length] = '\0';

	return 0;
}

int
buffer_vprintf(Buffer *b, const char *fmt, va_list args)
{
	int length;

	if (b == NULL)
		return -1;
	length = vsnprintf((char *)b->data+b->length, b->size-b->length, fmt, args);
	if (b->size <= b->length + length) {
		if (buffer_grow(b, length) != 0)
			return -1;
		length = vsnprintf((char *)b->data+b->length, b->size-b->length, fmt, args);
	}
	b->length += length;

	return 0;
}

int
buffer_printf(Buffer *b, const char *fmt, ...)
{
	int rc;
	va_list args;

	va_start(args, fmt);
	rc = buffer_vprintf(b, fmt, args);
	va_end(args);

	return rc;
}
