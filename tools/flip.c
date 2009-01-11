/*
 * flip.c
 *
 * Flip line termination characters.
 *
 * Copyright 1994, 2003 by Anthony Howe.  All rights reserved.
 */

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(__BORLANDC__)
#include <io.h>
#include <com/snert/lib/util/getopt.h>
#else
#include <unistd.h>
#endif

#define CR_BIT		0x01
#define LF_BIT		0x02

int binary;
int end_of_line;

char filemsg[] = "File \"%s\" ";
char tmp_name[] = "tmp_flip.tmp";
char *eol[] = { (char *) 0, "\r", "\n", "\r\n" };

char binfile[] = "Binary file \"%s\" skipped. ";
char readonly[] = "Read-only file \"%s\" skipped. ";
char rename_msg[] = "Failed to rename \"%s\" to \"%s\". ";

char usage_msg[] =
"usage: flip [-bcl] [-d|-m|-u] [file...]\n"
"\n"
"-b\tProcess binary files.\n"
"-c\tChange <newline> to CR.\n"
"-l\tChange <newline> to LF.\n"
"-cl\tChange <newline> to CRLF.\n"
"-d\tDOS & Windows style <newline>.\n"
"-m\tMac style <newline>.\n"
"-u\tUnix style <newline>.\n"
"\n"
"flip/1.2 Copyright 1994, 2003 by Anthony Howe. All rights reserved.\n"
;

int flip(const char *);

/***********************************************************************
 *** This section is essentially a copy of com/snert/src/lib/Error.c
 *** I include this code here, because sometimes "flip" is required to
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

/*
 * Return true for error, false for success.
 */
int
flip(fn)
const char *fn;
{
	FILE *fp, *tp;
	struct stat sb;
	int err, ch, nch;

	errno = 0;

	if (fn == NULL) {
		fp = stdin;
		tp = stdout;
	} else {
		if (stat(fn, &sb) == -1) {
			ErrorPrintLine(0, 0, filemsg, fn);
			return 1;
		}

		if (! (sb.st_mode & S_IWUSR)) {
			ErrorPrintLine(0, 0, readonly, fn);
			return 1;
		}

		if ((fp = fopen(fn, "rb")) == (FILE *) 0) {
			ErrorPrintLine(0, 0, filemsg, fn);
			return 1;
		}

		if ((tp = fopen(tmp_name, "wb")) == (FILE *) 0) {
			ErrorPrintLine(0, 0, filemsg, tmp_name);
			(void) fclose(fp);
			return 1;
		}
	}

	for (err = 0, ch = fgetc(fp); ch != EOF; ch = nch) {
		/* Next character. */
		nch = fgetc(fp);

		switch (ch) {
		case '\r':
			if (nch == '\n') {
				/* Replace CRLF with prefered <newline>. */
				nch = fgetc(fp);
#ifdef TREAT_MAC_AS_BINARY
			} else if (!binary) {
				/* Preserver CR when not followed by LF. */
				(void) fputc(ch, tp);
				continue;
#endif
			}

			/* Fall through. */
		case '\n':
			/* Write prefered <newline> style. */
			(void) fputs(eol[end_of_line], tp);
			continue;
		default:
			if (isprint(ch) || isspace(ch) || binary) {
				(void) fputc(ch, tp);
				continue;
			}

			ErrorPrintLine(0, 0, binfile, fn);
			err = 1;
		}
		break;
	}

	(void) fclose(fp);

	if (fclose(tp) == -1)
		FatalPrintLine(0, 0, filemsg, tmp_name);

	if (fn != NULL && !err && (unlink(fn) == -1 || rename(tmp_name, fn) == -1))
		FatalPrintLine(0, 0, rename_msg, tmp_name, fn);

	return err;
}

int
main(int argc, char **argv)
{
	int ch, err = 0;

	ErrorSetProgramName("flip");

	binary = end_of_line = 0;

	while ((ch = getopt(argc, argv, "bcdlmuw")) != -1) {
		switch (ch) {
		case 'b':
			binary = 1;
			break;
		case 'c':
			end_of_line |= CR_BIT;
			break;
		case 'l':
			end_of_line |= LF_BIT;
			break;
		case 'd': case 'w':
			end_of_line = CR_BIT | LF_BIT;
			break;
		case 'm':
			end_of_line = CR_BIT;
			break;
		case 'u':
			end_of_line = LF_BIT;
			break;
		default:
			UsagePrintLine(usage_msg);
		}
	}

	/* Must set a <newline> style. */
	if (end_of_line <= 0)
		UsagePrintLine(usage_msg);

	argc -= optind;
	argv += optind;

	if (0 < argc) {
		for (err = 0; 0 < argc; --argc, ++argv) {
			if (flip(*argv))
				err = 1;
		}
	} else {
		flip(NULL);
	}

	return err;
}


