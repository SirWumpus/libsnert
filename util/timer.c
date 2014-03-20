/*
 * timer.c
 *
 * Copyright 2009, 2013 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 ***
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include <com/snert/lib/util/timer.h>

#ifdef DEBUG_MALLOC
# include <com/snert/lib/util/DebugMalloc.h>
#endif

/***********************************************************************
 *** Timer Thread
 ***********************************************************************/

#if defined(HAVE_PTHREAD_COND_TIMEDWAIT)
static CLOCK time_zero = { 0, 0 };

static void *
timerThread(void *_data)
{
	int error;
	TIMER_DECLARE(period);
	struct timespec abstime, delay;
	Timer *timer = (Timer *) _data;

	PTHREAD_MUTEX_LOCK(&timer->mutex);

	/* Set initial delay. */
	delay = *(struct timespec *) &timer->delay;
#if !defined(HAVE_CLOCK_GETTIME) && defined(HAVE_GETTIMEOFDAY)
	delay.tv_nsec *= 1000;
#endif
	timespecSetAbstime(&abstime, &delay);

	while ((error = pthread_cond_timedwait(&timer->cv, &timer->mutex, &abstime)) != 0) {
		if (error != ETIMEDOUT || timer->task == NULL)
			break;

		TIMER_START(period);
		(*timer->task)(timer);
		pthread_testcancel();
		if (memcmp(&timer->period, &time_zero, sizeof (time_zero)) == 0)
			break;

		/* Compute execution time of task. */
		TIMER_DIFF(period);

		/* Compute remainder of period. */
		period = timer->period;
		CLOCK_SUB(&period, &TIMER_DIFF_VAR(period));
#if !defined(HAVE_CLOCK_GETTIME) && defined(HAVE_GETTIMEOFDAY)
		period.tv_usec *= 1000;
#endif
		/* Set end of next iteration. */
		timespecSetAbstime(&abstime, (struct timespec *) &period);
	}

	PTHREAD_MUTEX_UNLOCK(&timer->mutex);

#if defined(__WIN32__) || defined(__CYGWIN__)
	pthread_exit(NULL);
#endif
	return NULL;
}

#else /* defined(HAVE_PTHREAD_COND_TIMEDWAIT) */

static int
timerIsCanceled(Timer *timer)
{
#if defined(__WIN32__) || defined(__CYGWIN__)
	return WaitForSingleObject(timer->cancel_event, 0) == WAIT_OBJECT_0;
#else
	return 0;
#endif
}

static void *
timerThread(void *_data)
{
	TIMER_DECLARE(period);
	Timer *timer = (Timer *) _data;

#if defined(HAVE_STRUCT_TIMESPEC)
	pthreadSleep(timer->delay.tv_sec, timer->delay.tv_nsec);
#elif defined(HAVE_STRUCT_TIMEVAL)
	pthreadSleep(timer->delay.tv_sec, timer->delay.tv_usec);
#else
	pthreadSleep(timer->delay, 0);
#endif
	do {
		if (timer->task == NULL)
			break;

		TIMER_START(period);
#ifdef __unix__
		pthread_testcancel();
#endif
#if defined(__WIN32__) || defined(__CYGWIN__)
		if (timerIsCanceled(timer))
			break;
#endif
		(*timer->task)(timer);
#ifdef __unix__
		pthread_testcancel();
#endif
#if defined(__WIN32__) || defined(__CYGWIN__)
		if (timerIsCanceled(timer))
			break;
#endif
		TIMER_DIFF(period);

		period = timer->period;
		CLOCK_SUB(&period, &TIMER_DIFF_VAR(period));
#if defined(HAVE_STRUCT_TIMESPEC)
		pthreadSleep(period.tv_sec, period.tv_nsec);
#elif defined(HAVE_STRUCT_TIMEVAL)
		pthreadSleep(period.tv_sec, period.tv_usec);
#else
		pthreadSleep(period, 0);
#endif
	} while (TIMER_NE_CONST(timer->period, 0, 0));

	return NULL;
}
#endif /* defined(HAVE_PTHREAD_COND_TIMEDWAIT) */


/**
 * @param task
 *	A call-back function to be executed when delay/period expire.
 *
 * @param data
 *	Pointer to application task data.
 *
 * @param delay
 *	An initial delay in seconds before the first execution of the task.
 *
 * @param period
 *	The interval in seconds between repeated executions of the task.
 *
 * @param stack_size
 *	Stack size for the timer task thread.
 *
 * @return
 *	A pointer to a Timer structure. Otherwise NULL on error.
 */
