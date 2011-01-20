/*
 * timer.h
 *
 * Copyright 2008, 2010 by Anthony Howe.  All rights reserved.
 */

#ifndef __com_snert_lib_util_timer_h__
#define __com_snert_lib_util_timer_h__	1

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>

#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
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

/***********************************************************************
 *** Timer Macros
 ***********************************************************************/

#ifndef UNIT_ONE
#define UNIT_ONE	1
#endif

#ifndef UNIT_MILLI
#define UNIT_MILLI 	1000L
#endif

#ifndef UNIT_MICRO
#define UNIT_MICRO 	1000000L
#endif

#ifndef UNIT_NANO
#define UNIT_NANO 	1000000000L
#endif

#if !defined(HAVE_STRUCT_TIMESPEC)
# define HAVE_STRUCT_TIMESPEC
struct timespec {
	time_t  tv_sec;   /* Seconds */
	long    tv_nsec;  /* Nanoseconds */
};
#endif

extern void timespecAdd(struct timespec *acc, struct timespec *b);
extern void timespecSubtract(struct timespec *acc, struct timespec *b);
extern void timespecToTimeval(struct timespec *a, struct timeval *b);
extern void timespecSetAbstime(struct timespec *abstime, struct timespec *delay);

extern void timevalAdd(struct timeval *acc, struct timeval *b);
extern void timevalSubtract(struct timeval *acc, struct timeval *b);
extern void timevalToTimespec(struct timeval *a, struct timespec *b);

extern void timeAdd(time_t *acc, time_t *b);
extern void timeSubtract(time_t *acc, time_t *b);

#define timeToTimespec(a, b)		(b)->tv_tv_sec = *(a); (b)->tv_nsec = 0
#define timeToTimeval(a, b)		(b)->tv_tv_sec = *(a); (b)->tv_usec = 0

#define timespecGetMs(a)		((a)->tv_sec * UNIT_MILLI + (a)->tv_nsec / 1000000L)
#define timespecSetMs(a,ms)		(a)->tv_sec = ms / UNIT_MILLI; (a)->tv_nsec = (ms % UNIT_MILLI) * UNIT_MICRO
#define timespecCmp(a, CMP, b)		(((a)->tv_sec == (b)->tv_sec) ? ((a)->tv_nsec CMP (b)->tv_nsec) : ((a)->tv_sec CMP (b)->tv_sec))

#define timevalGetMs(a)			((a)->tv_sec * UNIT_MILLI + (a)->tv_usec / 1000L)
#define timevalSetMs(a,ms)		 (a)->tv_sec = ms / UNIT_MILLI; (a)->tv_usec = (ms % UNIT_MILLI) * UNIT_MILLI
#define timevalCmp(a, CMP, b)		(((a)->tv_sec == (b)->tv_sec) ? ((a)->tv_usec CMP (b)->tv_usec) : ((a)->tv_sec CMP (b)->tv_sec))

#if defined(HAVE_CLOCK_GETTIME)
/* 10^-9 (nano-second) resolution */

# define CLOCK				struct timespec
# define CLOCK_ADD(a, b)		timespecAdd(a, b)
# define CLOCK_SUB(a, b)		timespecSubtract(a, b)
# define CLOCK_GET(a)			clock_gettime(CLOCK_REALTIME, a)
# define CLOCK_FMT			"%ld.%.9ld"
# define CLOCK_FMT_DOT(a)		(long)(a).tv_sec, (a).tv_nsec
# define CLOCK_FMT_PTR(a)		(long)(a)->tv_sec, (a)->tv_nsec
# define CLOCK_SET_TIMESPEC(a,b)	*(a) = *(b)
# define CLOCK_SET_TIMEVAL(a,b)		timespecToTimeval(b,a)

# define TIMER_EQ_CONST(t,s,ns)		((t).tv_sec == (s) && (t).tv_nsec == (ns))
# define TIMER_NE_CONST(t,s,ns)		!TIMER_EQ_CONST(t,s,ns)
# define TIMER_GT_CONST(t,s,ns)		((t).tv_sec >  (s) && (t).tv_nsec >  (ns))
# define TIMER_LT_CONST(t,s,ns)		((t).tv_sec <  (s) && (t).tv_nsec <  (ns))
# define TIMER_GE_CONST(t,s,ns)		((t).tv_sec >= (s) && (t).tv_nsec >= (ns))
# define TIMER_LE_CONST(t,s,ns)		((t).tv_sec <= (s) && (t).tv_nsec <= (ns))

