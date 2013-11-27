/*
 * mimepart.c
 *
 * Copyright 2004, 2007 by Anthony Howe. All rights reserved.
 *
 *
 * Description
 * -----------
 *
 *	mimepart -p num [-v] message >output
 *	mimepart -f [-mv] message >output
 *
 * Build
 * -----
 *
 *	gcc -omimepart mimepart.c
 */


/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#endif
#ifdef HAVE_UNISTD_H
# undef _GNU_SOURCE
# define _GNU_SOURCE
# include <unistd.h>
#endif

#include <com/snert/lib/io/socket2.h>
#include <com/snert/lib/net/network.h>
#include <com/snert/lib/util/Text.h>
#include <com/snert/lib/sys/Time.h>

/***********************************************************************
 *** Constants
 ***********************************************************************/

#define _NAME			"mimepart"
#define _VERSION		_NAME "/1.2"

#define MAX_LINE_LENGTH		1024

#ifndef HAVE_STRNCASECMP
#define strncasecmp		TextInsensitiveCompareN
#endif

/***********************************************************************
 *** Global Variables
 ***********************************************************************/

static int debug;
static int listParts;
static int mboxFormat;
static int findForward;
static long partNumber;
static char *boundary;
static long boundaryLength;
static char line[MAX_LINE_LENGTH];
static char header[MAX_LINE_LENGTH * 2];

static char *usage =
"usage: " _NAME " -f [-m][-v][-d /path/maildir] [message] >output\n"
"       " _NAME " -l     [-v] [message] >output\n"
"       " _NAME " -p num [-v] [message] >output\n"
"\n"
"-d maildir\tpath of a maildir in which to save the forwarded message\n"
"-f\t\textract only a forwarded message attachment\n"
"-l\t\tlist summary of top level MIME parts\n"
"-m\t\toutput extracted messages in mbox format\n"
"-p num\t\textract the Nth top level MIME part block\n"
"-v\t\tverbose debug messages to standard error\n"
"\n"
"Read a MIME message from a file or standard input if message file name\n"
"is not given.\n"
"\n"
 _VERSION " Copyright 2004, 2005 by Anthony Howe. All rights reserved.\n"
;

/***********************************************************************
 ***
 ***********************************************************************/

#ifdef OFF
long
TextInputLine(FILE *fp, char *line, long size)
{
	long i;

	for (i = 0, --size; i < size; ++i) {
		line[i] = (char) fgetc(fp);

		if (feof(fp) || ferror(fp))
			return -1;

		if (line[i] == '\n') {
			line[i] = '\0';
			if (0 < i && line[i-1] == '\r')
				line[--i] = '\0';
			break;
		}
	}

	line[i] = '\0';

	if (debug)
		fprintf(stderr, "line=%s\n", line);

	return i;
}
#endif

#ifdef HAVE_RAND_R
#define RANDOM_NUMBER(max)	((int)((double)(max) * (double) rand_r(&rand_seed) / (RAND_MAX+1.0)))
#else
#define RANDOM_NUMBER(max)	((int)((double)(max) * (double) rand() / (RAND_MAX+1.0)))
#endif

#ifndef RAND_MSG_COUNT
#define RAND_MSG_COUNT		RANDOM_NUMBER(62.0*62.0)
#endif

#define TIME_CYCLE		60

static char *maildir;
static FILE *message_out;
static char *message_filename;
static const char base62[] = "0123456789ABCDEFGHIJKLMNOPQRSYUVWXYZabcdefghijklmnopqrsyuvwxyz";
static unsigned rand_seed;

static void
time_encode(time_t when, char buffer[6])
{
	struct tm local;

	(void) localtime_r(&when, &local);

	buffer[0] = base62[local.tm_year % TIME_CYCLE];
	buffer[1] = base62[local.tm_mon];
	buffer[2] = base62[local.tm_mday - 1];
	buffer[3] = base62[local.tm_hour];
	buffer[4] = base62[local.tm_min];
	buffer[5] = base62[local.tm_sec];
}

/*
 * The maildir file name is composed of
 *
 *	maildir subdir ymd HMS ppppp sssss cc
 */
static void
maildirFillName(unsigned *count, char buffer[20+DOMAIN_STRING_LENGTH])
{
	if (62 * 62 <= ++*count)
		*count = 1;

	time_encode(time(NULL), buffer);
	(void) snprintf(
		buffer+6, 20+DOMAIN_STRING_LENGTH-6, ".%05u%05u%c%c.",
		getpid(), (unsigned) RAND_MSG_COUNT, base62[*count / 62], base62[*count % 62]
	);

	gethostname(buffer+20, DOMAIN_STRING_LENGTH);
}

