/*
 * rlimits.c
 *
 * Copyright 2006 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#ifndef __WIN32__

#include <com/snert/lib/version.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#if defined(HAVE_SYSLOG_H) && ! defined(__MINGW32__)
# include <syslog.h>
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
#ifdef HAVE_SYS_RESOURCE_H
# include <sys/resource.h>
#endif

#include <com/snert/lib/io/Log.h>

/* This doesn't work for some stupid reason in GCC, even though its valid ANSI C89 */
/* #if sizeof (rlim_t) == sizeof (long long) */

#ifndef RLIM_T_FORMAT
#define RLIM_T_FORMAT		"%lu"
#endif
#ifndef RLIM_T_CAST
#define RLIM_T_CAST		(unsigned long)
#endif

void
rlimits(void)
{
#ifdef HAVE_STRUCT_RLIMIT
	struct rlimit limit;
#endif
#ifdef RLIMIT_AS
	if (getrlimit(RLIMIT_AS, &limit))
		syslog(LOG_ERR, "get RLIMIT_AS failed: %s (%d)", strerror(errno), errno);
	else
		syslog(LOG_DEBUG, "RLIMIT_AS { " RLIM_T_FORMAT ", " RLIM_T_FORMAT " }", RLIM_T_CAST limit.rlim_cur, RLIM_T_CAST limit.rlim_max);
#endif
#ifdef RLIMIT_CORE
	if (getrlimit(RLIMIT_CORE, &limit))
		syslog(LOG_ERR, "get RLIMIT_CORE failed: %s (%d)", strerror(errno), errno);
	else
		syslog(LOG_DEBUG, "RLIMIT_CORE { " RLIM_T_FORMAT ", " RLIM_T_FORMAT " }", RLIM_T_CAST limit.rlim_cur, RLIM_T_CAST limit.rlim_max);
#endif
#ifdef RLIMIT_CPU
	if (getrlimit(RLIMIT_CPU, &limit))
		syslog(LOG_ERR, "get RLIMIT_CPU failed: %s (%d)", strerror(errno), errno);
	else
		syslog(LOG_DEBUG, "RLIMIT_CPU { " RLIM_T_FORMAT ", " RLIM_T_FORMAT " }", RLIM_T_CAST limit.rlim_cur, RLIM_T_CAST limit.rlim_max);
#endif
#ifdef RLIMIT_DATA
	if (getrlimit(RLIMIT_DATA, &limit))
		syslog(LOG_ERR, "get RLIMIT_DATA failed: %s (%d)", strerror(errno), errno);
	else
		syslog(LOG_DEBUG, "RLIMIT_DATA { " RLIM_T_FORMAT ", " RLIM_T_FORMAT " }", RLIM_T_CAST limit.rlim_cur, RLIM_T_CAST limit.rlim_max);
#endif
#ifdef RLIMIT_FSIZE
	if (getrlimit(RLIMIT_FSIZE, &limit))
		syslog(LOG_ERR, "get RLIMIT_FSIZE failed: %s (%d)", strerror(errno), errno);
	else
		syslog(LOG_DEBUG, "RLIMIT_FSIZE { " RLIM_T_FORMAT ", " RLIM_T_FORMAT " }", RLIM_T_CAST limit.rlim_cur, RLIM_T_CAST limit.rlim_max);
#endif
#ifdef RLIMIT_MEMLOCK
	if (getrlimit(RLIMIT_MEMLOCK, &limit))
		syslog(LOG_ERR, "get RLIMIT_MEMLOCK failed: %s (%d)", strerror(errno), errno);
	else
		syslog(LOG_DEBUG, "RLIMIT_MEMLOCK { " RLIM_T_FORMAT ", " RLIM_T_FORMAT " }", RLIM_T_CAST limit.rlim_cur, RLIM_T_CAST limit.rlim_max);
#endif
#ifdef RLIMIT_NOFILE
	if (getrlimit(RLIMIT_NOFILE, &limit))
		syslog(LOG_ERR, "get RLIMIT_NOFILE failed: %s (%d)", strerror(errno), errno);
	else
		syslog(LOG_DEBUG, "RLIMIT_NOFILE { " RLIM_T_FORMAT ", " RLIM_T_FORMAT " }", RLIM_T_CAST limit.rlim_cur, RLIM_T_CAST limit.rlim_max);
#endif
#ifdef RLIMIT_NPROC
	if (getrlimit(RLIMIT_NPROC, &limit))
		syslog(LOG_ERR, "get RLIMIT_NPROC failed: %s (%d)", strerror(errno), errno);
	else
		syslog(LOG_DEBUG, "RLIMIT_NPROC { " RLIM_T_FORMAT ", " RLIM_T_FORMAT " }", RLIM_T_CAST limit.rlim_cur, RLIM_T_CAST limit.rlim_max);
#endif
#ifdef RLIMIT_RSS
	if (getrlimit(RLIMIT_RSS, &limit))
		syslog(LOG_ERR, "get RLIMIT_RSS failed: %s (%d)", strerror(errno), errno);
	else
		syslog(LOG_DEBUG, "RLIMIT_RSS { " RLIM_T_FORMAT ", " RLIM_T_FORMAT " }", RLIM_T_CAST limit.rlim_cur, RLIM_T_CAST limit.rlim_max);
#endif
#ifdef RLIMIT_STACK
	if (getrlimit(RLIMIT_STACK, &limit))
		syslog(LOG_ERR, "get RLIMIT_STACK failed: %s (%d)", strerror(errno), errno);
	else
		syslog(LOG_DEBUG, "RLIMIT_STACK { " RLIM_T_FORMAT ", " RLIM_T_FORMAT " }", RLIM_T_CAST limit.rlim_cur, RLIM_T_CAST limit.rlim_max);
#endif
}

#endif /* __WIN32__ */