# define TIMER_GET_MS(a)		timespecGetMs(a)
# define TIMER_SET_MS(a,ms)		timespecSetMs(a,ms)

#elif defined(HAVE_GETTIMEOFDAY)
/* 10^-6 (micro-second) resolution */

# define CLOCK				struct timeval
# define CLOCK_ADD(a, b)		timevalAdd(a, b)
# define CLOCK_SUB(a, b)		timevalSubtract(a, b)
# define CLOCK_GET(a)			gettimeofday(a, NULL)
# define CLOCK_FMT			"%ld.%.6ld"
# define CLOCK_FMT_DOT(a)		(long)(a).tv_sec, (a).tv_usec
# define CLOCK_FMT_PTR(a)		(long)(a)->tv_sec, (a)->tv_usec
# define CLOCK_SET_TIMESPEC(a,b)	timevalToTimespec(b,a)
# define CLOCK_SET_TIMEVAL(a,b)		*(a) = *(b)

# define TIMER_EQ_CONST(t,s,ns)		((t).tv_sec == (s) && (t).tv_usec == (ns))
# define TIMER_NE_CONST(t,s,ns)		!TIMER_EQ_CONST(t,s,ns)
# define TIMER_GT_CONST(t,s,ns)		((t).tv_sec >  (s) && (t).tv_usec >  (ns))
# define TIMER_LT_CONST(t,s,ns)		((t).tv_sec <  (s) && (t).tv_usec <  (ns))
# define TIMER_GE_CONST(t,s,ns)		((t).tv_sec >= (s) && (t).tv_usec >= (ns))
# define TIMER_LE_CONST(t,s,ns)		((t).tv_sec <= (s) && (t).tv_usec <= (ns))

# define TIMER_GET_MS(a)		timevalGetMs(a)
# define TIMER_SET_MS(a,ms)		timevalSetMs(a,ms)

#else
/* 1 second resolution */

# define CLOCK				time_t
# define CLOCK_ADD(a, b)		timeAdd(a, b)
# define CLOCK_SUB(a, b)		timeSub(a, b)
# define CLOCK_GET(a)			(void) time(a)
# define CLOCK_FMT			"%ld"
# define CLOCK_FMT_DOT(a)		(long)*(a)
# define CLOCK_FMT_PTR(a)		(long)*(a)
# define CLOCK_SET_TIMESPEC(a,b)	timeToTimespec(b,a)
# define CLOCK_SET_TIMEVAL(a,b)		timeToTimeval(b,a)

# define TIMER_EQ_CONST(t,s,ns)		((t) == (s))
# define TIMER_NE_CONST(t,s,ns)		!TIMER_EQ_CONST(t,s,ns)
# define TIMER_GT_CONST(t,s,ns)		((t) >  (s))
# define TIMER_LT_CONST(t,s,ns)		((t) <  (s))
# define TIMER_GE_CONST(t,s,ns)		((t) >= (s))
# define TIMER_LE_CONST(t,s,ns)		((t) <= (s))

# define TIMER_GET_MS(a)		(*(a) * UNIT_MILLI)
# define TIMER_SET_MS(a,ms)		*(a) = ms / UNIT_MILLI

#endif

#define TIMER_DIFF_VAR(t)		diff_ ## t
#define TIMER_DECLARE(t)		CLOCK t, TIMER_DIFF_VAR(t)
#define TIMER_START(t)			CLOCK_GET(&(t))
#define TIMER_DIFF(t)			CLOCK_GET(&(TIMER_DIFF_VAR(t))); \
					CLOCK_SUB(&(TIMER_DIFF_VAR(t)), &(t))
#define TIMER_FORMAT			CLOCK_FMT
#define TIMER_FORMAT_ARG(t)		CLOCK_FMT_DOT(t)

typedef struct timer Timer;
typedef void (*TimerTask)(Timer *);
typedef void (*TimerFreeData)(void *);

struct timer {
	pthread_t thread;
#if defined(HAVE_PTHREAD_COND_TIMEDWAIT)
	pthread_cond_t cv;
	pthread_mutex_t mutex;
#endif
#ifdef __WIN32__
	HANDLE cancel_event;
#endif
	CLOCK delay;
	CLOCK period;
	TimerTask task;
	TimerFreeData free_data;
	void *data;
};

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
extern Timer *timerCreate(TimerTask task, void *data, CLOCK *delay, CLOCK *period, size_t stack_size);

/**
 * @param
 *	A pointer to a Timer structure to free.
 */
extern void timerFree(void *_timer);

/***********************************************************************
 ***
 ***********************************************************************/

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_util_time62_h__ */
