/*
 * Copyright 2013 by Anthony Howe. All rights reserved.
 */

#ifndef __com_snert_lib_util_buffer_h__
#define __com_snert_lib_util_buffer_h__	1


#ifdef __cplusplus
extern "C" {
#endif

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


#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_util_buffer_h__ */
