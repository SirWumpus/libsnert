/*
 * stdlib.h
 *
 * Replacement stdlib.h for use with DebugMalloc.o. Prepend to CFLAGS
 *
 *	CFLAGS="-I. -DDEBUG_MALLOC_TRACE"
 *
 * Copyright 2007 by Anthony Howe. All rights reserved.
 */

#ifndef __stdlib_h__
#define __stdlib_h__	1

#include "/usr/include/stdlib.h"

#define DEBUG_MALLOC_TRACE
#define DEBUG_MALLOC_THREAD_REPORT

#define malloc(s)	DebugMalloc(s, __FILE__, __LINE__)
#define calloc(m,n)	DebugCalloc(m, n, __FILE__, __LINE__)
#define realloc(p,s)	DebugRealloc(p, s, __FILE__, __LINE__)

extern void  DebugFree(void *);
extern void *DebugMalloc(size_t, const char *, unsigned);
extern void *DebugCalloc(size_t, size_t, const char *, unsigned);
extern void *DebugRealloc(void *, size_t, const char *, unsigned);
extern void DebugMallocReport(void);

#ifdef  __cplusplus
}
#endif

#endif /* __stdlib_h__ */
