/*
 * Error.c
 *
 * Program error message & exit routines.
 *
 * Copyright 1994, 2004 by Anthony Howe.  All rights reserved.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include <com/snert/lib/io/Error.h>

static const char *programName = (char *) 0;

const char *
ErrorGetProgramName(void)
{
	return programName;
}

void
ErrorSetProgramName(const char *name)
{
	programName = name;
	errno = 0;
}

void
ErrorPrintV(const char *file, unsigned long line, const char *fmt, va_list args)
{
	if (fmt == (const char *) 0)
		fmt = "Error ";

	if (programName != (char *) 0)
		(void) fprintf(stderr, "%s: ", programName);

	(void) vfprintf(stderr, fmt, args);

	if (errno != 0) {
		(void) fprintf(stderr, ": %s", strerror(errno));
		errno = 0;
	}

	if (file != (const char *) 0 && *file != '\0')
		(void) fprintf(stderr, " [%s:%lu]", file, line);
}

void
ErrorPrintLineV(const char *file, unsigned long line, const char *fmt, va_list args)
{
	ErrorPrintV(file, line, fmt, args);
	(void) fprintf(stderr, "\r\n");
}

void
ErrorPrint(const char *file, unsigned long line, const char *fmt, ...)
{
	va_list args;

#ifdef USE_VARARGS_H
	va_start(args);
#else
	va_start(args, fmt);
#endif /* USE_VARARGS_H */

	ErrorPrintV(file, line, fmt, args);

	/* We require va_end() since some implementations define
	 * va_start() and va_end() as macros that define a block.
	 */
	va_end(args);
}

void
ErrorPrintLine(const char *file, unsigned long line, const char *fmt, ...)
{
	va_list args;

#ifdef USE_VARARGS_H
	va_start(args);
#else
	va_start(args, fmt);
#endif /* USE_VARARGS_H */

	ErrorPrintLineV(file, line, fmt, args);

	/* We require va_end() since some implementations define
	 * va_start() and va_end() as macros that define a block.
	 */
	va_end(args);
}

void
FatalPrintV(const char *file, unsigned long line, const char *fmt, va_list args)
{
	if (fmt == (const char *) 0)
		fmt = "Terminated for unknown reason.";

	ErrorPrintV(file, line, fmt, args);
	exit(1);
}

void
FatalPrintLineV(const char *file, unsigned long line, const char *fmt, va_list args)
{
	if (fmt == (const char *) 0)
		fmt = "Terminated for unknown reason.";

	ErrorPrintLineV(file, line, fmt, args);
	exit(1);
}

void
FatalPrint(const char *file, unsigned long line, const char *fmt, ...)
{
	va_list args;

#ifdef USE_VARARGS_H
	va_start(args);
#else
	va_start(args, fmt);
#endif /* USE_VARARGS_H */

	/* May not return. */
	FatalPrintV(file, line, fmt, args);

	/* We require va_end() since some implementations define
	 * va_start() and va_end() as macros that define a block.
	 */
	va_end(args);
}

void
FatalPrintLine(const char *file, unsigned long line, const char *fmt, ...)
{
	va_list args;

#ifdef USE_VARARGS_H
	va_start(args);
#else
	va_start(args, fmt);
#endif /* USE_VARARGS_H */

	/* May not return. */
	FatalPrintLineV(file, line, fmt, args);

	/* We require va_end() since some implementations define
	 * va_start() and va_end() as macros that define a block.
	 */
	va_end(args);
}

void
UsagePrintLine(const char *fmt)
{
	const char *name;

	if (fmt == (const char *) 0)
		fmt = "%s: Usage error";

	name = ErrorGetProgramName();
	if (name == (const char *) 0)
		name = "(program name)";

	ErrorSetProgramName((const char *) 0);
	ErrorPrintLine((const char *) 0, 0, fmt, name);
	ErrorSetProgramName(name);

	exit(2);
}
