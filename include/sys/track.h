/*
 * track.h
 *
 * Copyright 2015 by Anthony Howe.  All rights reserved.
 */

/***********************************************************************
 *** SHOULD BE INCLUDED ONLY AFTER stdlib.h TO AVOID COMPILER ERRORS.
 ***********************************************************************/

#ifndef __com_snert_lib_sys_track_h__
#define __com_snert_lib_sys_track_h__	1

#ifdef __cplusplus
extern "C" {
#endif

#if defined(HAVE_SYS_TYPES_H) && defined(HAVE_SIZE_T)
# include <sys/types.h>
#endif

#ifndef HAVE_FREEFN_T
typedef void (*FreeFn)(void *);
#endif

/*
 * Thread safe memory leak detection.  Leak reports dumped to stderr
 * on thread and program exit.
 */
extern void  track_init(void);
extern void  track_free(void *chunk, const char *here, long lineno);
extern void *track_malloc(size_t size, const char *here, long lineno);
extern void *track_calloc(size_t n, size_t size, const char *here, long lineno);
extern void *track_realloc(void *chunk, size_t size, const char *here, long lineno);
extern void *track_aligned_alloc(size_t alignment, size_t size, const char *here, long lineno);

#ifdef TRACK
# define free(p)		track_free(p, __func__, __LINE__)
# define malloc(s)		track_malloc(s, __func__, __LINE__)
# define calloc(n,s)		track_calloc(n, s, __func__, __LINE__)
# define realloc(p,s)		track_realloc(p, s, __func__, __LINE__)
# define aligned_alloc(n,s)	track_aligned_alloc(n, s, __func__, __LINE__)
#endif

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_sys_track_h__ */
