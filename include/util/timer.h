/*
 * timer.h
 *
 * Copyright 2008, 2009 by Anthony Howe.  All rights reserved.
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

#if defined(__MINGW32__) && !defined(WIN32_STRUCT_TIMESPEC)
# define WIN32_STRUCT_TIMESPEC
struct timespec {
	time_t  tv_sec;   /* Seconds */
	long    tv_nsec;  /* Nanoseconds */
};
#endif

extern void timespecAdd(struct timespec *acc, struct timespec *b);
extern void timespecSubtract(struct timespec *acc, struct timespec *b);

extern void timevalAdd(struct timeval *acc, struct timeval *b);
extern void timevalSubtract(struct timeval *acc, struct timeval *b);

extern void timeAdd(time_t *acc, time_t *b);
extern void timeSubtract(time_t *acc, time_t *b);

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

# define TIMER_EQ_CONST(t,s,ns)		((t).tv_sec == (s) && (t).tv_nsec == (ns))
# define TIMER_NE_CONST(t,s,ns)		!TIMER_EQ_CONST(t,s,ns)
# define TIMER_GT_CONST(t,s,ns)		((t).tv_sec >  (s) && (t).tv_nsec >  (ns))
# define TIMER_LT_CONST(t,s,ns)		((t).tv_sec <  (s) && (t).tv_nsec <  (ns))
# define TIMER_GE_CONST(t,s,ns)		((t).tv_sec >= (s) && (t).tv_nsec >= (ns))
# define TIMER_LE_CONST(t,s,ns)		((t).tv_sec <= (s) && (t).tv_nsec <= (ns))

# define TIMER_GET_MS(a)		((a)->tv_sec * UNIT_MILLI + (a)->tv_nsec / 1000000L)

#elif defined(HAVE_GETTIMEOFDAY)
/* 10^-6 (micro-second) resolution */

# define CLOCK				struct timeval
# define CLOCK_ADD(a, b)		timevalAdd(a, b)
# define CLOCK_SUB(a, b)		timevalSubtract(a, b)
# define CLOCK_GET(a)			gettimeofday(a, NULL)
# define CLOCK_FMT			"%ld.%.6ld"
# define CLOCK_FMT_DOT(a)		(long)(a).tv_sec, (a).tv_usec
# define CLOCK_FMT_PTR(a)		(long)(a)->tv_sec, (a)->tv_usec
# define CLOCK_SET_TIMESPEC(a,b)	(a)->tv_sec = (b)->tv_sec; (a)->tv_nsec = (b)->tv_usec * 1000

# define TIMER_EQ_CONST(t,s,ns)		((t).tv_sec == (s) && (t).tv_usec == (ns))
# define TIMER_NE_CONST(t,s,ns)		!TIMER_EQ_CONST(t,s,ns)
# define TIMER_GT_CONST(t,s,ns)		((t).tv_sec >  (s) && (t).tv_usec >  (ns))
# define TIMER_LT_CONST(t,s,ns)		((t).tv_sec <  (s) && (t).tv_usec <  (ns))
# define TIMER_GE_CONST(t,s,ns)		((t).tv_sec >= (s) && (t).tv_usec >= (ns))
# define TIMER_LE_CONST(t,s,ns)		((t).tv_sec <= (s) && (t).tv_usec <= (ns))

# define TIMER_GET_MS(a)		((a)->tv_sec * UNIT_MILLI + (a)->tv_usec / 1000L)

#else
/* 1 second resolution */

struct timesec {
	time_t	tv_sec;
	long	tv_ignored;
};

# define CLOCK				struct timesec
# define CLOCK_ADD(a, b)		timeAdd(a, b)
# define CLOCK_SUB(a, b)		timeSub(a, b)
# define CLOCK_GET(a)			(void) time((a)->tv_sec); (a)->tv_ignored = 0
# define CLOCK_FMT			"%ld"
# define CLOCK_FMT_DOT(a)		(long)(a).tv_sec
# define CLOCK_FMT_PTR(a)		(long)(a)->tv_sec
# define CLOCK_SET_TIMESPEC(a,b)	(a)->tv_sec = (b)->tv_sec; (a)->tv_nsec = 0

# define TIMER_EQ_CONST(t,s,ns)		((t) == (s))
# define TIMER_NE_CONST(t,s,ns)		!TIMER_EQ_CONST(t,s,ns)
# define TIMER_GT_CONST(t,s,ns)		((t) >  (s))
# define TIMER_LT_CONST(t,s,ns)		((t) <  (s))
# define TIMER_GE_CONST(t,s,ns)		((t) >= (s))
# define TIMER_LE_CONST(t,s,ns)		((t) <= (s))

# define TIMER_GET_MS(a)		((a)->tv_sec * UNIT_MILLI)

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
};

/**
 * @param task
 *	A call-back function to be executed when delay/period expire.
 *
 * @param delay
 *	An initial delay in seconds before the first execution of the task.
 *
 * @param period
 *	The interval in seconds between repeated executions of the task.
 *
 * @param stack_size
 *	The stack size for the timer/task thread.
 *
 * @return
 *	A pointer to a Timer structure.
 */
extern Timer *timerCreate(TimerTask task, CLOCK *delay, CLOCK *period, size_t stack_size);

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