int
maildirOpen(const char *maildir_root, char **filename)
{
	int fd;
	unsigned count = 0;
	size_t filename_length, maildir_length;

	maildir_length = strlen(maildir_root) + sizeof("/tmp/")-1;
	filename_length = maildir_length + 20 + DOMAIN_STRING_LENGTH;
	if ((*filename = malloc(filename_length)) == NULL)
		return -1;

	(void) snprintf(*filename, maildir_length+1, "%s/tmp/", maildir_root);

	do {
		maildirFillName(&count, *filename + maildir_length);
		errno = 0;
	} while ((fd = open(*filename, O_WRONLY|O_CREAT|O_EXCL, 0640)) == -1 && errno == EEXIST);

	return errno == 0 ? fd : -1;
}

int
maildirClose(int fd, char *filename)
{
	char *tmp, *filename_new;

	if (fd < 0 || filename == NULL)
		goto error0;

	if ((filename_new = strdup(filename)) == NULL)
		goto error0;

	if ((tmp = strstr(filename_new, "/tmp/")) == NULL)
		goto error1;

	/* Replace tmp by new in filename_new. */
	memcpy(tmp+1, "new", sizeof ("new")-1);

#ifdef __unix__
	if (link(filename, filename_new))
		goto error1;
#endif
#ifdef __WIN32__
	if (!CreateHardLink(filename_new, filename, NULL))
		goto error1;
#endif

	if (close(fd))
		goto error1;

	(void) unlink(filename);
	free(filename);

	return 0;
error1:
	free(filename_new);
error0:
	return -1;
}

FILE *
maildirFopen(const char *maildir_root, char **filename)
{
	int fd;
	FILE *fp;

	if ((fd = maildirOpen(maildir_root, filename)) == -1)
		return NULL;

	if ((fp = fdopen(fd, "w")) == NULL) {
		(void) close(fd);
		unlink(*filename);
		free(*filename);
	}

	return fp;
}

int
maildirFclose(FILE *fp, char *filename)
{
	char *tmp, *filename_new;

	if (fp == NULL || filename == NULL)
		goto error0;

	if ((filename_new = strdup(filename)) == NULL)
		goto error0;

	if ((tmp = strstr(filename_new, "/tmp/")) == NULL)
		goto error1;

	/* Replace "tmp" by "new" in filename_new. */
	memcpy(tmp+1, "new", sizeof ("new")-1);

#ifdef __unix__
	if (link(filename, filename_new))
		goto error1;
#endif
#ifdef __WIN32__
	if (!CreateHardLink(filename_new, filename, NULL))
		goto error1;
#endif

	if (fclose(fp))
		goto error1;

	(void) unlink(filename);
	free(filename);

	return 0;
error1:
	free(filename_new);
error0:
	return -1;
}

void
ExitOnErrorOrEOF(FILE *fp)
{
	if (feof(fp))
		exit(0);

	fprintf(stderr, "error reading message: %s (%d)\n", strerror(errno), errno);
	exit(1);
}

long
HeaderInputLine(FILE *fp, char *buf, long size)
{
	int ch;
	long length, totalLength;

	totalLength = 0;
	while (0 < (length = TextInputLine(fp, buf + totalLength, size - totalLength))) {
		if (size <= totalLength + length)
			return -1;

		totalLength += length;

		if ((ch = fgetc(fp)) == EOF) {
			if (ferror(fp))
				return -1;
			return totalLength;
		}

		if (ch != ' ' && ch != '\t') {
			ungetc(ch, fp);
			break;
		}
	}

	if (debug)
		fprintf(stderr, "length=%ld header=%s\n", totalLength, buf);

	return length < 0 && totalLength == 0 ? -1 : totalLength;
}

void
processHeader()
{
	char *param, *delims;

	if (strncasecmp(header, "Content-Type:", sizeof ("Content-Type:")-1) != 0)
		return;

	if ((param = strstr(header, "boundary=")) == NULL) {
		fprintf(stderr, "Content-Type header missing boundary paramater\n");
		exit(1);
	}

	param += sizeof ("boundary=")-1;
	if (*param == '"') {
		delims = "\"";
		param++;
	} else {
		delims = " \t";
	}


	boundaryLength = strcspn(param, delims);

	if ((boundary = malloc(boundaryLength+1)) == NULL) {
		fprintf(stderr, "memory allocation failed: %s, (%d)\n", strerror(errno), errno);
		exit(1);
	}

#ifdef HAVE_STRLCPY
	strlcpy(boundary, param, boundaryLength+1);
#else
	/* sprintf(boundary, boundaryLength+1, "%s", param); */
	strncpy(boundary, param, boundaryLength+1);
	boundary[boundaryLength] = '\0';
#endif
	if (debug)
		fprintf(stderr, "length=%ld boundary=%s\n", boundaryLength, boundary);
}

