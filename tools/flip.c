/*
 * flip.c
 *
 * Flip line termination characters.
 *
 * Copyright 1994, 2003 by Anthony Howe.  All rights reserved.
 */

#include <err.h>
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

char tmp_name[16];
char filemsg[] = "File \"%s\" ";
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
"flip/1.3 Copyright 1994, 2016 by Anthony Howe. All rights reserved.\n"
;

/*
 * Return true for error, false for success.
 */
int
flip(fn)
const char *fn;
{
	FILE *fp, *tp;
	struct stat sb;
	int is_err, ch, nch;

	errno = 0;

	if (fn == NULL) {
		fp = stdin;
		tp = stdout;
	} else {
		if (stat(fn, &sb) == -1) {
			warn(filemsg, fn);
			return 1;
		}

		if (! (sb.st_mode & S_IWUSR)) {
			warn(readonly, fn);
			return 1;
		}

		if ((fp = fopen(fn, "rb")) == (FILE *) 0) {
			warn(filemsg, fn);
			return 1;
		}

		if ((tp = fopen(tmp_name, "wb")) == (FILE *) 0) {
			warn(filemsg, tmp_name);
			(void) fclose(fp);
			return 1;
		}
	}

	for (is_err = 0, ch = fgetc(fp); ch != EOF; ch = nch) {
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

			warn(binfile, fn);
			is_err = 1;
		}
		break;
	}

	(void) fclose(fp);

	if (fclose(tp) == -1)
		err(1, filemsg, tmp_name);

	if (fn != NULL && !is_err && (unlink(fn) == -1 || rename(tmp_name, fn) == -1))
		err(1, rename_msg, tmp_name, fn);

	return is_err;
}

int
main(int argc, char **argv)
{
	int ch, ex;

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
			errx(2, usage_msg);
		}
	}

	/* Must set a <newline> style. */
	if (end_of_line <= 0)
		errx(2, usage_msg);

	/* Use a unique temporary file name per process to avoid
	 * temporary file name collisions across multiple instances.
	 */
	(void) snprintf(tmp_name, sizeof (tmp_name), "%u.tmp", (unsigned) getpid());

	argc -= optind;
	argv += optind;

	if (0 < argc) {
		for (ex = EXIT_SUCCESS; 0 < argc; --argc, ++argv) {
			if (flip(*argv))
				ex = EXIT_FAILURE;
		}
	} else {
		flip(NULL);
	}

	return ex;
}


