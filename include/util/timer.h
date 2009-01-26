/*
 * timer.h
 *
 * Copyright 2008 by Anthony Howe.  All rights reserved.
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

#if defined(__MINGW32__)
struct timespec {
	time_t  tv_sec;   /* Seconds */
	long    tv_nsec;  /* Nanoseconds */
};
#endif

extern void timespecSet(struct timespec *acc, unsigned long ns);
extern void timespecAdd(struct timespec *acc, struct timespec *b);
extern void timespecSubtract(struct timespec *acc, struct timespec *b);

extern void timevalSet(struct timeval *acc, unsigned long us);
extern void timevalAdd(struct timeval *acc, struct timeval *b);
extern void timevalSubtract(struct timeval *acc, struct timeval *b);

#ifdef HAVE_CLOCK_GETTIME

# define TIMER_DECLARE(t)	struct timespec t, diff_ ## t
# define TIMER_START(t)		clock_gettime(CLOCK_REALTIME, &(t))
# define TIMER_DIFF(t)		clock_gettime(CLOCK_REALTIME, &(diff_ ## t)); \
				timespecSubtract(&(diff_ ## t), &(t))

# define TIMER_EQ_CONST(t,s,ns)	((t).tv_sec == (s) && (t).tv_nsec == (ns))
# define TIMER_NE_CONST(t,s,ns)	!TIMER_EQ_CONST(t,s,ns)
# define TIMER_GT_CONST(t,s,ns)	((t).tv_sec >  (s) && (t).tv_nsec >  (ns))
# define TIMER_LT_CONST(t,s,ns)	((t).tv_sec <  (s) && (t).tv_nsec <  (ns))
# define TIMER_GE_CONST(t,s,ns)	((t).tv_sec >= (s) && (t).tv_nsec >= (ns))
# define TIMER_LE_CONST(t,s,ns)	((t).tv_sec <= (s) && (t).tv_nsec <= (ns))

# define TIMER_FORMAT		"%ld.%.9ld"
# define TIMER_FORMAT_ARG(t)	(long)(t).tv_sec, (t).tv_nsec

#elif HAVE_GETTIMEOFDAY

# define TIMER_DECLARE(t)	struct timeval t, diff_ ## t
# define TIMER_START(t)		gettimeofday(&(t), NULL)
# define TIMER_DIFF(t)		gettimeofday(&(diff_ ## t), NULL); \
				timevalSubtract(&(diff_ ## t), &(t))

# define TIMER_EQ_CONST(t,s,ns)	((t).tv_sec == (s) && (t).tv_usec == (ns))
# define TIMER_NE_CONST(t,s,ns)	!TIMER_EQ_CONST(t,s,ns)
# define TIMER_GT_CONST(t,s,ns)	((t).tv_sec >  (s) && (t).tv_usec >  (ns))
# define TIMER_LT_CONST(t,s,ns)	((t).tv_sec <  (s) && (t).tv_usec <  (ns))
# define TIMER_GE_CONST(t,s,ns)	((t).tv_sec >= (s) && (t).tv_usec >= (ns))
# define TIMER_LE_CONST(t,s,ns)	((t).tv_sec <= (s) && (t).tv_usec <= (ns))

# define TIMER_FORMAT		"%ld.%.6ld"
# define TIMER_FORMAT_ARG(t)	(long)(t).tv_sec, (t).tv_usec

#else

# define TIMER_DECLARE(t)	time_t t, diff_ ## t
# define TIMER_START(t)		(void) time(&(t))
# define TIMER_DIFF(t)		diff_ ## t = time(NULL)-(t)

# define TIMER_EQ_CONST(t,s,ns)	((t) == (s))
# define TIMER_NE_CONST(t,s,ns)	!TIMER_EQ_CONST(t,s,ns)
# define TIMER_GT_CONST(t,s,ns)	((t) >  (s))
# define TIMER_LT_CONST(t,s,ns)	((t) <  (s))
# define TIMER_GE_CONST(t,s,ns)	((t) >= (s))
# define TIMER_LE_CONST(t,s,ns)	((t) <= (s))

# define TIMER_FORMAT		"%ld"
# define TIMER_FORMAT_ARG(t)	(long) (t)

#endif

/***********************************************************************
 ***
 ***********************************************************************/

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_util_time62_h__ */
