/*
 * timeval.c
 *
 * Copyright 2008 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 ***
 ***********************************************************************/

#include <com/snert/lib/version.h>
#include <com/snert/lib/util/timer.h>

/***********************************************************************
 *** timeval functions
 ***********************************************************************/

void
timevalSubtract(struct timeval *acc, struct timeval *b)
{
	if (acc->tv_usec < b->tv_usec) {
		acc->tv_usec += UNIT_MICRO;
		acc->tv_sec--;
	}

	acc->tv_usec -= b->tv_usec;
	acc->tv_sec -= b->tv_sec;
}

void
timevalAdd(struct timeval *acc, struct timeval *b)
{
	if (acc != NULL && b != NULL) {
		acc->tv_sec += b->tv_sec;
		acc->tv_usec += b->tv_usec;

		if (UNIT_MICRO <= acc->tv_usec) {
			acc->tv_usec -= UNIT_MICRO;
			acc->tv_sec++;
		}
	}
}


#ifdef HAVE_STRUCT_TIMESPEC
void
timevalToTimespec(struct timeval *a, struct timespec *b)
{
	b->tv_sec = a->tv_sec;
	b->tv_nsec = a->tv_usec * UNIT_MILLI;
}
#endif
