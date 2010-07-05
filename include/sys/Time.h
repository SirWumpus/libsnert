/*
 * Time.h
 *
 * 	ANSI C		seconds 	10^1
 *	BSD, SYSTEMV	micro-seconds 	10^-6
 *	POSIX		nano-seconds	10^-9
 *
 * Copyright 2002, 2005 by Anthony Howe.  All rights reserved.
 */

#ifndef __com_snert_lib_sys_Time_h__
#define __com_snert_lib_sys_Time_h__	1

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

#ifndef UNIT_ONE
#define UNIT_ONE	1
#endif

#ifndef UNIT_MILLI
#define UNIT_MILLI 	1000
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

/*
 * Redefined version of "struct timespec". See POSIX.1.
 */
typedef struct {
	long s;
	long ns;	/* 0 <= ns < 1 000 000 000 */
} Time;

#ifdef __cplusplus
extern "C" {
#endif

extern Time *TimeGetNow();
extern void TimeSetNow(Time *t);

extern Time *TimeClone(Time *t);
extern void TimeDestroy(Time *t);

extern void TimeAdd(Time *acc, Time *val);
extern void TimeSub(Time *acc, Time *val);

extern int TimeIsZero(Time *acc);

/*
 * Add an RFC 2821 style timestamp string to buffer.
 * Return number bytes added or zero on error.
 */
extern size_t TimeStampAdd(char *buffer, size_t size);
extern size_t TimeStamp(time_t *now, char *buffer, size_t size);
extern size_t TimeStampGMT(time_t *now, char *buffer, size_t size);

/*
 * www, dd MMM yyyy hh:mm:ss -zzzz\0 32 bytes long
 */
#define TIME_STAMP_MIN_SIZE	32

#if ! defined(HAVE_GMTIME_R)
extern struct tm *gmtime_r(const time_t *, struct tm *);
#endif
#if ! defined(HAVE_LOCALTIME_R)
extern struct tm *localtime_r(const time_t *, struct tm *);
#endif

extern unsigned int sleep(unsigned int);

/**
 * @param local
 *	A pointer to a struct tm previously prepared by locatime, localtime_r
 *	gmtime, or gmtime_r.
 *
 * @param buffer
 *	An output buffer of where the RFC 2821 formatted date-time C string
 *	is to be saved.
 *
 * @param size
 *	Size of the buffer.
 *
 * @return
 *	The length of the string in buffer.
 */
extern int getRFC2821DateTime(struct tm *local, char *buffer, size_t size);

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_sys_Time_h__ */
