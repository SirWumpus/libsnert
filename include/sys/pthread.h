/*
 * pthread.h
 *
 * Copyright 2004, 2009 by Anthony Howe.  All rights reserved.
 */

#ifndef __com_snert_lib_sys_pthread_h__
#define __com_snert_lib_sys_pthread_h__	1

# ifdef __cplusplus
extern "C" {
# endif

# if defined(__WIN32__)
#  include <windows.h>
#  include <com/snert/lib/sys/Time.h>

# ifndef ETIMEDOUT
# define ETIMEDOUT	WSAETIMEDOUT
# endif

#  define HAVE_PTHREAD_T
typedef HANDLE pthread_t;

#  define HAVE_PTHREAD_ATTR_T
typedef struct {
	size_t stack_size;
} pthread_attr_t;

#  define HAVE_PTHREAD_MUTEX_T
typedef HANDLE pthread_mutex_t;

#  define HAVE_PTHREAD_MUTEXATTR_T
typedef long pthread_mutexattr_t;

#  define HAVE_PTHREAD_COND_T
/* http://www.cs.wustl.edu/~schmidt/win32-cv-1.html */
typedef struct {
	int waiters_count_;
	// Number of waiting threads.

	CRITICAL_SECTION waiters_count_lock_;
	// Serialize access to <waiters_count_>.

	HANDLE sema_;
	// Semaphore used to queue up threads waiting for the condition to
	// become signaled.

	HANDLE waiters_done_;
	// An auto-reset event used by the broadcast/signal thread to wait
	// for all the waiting thread(s) to wake up and be released from the
	// semaphore.

	size_t was_broadcast_;
	// Keeps track of whether we were broadcasting or signaling.  This
	// allows us to optimize the code if we're just signaling.
} pthread_cond_t;

#  define HAVE_PTHREAD_CONDATTR_T
typedef long pthread_condattr_t;

#  define PTHREAD_MUTEX_INITIALIZER	0

#  define HAVE_PTHREAD_CANCEL
#  define HAVE_PTHREAD_CREATE
#  define HAVE_PTHREAD_DETACH
#  define HAVE_PTHREAD_EXIT
#  define HAVE_PTHREAD_JOIN
#  define HAVE_PTHREAD_SELF
#  define HAVE_PTHREAD_YIELD
#  define HAVE_PTHREAD_MUTEX_INIT
#  define HAVE_PTHREAD_MUTEX_DESTROY
#  define HAVE_PTHREAD_MUTEX_LOCK
#  define HAVE_PTHREAD_MUTEX_TRYLOCK
#  define HAVE_PTHREAD_MUTEX_UNLOCK
#  define HAVE_PTHREAD_TESTCANCEL
#  define HAVE_PTHREAD_ATTR_INIT
#  define HAVE_PTHREAD_ATTR_DESTROY
#  define HAVE_PTHREAD_ATTR_SETSTACKSIZE
#  define HAVE_PTHREAD_ATTR_GETSTACKSIZE
#  define HAVE_PTHREAD_COND_INIT
#  define HAVE_PTHREAD_COND_SIGNAL
#  define HAVE_PTHREAD_COND_BROADCAST
#  define HAVE_PTHREAD_COND_WAIT
#  define HAVE_PTHREAD_COND_TIMEDWAIT
#  define HAVE_PTHREAD_COND_DESTROY
#  define HAVE_PTHREAD_CLEANUP_PUSH
#  define HAVE_PTHREAD_CLEANUP_POP

struct _pthread_cleanup_buffer {
	struct _pthread_cleanup_buffer *next;
	void (*cleanup_fn)(void *);
	void *cleanup_arg;
};

extern void _pthread_cleanup_push(struct _pthread_cleanup_buffer *buffer, void (*fn)(void *), void *arg);
extern void _pthread_cleanup_pop(struct _pthread_cleanup_buffer *buffer, int execute);

#define pthread_cleanup_push(fn, arg)	{ struct _pthread_cleanup_buffer _buf; _pthread_cleanup_push(&_buf, (fn), (arg))

#define pthread_cleanup_pop(execute)	_pthread_cleanup_pop(&_buf, (execute)); }

extern int pthreadInit(void);
extern void pthreadFini(void);

extern int pthread_attr_init(pthread_attr_t *attr);
extern int pthread_attr_destroy(pthread_attr_t *attr);
extern int pthread_attr_setstacksize(pthread_attr_t *attr, size_t stacksize);
extern int pthread_attr_getstacksize(const pthread_attr_t *attr, size_t *stacksize);
extern int pthread_create(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);

extern pthread_t pthread_self(void);
extern int pthread_detach(pthread_t);
extern void pthread_yield(void);
extern int pthread_join(pthread_t, void **);

extern int pthread_cancel(pthread_t);

extern int pthread_setcancelstate(int new_state, int *old_state);

#ifndef PTHREAD_CANCEL_ENABLE
#define PTHREAD_CANCEL_ENABLE		1
#endif
#ifndef PTHREAD_CANCEL_DISABLE
#define PTHREAD_CANCEL_DISABLE		0
#endif

/**
 * Explcit check for cancel state. Windows / mingw does not support POSIX
 * cancellation points, therefore the application must make diligent use
 * of pthread_testcancel in order for pthread_cancel to function.
 *
 * If the thread's cancel state has been set, then pthread_testcancel will
 * call pthread_exit to run thread cleanup handlers before thread exit.
 */
extern void pthread_testcancel(void);

/**
 * The thread terminates itself. In order for Windows to properly cleanup,
 * pthread_exit must be called from the thread function, instead of simply
 * returning from the function.
 */
extern void pthread_exit(void *value_ptr);

extern int pthread_mutex_init(pthread_mutex_t *, const pthread_mutexattr_t *);
extern int pthread_mutex_lock(pthread_mutex_t *);
extern int pthread_mutex_trylock(pthread_mutex_t *);
extern int pthread_mutex_unlock(pthread_mutex_t *);
extern int pthread_mutex_destroy(pthread_mutex_t *);

extern int pthread_cond_init(pthread_cond_t *, const pthread_condattr_t *);
extern int pthread_cond_signal(pthread_cond_t *);
extern int pthread_cond_broadcast(pthread_cond_t *);
extern int pthread_cond_wait(pthread_cond_t *, pthread_mutex_t *);
extern int pthread_cond_timedwait(pthread_cond_t *, pthread_mutex_t *, const struct timespec *);
extern int pthread_cond_destroy(pthread_cond_t *);

# elif defined(HAVE_PTHREAD_H)
#  include <limits.h>
#  include <pthread.h>

# define pthreadInit()			0
# define pthreadFini()

# endif /* HAVE_PTHREAD_H */

# ifndef PTHREAD_STACK_MIN
#  define PTHREAD_STACK_MIN		16384
# endif

#ifdef DEBUG_MUTEX
# include <com/snert/lib/sys/lockpick.h>
#endif

#if defined(HAVE_PTHREAD_CLEANUP_PUSH)
/* If we are called because of pthread_cancel(), be
 * sure to cleanup the current locked mutex too.
 */
# define PTHREAD_MUTEX_LOCK(m)		if (!pthread_mutex_lock(m)) { \
						pthread_cleanup_push((void (*)(void*)) pthread_mutex_unlock, (m));

# define PTHREAD_MUTEX_UNLOCK(m)		; \
						pthread_cleanup_pop(1); \
					}

