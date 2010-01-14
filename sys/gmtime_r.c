/*
 * gmtime_r.c
 *
 * Copyright 2004, 2005 by Anthony Howe.  All rights reserved.
 */

#include <com/snert/lib/version.h>
#include <com/snert/lib/sys/Time.h>
#include <com/snert/lib/sys/pthread.h>
#include <com/snert/lib/util/Text.h>

#if ! defined(HAVE_GMTIME_R) && defined(HAVE_GMTIME)
struct tm *
gmtime_r(const time_t *clock, struct tm *gmt)
{
	struct tm *result = NULL;
	static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

	PTHREAD_MUTEX_LOCK(&mutex);
	if ((result = gmtime(clock)) != NULL) {
		*gmt = *result;
		result = gmt;
	}
	PTHREAD_MUTEX_UNLOCK(&mutex);

	return result;
}
#endif


#if ! defined(HAVE_LOCALTIME_R) && defined(HAVE_LOCALTIME)
struct tm *
localtime_r(const time_t *clock, struct tm *local)
{
	struct tm *result = NULL;
	static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

	PTHREAD_MUTEX_LOCK(&mutex);
	if ((result = localtime(clock)) != NULL) {
		*local = *result;
		result = local;
	}
	PTHREAD_MUTEX_UNLOCK(&mutex);

	return result;
}
#endif


#if ! defined(HAVE_CTIME_R) && defined(HAVE_CTIME)
char *
ctime_r(const time_t *clock, char *buf)
{
	char *result = NULL;
	static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

	PTHREAD_MUTEX_LOCK(&mutex);
	if ((result = ctime(clock)) != NULL) {
		TextCopy(buf, 26, result);
		result = buf;
	}
	PTHREAD_MUTEX_UNLOCK(&mutex);

	return result;
}
#endif


#if ! defined(HAVE_ASCTIME_R) && defined(HAVE_ASCTIME)
char *
asctime_r(struct tm *clock, char *buf)
{
	char *result = NULL;
	static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

	PTHREAD_MUTEX_LOCK(&mutex);
	if ((result = asctime(clock)) != NULL) {
		TextCopy(buf, 26, result);
		result = buf;
	}
	PTHREAD_MUTEX_UNLOCK(&mutex);

	return result;
}
#endif

