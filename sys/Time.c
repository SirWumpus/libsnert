/*
 * Time.c
 *
 * Fine resolution clock.
 *
 * Copyright 2002, 2005 by Anthony Howe.  All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#define UNKNOWN_API		0
#define ANSI_API		1	/* time(), seconds 10^1 */
#define BSD_API			2	/* gettimeofday(), micro-seconds 10^-6 */
#define POSIX_API		3	/* clock_gettime(), nano-seconds 10^-9 */

#include <stdlib.h>
#include <sys/types.h>

#include <com/snert/lib/version.h>
#include <com/snert/lib/sys/Time.h>

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

Time *
TimeGetNow()
{
	Time *t;

	if ((t = malloc(sizeof (*t))) != NULL)
		TimeSetNow(t);

	return t;
}

void
TimeSetNow(Time *t)
{
#if defined(HAVE_CLOCK_GETTIME) /* POSIX */
	(void) clock_gettime(CLOCK_REALTIME, (struct timespec *) t);
#elif defined(HAVE_GETTIMEOFDAY) /* BSD */
	struct timezone gmt = { 0, 0 };
	gettimeofday((struct timeval *) t, &gmt);
	t->ns *= UNIT_MILLI;
#elif defined(HAVE_TIME) /* ANSI */
	t->s = (unsigned long) time(NULL);
	t->ns = 0;
#else
# error "No fine resolution clock support defined."
#endif
}

Time *
TimeClone(Time *orig)
{
	Time *copy;

	if ((copy = malloc(sizeof (*copy))) != NULL)
		*copy = *orig;

	return copy;
}

void
TimeDestroy(Time *tp)
{
	if (tp != NULL)
		free(tp);
}

void
TimeAdd(Time *acc, Time *b)
{
	if (acc != NULL && b != NULL) {
		acc->s += b->s;
		acc->ns += b->ns;

		if (UNIT_NANO <= acc->ns) {
			acc->ns -= UNIT_NANO;
			acc->s++;
		}
	}
}

void
TimeSub(Time *acc, Time *b)
{
	if (acc != NULL && b != NULL) {
		if (acc->ns < b->ns) {
			acc->ns += UNIT_NANO;
			acc->s--;
		}
		acc->ns -= b->ns;
		acc->s -= b->s;
	}
}

int
TimeIsZero(Time *acc)
{
	return acc->s == 0 && acc->ns == 0;
}

size_t
TimeStampAdd(char *buffer, size_t size)
{
	time_t now = time(NULL);

	return TimeStamp(&now, buffer, size);
}