# define PTHREAD_PUSH_FREE(p)		pthread_cleanup_push(free, p)
# define PTHREAD_POP_FREE(x, p)		; pthread_cleanup_pop(x); if (x) { p = NULL; }

#else

# define PTHREAD_MUTEX_LOCK(m)		if (!pthread_mutex_lock(m)) {

# define PTHREAD_MUTEX_UNLOCK(m)		; \
						(void) pthread_mutex_unlock(m); \
					}

# define PTHREAD_PUSH_FREE(p)
# define PTHREAD_POP_FREE(x, p)		if (x) { free(p); p = NULL; }

#endif /* defined(HAVE_PTHREAD_CLEANUP_PUSH) */

#if defined(HAVE_PTHREAD_YIELD) && defined(__linux__)
/* Stupid Linux wants stupid extension macros to declare a function it has
 * in the library. Wankers. OpenBSD is far more sensible in this regard.
 */
extern void pthread_yield(void);
#endif

#if !defined(HAVE_PTHREAD_YIELD)
# define pthread_yield()
#endif

/* Non-POSIX using pthread functions. */
extern int pthreadMutexDestroy(pthread_mutex_t *);
extern int pthreadSleep(unsigned seconds, unsigned nanoseconds);

/***********************************************************************
 ***
 ***********************************************************************/

# ifdef  __cplusplus
}
# endif

#endif /* __com_snert_lib_sys_pthread_h__ */