Timer *
timerCreate(TimerTask task, void *data, CLOCK *delay, CLOCK *period, size_t stack_size)
{
	Timer *timer;
	pthread_attr_t *pthread_attr_ptr = NULL;

	if (task == NULL || (delay == NULL && period == NULL))
		goto error0;

	if ((timer = calloc(1, sizeof (*timer))) == NULL)
		goto error0;

#if defined(HAVE_PTHREAD_COND_TIMEDWAIT)
	if (pthread_cond_init(&timer->cv, NULL))
		goto error1;

	if (pthread_mutex_init(&timer->mutex, NULL))
		goto error2;
#endif
#if defined(__WIN32__) || defined(__CYGWIN__)
	timer->cancel_event = CreateEvent(NULL, 0, 0, NULL);
#endif
#if defined(HAVE_PTHREAD_ATTR_INIT)
{
	pthread_attr_t pthread_attr;

	if (pthread_attr_init(&pthread_attr) == 0) {
# if defined(HAVE_PTHREAD_ATTR_SETSCOPE)
		(void) pthread_attr_setscope(&pthread_attr, PTHREAD_SCOPE_SYSTEM);
# endif
# if defined(HAVE_PTHREAD_ATTR_SETSTACKSIZE)
		if (stack_size < PTHREAD_STACK_MIN)
			stack_size = PTHREAD_STACK_MIN;
		(void) pthread_attr_setstacksize(&pthread_attr, stack_size);
# endif
		pthread_attr_ptr = &pthread_attr;
	}
}
#endif
	timer->task = task;
	timer->data = data;

	if (delay != NULL)
		timer->delay = *delay;
	if (period != NULL)
		timer->period = *period;

	if (pthread_create(&timer->thread, NULL, timerThread, timer))
		goto error3;

#if defined(HAVE_PTHREAD_ATTR_INIT)
	if (pthread_attr_ptr != NULL)
		(void) pthread_attr_destroy(pthread_attr_ptr);
#endif
	return timer;
error3:
#if defined(HAVE_PTHREAD_COND_TIMEDWAIT)
	(void) pthreadMutexDestroy(&timer->mutex);
error2:
	(void) pthread_cond_destroy(&timer->cv);
error1:
#endif
	free(timer);
error0:
	return NULL;
}

/**
 * @param
 *	A pointer to a Timer structure to cancel and free.
 */
void
timerFree(void *_timer)
{
	Timer *timer = (Timer *) _timer;

	if (timer != NULL) {
		timer->task = NULL;
		CLOCK_SUB(&timer->period, &timer->period);
#ifdef __unix__
		PTHREAD_MUTEX_LOCK(&timer->mutex);
# if defined(HAVE_PTHREAD_COND_TIMEDWAIT)
		(void) pthread_cond_signal(&timer->cv);
# else
		(void) pthread_cancel(timer->thread);
# endif
		PTHREAD_MUTEX_UNLOCK(&timer->mutex);
#endif
#if defined(__WIN32__) || defined(__CYGWIN__)
		SetEvent(timer->cancel_event);
#endif
		(void) pthread_join(timer->thread, NULL);
#if defined(HAVE_PTHREAD_COND_TIMEDWAIT)
		(void) pthread_cond_destroy(&timer->cv);
		(void) pthreadMutexDestroy(&timer->mutex);
#endif
#if defined(__WIN32__) || defined(__CYGWIN__)
		CloseHandle(timer->cancel_event);
#endif
		if (timer->free_data != NULL)
			(*timer->free_data)(timer->data);
		free(timer);
	}
}

#ifdef TEST
/***********************************************************************
 *** Timer Test
 ***********************************************************************/

#include <stdio.h>

static long counter;

static void
task(Timer *timer)
{
	if (0 <= counter)
		printf("%ld\n", counter--);

	if (counter <= 0) {
		timer->period.tv_nsec = 0;
		timer->period.tv_sec = 0;
	}
}

int
main(int argc, char **argv)
{
	Timer *timer;
	CLOCK period = { 1, 0 };

	counter = 10;
	if (1 < argc)
		counter = strtol(argv[1], NULL, 10);

	if ((timer = timerCreate(task, NULL, NULL, &period, 0)) == NULL)
		return 1;

	sleep(6);
	timerFree(timer);

	return 0;
}

#endif
