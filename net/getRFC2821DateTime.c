/*
 * getRFC2821DateTime.c
 *
 * Copyright 2008, 2009 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 ***
 ***********************************************************************/

#include <stdio.h>
#include <string.h>

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

#include <com/snert/lib/net/network.h>

#ifdef __WIN32__
# include <windows.h>

/*
 * Windows is so fucked. Thay can't follow a simple standard like ANSI C.
 */
static int
getTimeZone(char *buffer, size_t size)
{
	int bias, hh, mm;
	TIME_ZONE_INFORMATION info;

	(void) GetTimeZoneInformation(&info);

	bias = -(info.Bias + info.DaylightBias);
	hh = bias / 60;
	mm = bias - hh * 60;
	if (mm < 0)
		mm = -mm;

	return snprintf(buffer, size, "%+.2d%.2d", hh, mm);
}
#endif

int
getRFC2821DateTime(struct tm *local, char *buffer, size_t size)
{
	int length;

#ifdef __WIN32__
	length = strftime(buffer, size, "%a, %d %b %Y %H:%M:%S ", local);
	length += getTimeZone(buffer+length, size-length);
#else
	length = strftime(buffer, size, "%a, %d %b %Y %H:%M:%S %z", local);
#endif
	return length;
}
