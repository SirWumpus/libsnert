/*
 * strings.c
 *
 * Copyright 2004 by Anthony Howe.  All rights reserved.
 */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#if defined(__BORLANDC__)
# include <io.h>
# include <com/snert/lib/util/getopt.h>
#else
# include <unistd.h>
#endif

long minStringLength = 4;
char stringBuffer[BUFSIZ];
char filemsg[] = "File \"%s\" ";

/***********************************************************************
 *** This section is essentially a copy of com/snert/src/lib/Error.c
 *** I include this code here, because sometimes "strings" is required to
 *** to built before we can actually compile the library containing
 *** Error.c
 ***********************************************************************/

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

/***********************************************************************
 ***
 ***********************************************************************/

int
strings(const char *filename)
{
	FILE *fp;
	unsigned index;
	int err, ch, overflow;

	errno = 0;

	if ((fp = fopen(filename, "rb")) == (FILE *) 0) {
		ErrorPrintLine(0, 0, filemsg, filename);
		return 1;
	}

	index = overflow = 0;
	for (err = 0; (ch = fgetc(fp)) != EOF; ) {
		if (isprint(ch)) {
			if (sizeof(stringBuffer)-1 <= index) {
				stringBuffer[index] = '\0';
				(void) printf(stringBuffer);
				overflow = 1;
				index = 0;
			}
			stringBuffer[index++] = (char) ch;
		} else if (overflow || minStringLength <= index) {
			stringBuffer[index] = '\0';
			(void) printf("%s\n", stringBuffer);
			overflow = 0;
			index = 0;
		} else {
			index = 0;
		}
	}

	(void) fclose(fp);

	return err;
}

int
main(int argc, char **argv)
{
	int ch, err;

	ErrorSetProgramName("strings");

	while ((ch = getopt(argc, argv, "n:")) != -1) {
		switch (ch) {
		case 'n':
			minStringLength = strtol(optarg, (char **) 0, 10);
			break;
		default:
			UsagePrintLine("usage: strings [-n min.length] files...\n");
		}
	}

	argc -= optind;
	argv += optind;

	for (err = 0; 0 < argc; --argc, ++argv) {
		if (strings(*argv))
			err = 1;
	}

	return err;
}
