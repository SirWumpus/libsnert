/*
 * timespec.c
 *
 * Copyright 2008 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 ***
 ***********************************************************************/

#include <com/snert/lib/version.h>
#include <com/snert/lib/util/timer.h>

/***********************************************************************
 *** timespec functions
 ***********************************************************************/

void
timespecSubtract(struct timespec *acc, struct timespec *b)
{
	if (acc->tv_nsec < b->tv_nsec) {
		acc->tv_nsec += UNIT_NANO;
		acc->tv_sec--;
	}

	acc->tv_nsec -= b->tv_nsec;
	acc->tv_sec -= b->tv_sec;
}

void
timespecAdd(struct timespec *acc, struct timespec *b)
{
	if (acc != NULL && b != NULL) {
		acc->tv_sec += b->tv_sec;
		acc->tv_nsec += b->tv_nsec;

		if (UNIT_NANO <= acc->tv_nsec) {
			acc->tv_nsec -= UNIT_NANO;
			acc->tv_sec++;
		}
	}
}

#ifdef HAVE_STRUCT_TIMESPEC
void
timespecToTimeval(struct timespec *a, struct timeval *b)
{
	b->tv_sec = a->tv_sec;
	b->tv_usec = a->tv_nsec / UNIT_MILLI;
}
#endif

void
timeSubtract(time_t *acc, time_t *b)
{
	*acc -= *b;
}

void
timeAdd(time_t *acc, time_t *b)
{
	*acc += *b;
}