int
isMessageRfc822()
{
	char *p;

	if (strncasecmp(header, "Content-Type:", sizeof ("Content-Type:")-1) != 0)
		return 0;

	p = header +  sizeof ("Content-Type:")-1;
	p += strspn(p, " \t");

	if (strncasecmp(p, "message/rfc822", sizeof ("message/rfc822")-1) != 0)
		return 0;

	return 1;
}

void
atExitCleanUp(void)
{
	(void) maildirFclose(stdout, message_filename);
}

int
main(int argc, char **argv)
{
	time_t now;
	long length, part;
	int argi, isMessage;

	(void) time(&now);
	srand(rand_seed = now);
	partNumber = ((unsigned long) ~0) >> 1;

	for (argi = 1; argi < argc; argi++) {
		if (argv[argi][0] != '-' || (argv[argi][1] == '-' && argv[argi][2] == '\0'))
			break;

		switch (argv[argi][1]) {
		case 'd':
			maildir = argv[argi][2] == '\0' ? argv[++argi] : &argv[argi][2];
			break;
		case 'f':
			/* Look for a top level message/rfc822 MIME part. */
			findForward = 1;
			break;
		case 'l':
			listParts = 1;
			break;
		case 'm':
			mboxFormat = 1;
			break;
		case 'p':
			/* Look for the Nth top level MIME part. */
			partNumber = strtol(argv[argi][2] == '\0' ? argv[++argi] : &argv[argi][2], NULL, 10);
			break;
		case 'v':
			debug = 1;
			break;
		default:
			fprintf(stderr, "invalid option -%c\n%s", argv[argi][1], usage);
			return 2;
		}
	}

	if (argi + 1 == argc && freopen(argv[argi], "r", stdin) == NULL) {
		fprintf(stderr, "open \"%s\" error: %s (%d)\n", argv[argi], strerror(errno), errno);
		return 1;
	}

	/* Read header section looking for the Content-Type. */
	while (0 < HeaderInputLine(stdin, header, sizeof (header))) {
		processHeader();
	}

	if (ferror(stdin)) {
		fprintf(stderr, "error reading message headers: %s (%d)\n", strerror(errno), errno);
		return 1;
	}

	if (feof(stdin)) {
		fprintf(stderr, "unexpected EOF during message headers\n");
		return 1;
	}

	/* Fake the end of MIME part headers. */
	ungetc('\n', stdin);

	if (atexit(atExitCleanUp)) {
		fprintf(stderr, "atexit error: %s (%d)\n", strerror(errno), errno);
		return 1;
	}

	message_out = stdout;

	/* For each top level MIME part... */
	for (part = -1; part < partNumber; ) {
		/* Start of MIME part headers. */
		part++;
		isMessage = 0;

		while (0 < HeaderInputLine(stdin, header, sizeof (header))) {
			if (listParts)
				fprintf(message_out, "%.3ld: %s\n", part, header);
			if (isMessageRfc822()) {
				isMessage = findForward;

				if (isMessage && maildir != NULL) {
					message_out = maildirFopen(maildir, &message_filename);
					if (message_out == NULL) {
						fprintf(stderr, "maildir open error: %s (%d)\n", strerror(errno), errno);
						return 1;
					}
				}
			}
		}

		if (mboxFormat && isMessage) {
			/* Read first line of forwarded message, assumes is Return-Path. */
			if ((length = HeaderInputLine(stdin, header, sizeof (header))) < 0)
				ExitOnErrorOrEOF(stdin);

			if (sscanf(header, "%*[^:]: <%255[^>]>", line) == 1)
				fprintf(message_out, "From %s %s", line, ctime(&now));
			else
				fprintf(message_out, "From MAILER-DAEMON %s", ctime(&now));

			fprintf(message_out, "%s\n", header);
		}

		if (part != partNumber && !isMessage) {
			/* Skip unwanted parts until next boundary or EOF. */
			for (;;) {
				if ((length = TextInputLine(stdin, line, sizeof (line))) < 0)
					ExitOnErrorOrEOF(stdin);

				/* Have we found our top-level boundary? */
				if (line[0] == '-' && line[1] == '-' && strncmp(line+2, boundary, boundaryLength) == 0)
					break;
			}

			continue;
		}

		/* Write this part until next boundary or EOF. */
		for (;;) {
			if ((length = TextInputLine(stdin, line, sizeof (line))) < 0)
				ExitOnErrorOrEOF(stdin);

			/* Have we found our top-level boundary? */
			if (line[0] == '-' && line[1] == '-' && strncmp(line+2, boundary, boundaryLength) == 0)
				break;

			if (mboxFormat && strncmp(line, "From ", sizeof ("From ")-1) == 0)
				fprintf(message_out, ">");

			fprintf(message_out, "%s\n", line);
		}
	}

	return 0;
}

