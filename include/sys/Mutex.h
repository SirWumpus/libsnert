/*
 * Mutex.h
 *
 * Copyright 2001, 2004 by Anthony Howe.  All rights reserved.
 *
 * Ralf S. Engelschall's MM Library, while nice, appears to be incomplete:
 * no POSIX semaphores, permission issues for SysV, and constant need to
 * lock/unlock sections before doing a memory allocation or free. The last
 * concerns me greatly, because some specialised routines really want
 * monitor-like behaviour, only one user in the entire routine at a time.
 */

#ifndef __com_snert_lib_sys_Mutex_h__
#define __com_snert_lib_sys_Mutex_h__	1

#ifndef __com_snert_lib_version_h__
#include <com/snert/lib/version.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern void *MutexCreate(const char *lockfile);
extern void MutexPreRelease(void *mp);
extern void MutexDestroy(void *mp);
extern int MutexPermission(void *mp, int mode, int user, int group);
extern int MutexLock(void *mp);
extern int MutexUnlock(void *mp);

#if ! defined(__WIN32__) && ! defined(HAVE_PTHREAD_MUTEX_INIT)

typedef unsigned long pthread_mutex_t;
typedef unsigned long pthread_mutexattr_t;

# define PTHREAD_MUTEX_INITIALIZER	0

extern int pthread_mutex_init(pthread_mutex_t *, const pthread_mutexattr_t *);
extern int pthread_mutex_lock(pthread_mutex_t *);
extern int pthread_mutex_unlock(pthread_mutex_t *);
extern int pthread_mutex_destroy(pthread_mutex_t *);

#endif /* ! defined(__WIN32__) && ! defined(HAVE_PTHREAD_MUTEX_INIT) */

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_sys_Mutex_h__ */
