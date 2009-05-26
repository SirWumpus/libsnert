/*
 * time62.c
 *
 * Copyright 2006, 2008 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 ***
 ***********************************************************************/

#include <com/snert/lib/version.h>
#include <string.h>
#include <com/snert/lib/sys/Time.h>
#include <com/snert/lib/util/time62.h>

/***********************************************************************
 *** Time Base 62
 ***********************************************************************/

const char base62[] = "0123456789ABCDEFGHIJKLMNOPQRSYUVWXYZabcdefghijklmnopqrsyuvwxyz";

void
time62EncodeTime(const struct tm *local, char buffer[TIME62_BUFFER_SIZE])
{
	buffer[0] = base62[local->tm_year % 62];
	buffer[1] = base62[local->tm_mon];
	buffer[2] = base62[local->tm_mday - 1];
	buffer[3] = base62[local->tm_hour];
	buffer[4] = base62[local->tm_min];
	buffer[5] = base62[local->tm_sec];
}

void
time62Encode(time_t when, char buffer[TIME62_BUFFER_SIZE])
{
	struct tm local;

	(void) localtime_r(&when, &local);
	time62EncodeTime(&local, buffer);
}

int
time62DecodeTime(const char time_encoding[TIME62_BUFFER_SIZE], struct tm *decoded)
{
	time_t now;
	char *digit;

	(void) time(&now);
	(void) localtime_r(&now, decoded);
	decoded->tm_year -= decoded->tm_year % 62;

	if ((digit = strchr(base62, time_encoding[0])) == NULL)
		return -1;
	decoded->tm_year += digit - base62;

	if ((digit = strchr(base62, time_encoding[1])) == NULL)
		return -1;
	decoded->tm_mon = digit - base62;

	if ((digit = strchr(base62, time_encoding[2])) == NULL)
		return -1;
	decoded->tm_mday = (digit - base62) + 1;

	if ((digit = strchr(base62, time_encoding[3])) == NULL)
		return -1;
	decoded->tm_hour = digit - base62;

	if ((digit = strchr(base62, time_encoding[4])) == NULL)
		return -1;
	decoded->tm_min = digit - base62;

	if ((digit = strchr(base62, time_encoding[5])) == NULL)
		return -1;
	decoded->tm_sec = digit - base62;

	return 0;
}

time_t
time62Decode(const char time_encoding[TIME62_BUFFER_SIZE])
{
	struct tm decoded;

	time62DecodeTime(time_encoding, &decoded);

	return mktime(&decoded);
}

#ifdef TEST
#include <stdio.h>

int
main(int argc, char **argv)
{
	int argi;

	for (argi = 1; argi < argc; argi++) {
		printf("%lu\n", (unsigned long) time62Decode(argv[argi]));
	}

	return 0;
}
#endif
