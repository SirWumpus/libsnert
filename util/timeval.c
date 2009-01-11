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
timevalSet(struct timeval *acc, unsigned long us)
{
	acc->tv_sec = us / UNIT_MICRO;
	acc->tv_usec = us % UNIT_MICRO;
}

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
