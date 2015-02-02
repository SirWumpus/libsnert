/*
 * DebugMalloc.h
 *
 * Copyright 2003, 2015 by Anthony Howe.  All rights reserved.
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
extern int memory_dump_length;
extern int memory_thread_leak;
extern int memory_test_double_free;

extern int memory_freed_marker;
extern int memory_lower_marker;
extern int memory_upper_marker;

extern void *memory_free_chunk;
extern void *memory_malloc_chunk;
extern unsigned long memory_report_interval;

#if defined(DEBUG_MALLOC)

extern void  DebugFree(void *chunk, const char *here, unsigned line);
extern void *DebugMalloc(size_t size, const char *here, unsigned line);
extern void *DebugCalloc(size_t n, size_t size, const char *here, unsigned line);
extern void *DebugRealloc(void *chunk, size_t size, const char *here, unsigned line);

/**
 * Force the transition from MEMORY_INITIALISING to MEMORY_INITIALISED
 * in case not all object files were built using DebugMalloc.h and
 * DEBUG_MALLOC macro.  Place at top of main().
 */
extern void DebugMallocStart(void);

extern void DebugMallocReport(void);
extern void DebugMallocSummary(void);
extern void DebugMallocDump(void *chunk, size_t length);
extern void DebugMallocHere(void *chunk, const char *here, unsigned line);
extern void DebugMallocAssert(void *chunk, const char *here, unsigned line);

#define free(p)				DebugFree(p, __func__, __LINE__)
#define malloc(s)			DebugMalloc(s, __func__, __LINE__)
#define calloc(n,s)			DebugCalloc(n, s, __func__, __LINE__)
#define realloc(p,s)			DebugRealloc(p, s, __func__, __LINE__)

#else

#define DebugFree(p, f, l)		free(p)
#define DebugMalloc(s, f, l)		malloc(s)
#define DebugCalloc(n, s, f, l)		calloc(n, s)
#define DebugRealloc(p, s, f, l)	realloc(p, s);

#define DebugMallocStart()
#define DebugMallocAssert(...)
#define DebugMallocReport()
#define DebugMallocSummary()
#define DebugMallocDump(...)
#define DebugMallocHere(...)

#endif

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_util_DebugMalloc_h__ */
