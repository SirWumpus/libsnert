/*
 * pthread.h
 *
 * Copyright 2004, 2005 by Anthony Howe.  All rights reserved.
 */

#ifndef __com_snert_lib_sys_pthread_h__
#define __com_snert_lib_sys_pthread_h__	1

#  ifdef __cplusplus
extern "C" {
#  endif

# if defined(__WIN32__)
#  include <windows.h>

#  define HAVE_PTHREAD_T
typedef HANDLE pthread_t;

#  define HAVE_PTHREAD_ATTR_T
typedef DWORD pthread_attr_t;

#  define HAVE_PTHREAD_MUTEX_T
typedef HANDLE pthread_mutex_t;

#  define HAVE_PTHREAD_MUTEXATTR_T
typedef long pthread_mutexattr_t;

#  define PTHREAD_MUTEX_INITIALIZER	0

#  ifndef HAVE_PTHREAD_CANCEL
#   define HAVE_PTHREAD_CANCEL
#  endif
#  ifndef HAVE_PTHREAD_CREATE
#   define HAVE_PTHREAD_CREATE
#  endif
#  ifndef HAVE_PTHREAD_DETACH
#   define HAVE_PTHREAD_DETACH
#  endif
#  ifndef HAVE_PTHREAD_EXIT
#   define HAVE_PTHREAD_EXIT
#  endif
#  ifndef HAVE_PTHREAD_JOIN
#   define HAVE_PTHREAD_JOIN
#  endif
#  ifndef HAVE_PTHREAD_SELF
#   define HAVE_PTHREAD_SELF
#  endif
#  ifndef HAVE_PTHREAD_YIELD
#   define HAVE_PTHREAD_YIELD
#  endif
#  ifndef HAVE_PTHREAD_MUTEX_INIT
#   define HAVE_PTHREAD_MUTEX_INIT
#  endif
#  ifndef HAVE_PTHREAD_MUTEX_DESTROY
#   define HAVE_PTHREAD_MUTEX_DESTROY
#  endif
#  ifndef HAVE_PTHREAD_MUTEX_LOCK
#   define HAVE_PTHREAD_MUTEX_LOCK
#  endif
#  ifndef HAVE_PTHREAD_MUTEX_TRYLOCK
#   define HAVE_PTHREAD_MUTEX_TRYLOCK
#  endif
#  ifndef HAVE_PTHREAD_MUTEX_UNLOCK
#   define HAVE_PTHREAD_MUTEX_UNLOCK
#  endif

extern int pthread_create(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
extern int pthread_detach(pthread_t);
extern pthread_t pthread_self(void);
extern void pthread_yield(void);
extern int pthread_join(pthread_t, void **);
extern int pthread_cancel(pthread_t);
extern void pthread_exit(void *value_ptr);

extern int pthread_mutex_init(pthread_mutex_t *, const pthread_mutexattr_t *);
extern int pthread_mutex_lock(pthread_mutex_t *);
extern int pthread_mutex_trylock(pthread_mutex_t *);
extern int pthread_mutex_unlock(pthread_mutex_t *);
extern int pthread_mutex_destroy(pthread_mutex_t *);

# elif defined(HAVE_PTHREAD_H)
#  include <pthread.h>
# endif /* HAVE_PTHREAD_H */

/* Non-POSIX using pthread functions. */
extern int pthreadMutexDestroy(pthread_mutex_t *);
extern int pthreadSleep(unsigned seconds, unsigned nanoseconds);

#  ifdef  __cplusplus
}
#  endif

#endif /* __com_snert_lib_sys_pthread_h__ */

