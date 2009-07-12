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

