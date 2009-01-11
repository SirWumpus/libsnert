/*
 * TimeStamp.c
 *
 * Fill a buffer with a RFC 2821 timestamp string.
 *
 * Copyright 2006 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <string.h>

#include <com/snert/lib/sys/Time.h>

size_t
TimeStamp(time_t *now, char *buffer, size_t size)
{
	char *tz;
	int offset;
	long length;
	struct tm gmt;
	struct tm local;

	if (gmtime_r(now, &gmt) == NULL)
		return 0;

	if (localtime_r(now, &local) == NULL)
		return 0;

	length = strftime(buffer, size, "%a, %d %b %Y %H:%M:%S +0000", &local);

	/* Reformat the numerical time zone. */
	tz = &buffer[length - 5];

	/* Make sure the string was not truncated. */
	if (length < 5 || *tz != '+')
		return 0;

	/* Taken from sendmail/arpadate.c */
	offset = (local.tm_hour - gmt.tm_hour) * 60 + local.tm_min - gmt.tm_min;

	/* Assume that offset isn't more than a day. */
	if (local.tm_year < gmt.tm_year)
		offset -= 24 * 60;
	else if (local.tm_year > gmt.tm_year)
		offset += 24 * 60;
	else if (local.tm_yday < gmt.tm_yday)
		offset -= 24 * 60;
	else if (local.tm_yday > gmt.tm_yday)
		offset += 24 * 60;

	if (offset < 0) {
		offset = -offset;
		*tz++ = '-';
	} else {
		*tz++ = '+';
	}

	*tz++ = (char)((offset / 600) + '0');
	*tz++ = (char)((offset / 60) % 10 + '0');
	offset %= 60;
	*tz++ = (char)((offset / 10) + '0');
	*tz   = (char)((offset % 10) + '0');

	return length;
}
