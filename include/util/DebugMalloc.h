/*
 * DebugMalloc.h
 *
 * Copyright 2003, 2013 by Anthony Howe.  All rights reserved.
 */

#ifndef __com_snert_lib_util_DebugMalloc_h__
#define __com_snert_lib_util_DebugMalloc_h__		1

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include <sys/types.h>

extern int memory_exit;
extern int memory_signal;
extern int memory_show_free;
extern int memory_show_malloc;
extern int memory_thread_leak;
extern int memory_test_double_free;

extern int memory_freed_marker;
extern int memory_lower_marker;
extern int memory_upper_marker;

extern void *memory_free_chunk;
extern void *memory_malloc_chunk;
extern unsigned long memory_report_interval;

extern void err_msgv(char *fmt, va_list args);
extern void err_msg(char *fmt, ...);

#if defined(DEBUG_MALLOC)

extern void  DebugFree(void *chunk, const char *file, unsigned line);
extern void *DebugMalloc(size_t size, const char *file, unsigned line);
extern void *DebugCalloc(size_t n, size_t size, const char *file, unsigned line);
extern void *DebugRealloc(void *chunk, size_t size, const char *file, unsigned line);

extern void DebugMallocReport(void);
extern void DebugMallocSummary(void);
extern void DebugMallocDump(void *chunk);
extern void DebugMallocHere(void *chunk, const char *file, unsigned line);

#define free(p)				DebugFree(p, __FILE__, __LINE__)
#define malloc(n)			DebugMalloc(n, __FILE__, __LINE__)
#define calloc(m,n)			DebugCalloc(m, n, __FILE__, __LINE__)
#define realloc(p,n)			DebugRealloc(p, n, __FILE__, __LINE__)

#else

#define DebugFree(p)			free(p)
#define DebugMalloc(n f, l)		malloc(n)
#define DebugCalloc(m, n, f, l)		calloc(m, n)
#define DebugRealloc(p, s, f, l)	realloc(p, s);

#define DebugMallocReport()
#define DebugMallocSummary()
#define DebugMallocDump(...)
#define DebugMallocHere(...)

#endif

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_util_DebugMalloc_h__ */
