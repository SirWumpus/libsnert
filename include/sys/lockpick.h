/*
 * lockpick.h
 *
 * Copyright 2010 by Anthony Howe.  All rights reserved.
 */

#ifndef __com_snert_lib_sys_lockpick_h__
#define __com_snert_lib_sys_lockpick_h__		1

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>

extern int lp_mutex_lock(pthread_mutex_t *, const char *file, unsigned line);
extern int lp_mutex_unlock(pthread_mutex_t *, const char *file, unsigned line);
extern int lp_mutex_trylock(pthread_mutex_t *, const char *file, unsigned line);
extern int lp_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex, const char *file, unsigned line);
extern int lp_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *abstime, const char *file, unsigned line);

/*
 * Including this header in an application allows us to replace
 * reference to these functions with versions that can help pin
 * point trouble spots in the application source.
 */
#define pthread_mutex_lock(m)		lp_mutex_lock(m, __FILE__, __LINE__)
#define pthread_mutex_unlock(m)		lp_mutex_unlock(m, __FILE__, __LINE__)
#define pthread_mutex_trylock(m)	lp_mutex_trylock(m, __FILE__, __LINE__)
#define pthread_cond_wait(c,m)		lp_cond_wait(c, m, __FILE__, __LINE__)
#define pthread_cond_timedwait(c,m,t)	lp_cond_timedwait(c, m, t, __FILE__, __LINE__)

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_sys_lockpick_h__ */
