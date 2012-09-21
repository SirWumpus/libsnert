/*
 * pthreadSleep.c
 *
 * Copyright 2007, 2010 by Anthony Howe.  All rights reserved.
 */

#include <com/snert/lib/version.h>

#include <errno.h>
#include <stdlib.h>

#ifdef TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# ifdef HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#include <com/snert/lib/sys/pthread.h>

#if defined(HAVE_PTHREAD_COND_TIMEDWAIT) && defined(HAVE_CLOCK_GETTIME) && defined(__unix__)
static int thread_sleep_ready;
static pthread_cond_t thread_sleep_cv;
static pthread_mutex_t thread_sleep_mutex;

#if defined(HAVE_PTHREAD_ATFORK)
# if defined(__CYGWIN__)
extern int pthread_atfork(void (*prepare)(void), void (*parent)(void), void (*child)(void));
# endif

static void
pthreadSleepAtForkChild(void)
{
	(void) pthreadMutexDestroy(&thread_sleep_mutex);
}
#endif

static void
pthreadSleepFini(void)
{
	(void) pthreadMutexDestroy(&thread_sleep_mutex);
	(void) pthread_cond_destroy(&thread_sleep_cv);
}

static int
pthreadSleepInit(void)
{
	if (pthread_cond_init(&thread_sleep_cv, NULL))
		return 0;

	if (pthread_mutex_init(&thread_sleep_mutex, NULL))
		return 0;

#if defined(HAVE_PTHREAD_ATFORK)
	if (pthread_atfork(NULL, NULL, pthreadSleepAtForkChild))
		return 0;
#endif
	if (atexit(pthreadSleepFini))
		return 0;

	return 1;
}

int
pthreadSleep(unsigned seconds, unsigned nanoseconds)
{
	int error = 0;
	struct timespec abstime;

	if (!thread_sleep_ready) {
		if (!pthreadSleepInit())
			return -1;
		thread_sleep_ready = 1;
	}

	clock_gettime(CLOCK_REALTIME, &abstime);
	abstime.tv_nsec += nanoseconds;
	abstime.tv_sec += seconds;

	if (1000000000UL <= abstime.tv_nsec) {
		abstime.tv_nsec -= 1000000000UL;
		abstime.tv_sec++;
	}

	PTHREAD_MUTEX_LOCK(&thread_sleep_mutex);

	/* Beware of possible infinite loops if something other than
	 * EINVAL or ETIMEDOUT are returned. What of EINTR?
	 */
	while ((error = pthread_cond_timedwait(&thread_sleep_cv, &thread_sleep_mutex, &abstime)) != ETIMEDOUT) {
		if (error == EINVAL)
			break;
	}

	PTHREAD_MUTEX_UNLOCK(&thread_sleep_mutex);

	return -(error != ETIMEDOUT);
}
#else

int
pthreadSleep(unsigned seconds, unsigned nanoseconds)
{
#if defined(__WIN32__)
	Sleep(seconds * 1000 + nanoseconds / 1000000);
#elif defined (HAVE_NANOSLEEP)
{
	struct timespec ts0, ts1, *sleep_time, *unslept_time, *tmp;

	sleep_time = &ts0;
	unslept_time = &ts1;
	ts0.tv_sec = seconds;
	ts0.tv_nsec = nanoseconds;

	while (nanosleep(sleep_time, unslept_time)) {
		tmp = sleep_time;
		sleep_time = unslept_time;
		unslept_time = tmp;
	}
}
#else
{
	unsigned unslept;

	while (0 < (unslept = sleep(seconds)))
		seconds = unslept;
}
#endif
	return 0;
}

#endif
