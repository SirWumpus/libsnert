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

/* The C Standard, 7.1.4, paragraph 1, states [ISO/IEC 9899:2011]
 *
 * "Any macro definition of a function can be suppressed locally by
 *  enclosing the name of the function in parentheses, because the
 *  name is then not followed by the left parenthesis that indicates
 *  expansion of a macro function name."
 *
 * NetBSD 6.1.5 stdlib.h fails to define these in a macro safe manner
 * so that an application can provide macro equivalents.  This may be
 * true of other OSes.
 */
extern void  (free)(void *);
extern void *(malloc)(size_t);
extern void *(calloc)(size_t, size_t);
extern void *(realloc)(void *, size_t);
extern void *(aligned_alloc)(size_t, size_t);

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_sys_track_h__ */
