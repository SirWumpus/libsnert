/*
 * Log.c
 *
 * Copyright 2002, 2010 by Anthony Howe.  All rights reserved.
 */

#define MAX_LOG_LINE_SIZE	512

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#ifndef __MINGW32__
# if defined(HAVE_SYSLOG_H)
#  include <syslog.h>
# endif
#endif

#ifdef TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# ifdef HAVE_SYS_TIME_H
# include <sys/time.h>
# else
# include <time.h>
# endif
#endif

#if defined(HAVE_UNISTD_H)
# include <unistd.h>
#else
extern long getpid(void);
#endif

#if HAVE_IO_H
# include <io.h>
#endif

#include <com/snert/lib/io/Log.h>
#include <com/snert/lib/io/posix.h>
#include <com/snert/lib/sys/Time.h>

#ifdef DEBUG_MALLOC
# include <com/snert/lib/util/DebugMalloc.h>
#endif

FILE *logFile;
static unsigned logMask = LOG_UPTO(LOG_DEBUG);
static char *logLevels[] = { "PANIC", "ALERT", "FATAL", "ERROR", "WARN", "NOTICE", "INFO", "DEBUG" };
static const char *programName = "";

#if ! defined(__MINGW32__)
void
alt_syslog(int level, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	if (logFile == NULL)
		vsyslog(level, fmt, args);
	else
		LogV(level, fmt, args);
	va_end(args);
}
#endif

const char *
LogGetProgramName(void)
{
	return programName;
}

void
LogSetProgramName(const char *name)
{
	if (*programName == '\0') {
		programName = name;
		errno = 0;
	}
}

void
LogSetMask(unsigned mask)
{
	logMask = mask;
}

#ifdef REPLACED_BY_LogSetMask
void
LogSetLevel(int level)
{
	logMask = LOG_UPTO(level);
}
#endif

int
LogPrintV(int level, const char *msg, va_list args)
{
	time_t now;
	int length;
	char stamp[20]; /* yyyy-mm-dd HH:MM:SS */
	struct tm local;
	char buffer[MAX_LOG_LINE_SIZE], *buf;

	if (logFile == NULL || msg == NULL || !(logMask & LOG_MASK(level)))
		return -1;

	now = time(NULL);
	(void) localtime_r(&now, &local);
	(void) strftime(stamp, sizeof (stamp), "%Y-%m-%d %H:%M:%S", &local);

	length = snprintf(
		buffer, sizeof (buffer), "%s %s:%lu %s ",
		stamp, programName, (unsigned long) getpid(), logLevels[level]
	);

	length += vsnprintf(buffer+length, sizeof (buffer)-length, msg, args);

	/* Replace ASCII control character, particularly CRLF. */
	for (buf = buffer; *buf != '\0'; buf++) {
		switch (*buf) {
		case '\r': case '\n':
			*buf = ' ';
		case '\t':
			continue;
		}

		if (iscntrl(*buf))
			*buf = '?';
	}

#if defined(HAVE_FLOCKFILE)
	flockfile(logFile);
#endif

	(void) fputs(buffer, logFile);
	(void) fputs("\r\n", logFile);
	(void) fflush(logFile);

#if defined(HAVE_FLOCKFILE)
	funlockfile(logFile);
#endif

	return 0;
}

/*
 * Return true if successfully logged, otherwise false if skipped.
 */
int
LogV(int level, const char *msg, va_list args)
{
	return LogPrintV(level, msg, args);
}

int
Log(int level, const char *msg, ...)
{
	int rc;
	va_list args;

	va_start(args, msg);
	rc = LogPrintV(level, msg, args);
	va_end(args);

	return rc;
}

int
LogDebug(const char *msg, ...)
{
	int rc;
	va_list args;

	va_start(args, msg);
	rc = LogPrintV(LOG_DEBUG, msg, args);
	va_end(args);

	return rc;
}

int
LogInfo(const char *msg, ...)
{
	int rc;
	va_list args;

	va_start(args, msg);
	rc = LogPrintV(LOG_INFO, msg, args);
	va_end(args);

	return rc;
}

int
LogWarn(const char *msg, ...)
{
	int rc;
	va_list args;

	va_start(args, msg);
	rc = LogPrintV(LOG_WARN, msg, args);
	va_end(args);

	return rc;
}

int
LogErrorV(const char *msg, va_list args)
{
	return LogPrintV(LOG_ERROR, msg, args);
}

int
LogError(const char *msg, ...)
{
	int rc;
	va_list args;

	va_start(args, msg);
	rc = LogErrorV(msg, args);
	va_end(args);

	return rc;
}

int
LogStderrV(int level, const char *msg, va_list args)
{
	if (logFile == NULL || msg == NULL || !(logMask & LOG_MASK(level)))
		return -1;

	(void) LogErrorV(msg, args);

#if defined(HAVE_FLOCKFILE)
	flockfile(stderr);
#endif

	(void) fprintf(stderr, "%s: ", programName);
	(void) vfprintf(stderr, msg, args);
	if (errno != 0)
		(void) fprintf(stderr, ": (%d) %s", errno, strerror(errno));
	(void) fputs("\r\n", stderr);
	(void) fflush(stderr);

#if defined(HAVE_FLOCKFILE)
	funlockfile(stderr);
#endif

	return 0;
}

int
LogStderr(int level, const char *msg, ...)
{
	int rc;
	va_list args;

	va_start(args, msg);
	rc = LogStderrV(level, msg, args);
	va_end(args);

	return rc;
}

void
LogFatal(const char *msg, ...)
{
	va_list args;

	va_start(args, msg);
	(void) LogStderrV(LOG_FATAL, msg, args);
	va_end(args);

	exit(EXIT_FAILURE);
}

int
LogOpen(const char *fname)
{
	LogClose();

	if (fname == NULL || strcmp("(standard error)", fname) == 0)
		logFile = stderr;

	else if (strcmp("(standard output)", fname) == 0)
		logFile = stdout;

	else if ((logFile = fopen(fname, "ab")) == NULL) {
		LogError("failed to open log file \"%s\"", fname);
		return -1;
	}
#if defined(__BORLANDC__) || defined(__CYGWIN__) || defined(__MINGW32__)
	setmode(fileno(logFile), O_BINARY);
#endif
	return 0;
}

void
LogClose(void)
{
	if (logFile != NULL) {
		if (logFile == stderr || logFile == stdout)
			(void) fflush(logFile);
		else
			(void) fclose(logFile);
	}
}

/*
 * openlog() replacement for systems without <syslog.h>, like Windows
 */
void
LogOpenLog(const char *ident, int option, int facility)
{
	size_t length;
	char *filename;

	length = strlen(ident) + 5;
	if ((filename = malloc(length)) != NULL) {
		LogSetProgramName(ident);
		(void) snprintf(filename, length, "%s.log", ident);
		(void) LogOpen(filename);
		free(filename);
	}
}

