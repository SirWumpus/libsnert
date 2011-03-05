/*
 * popin.c
 *
 * RFC 1939
 *
 * Copyright 2004, 2006 by Anthony Howe.  All rights reserved.
 *
 *
 * Description
 * -----------
 *
 *	popin [-dlrv][-h host][-p port] user pass [msgnum ...] >output
 *
 * A command-line POP3 mail client. Intended for scripted use. Yes,
 * I know about `fetchmail', but I wanted something simpler that I
 * could easily script into a Nagios test script.
 *
 * See also
 * --------
 *
 *	smtpout.c
 *	mail-cycle.sh
 */

#ifndef DEFAULT_POP_HOST
#define DEFAULT_POP_HOST		"127.0.0.1"
#endif

#ifndef DEFAULT_POP_PORT
#define DEFAULT_POP_PORT		110
#endif

#ifndef DEFAULT_SOCKET_TIMEOUT
#define DEFAULT_SOCKET_TIMEOUT		120
#endif

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef __MINGW32__
# if defined(HAVE_SYSLOG_H)
#  include <syslog.h>
# endif
#endif

#if defined(__BORLANDC__) || defined(__CYGWIN__)
# include <io.h>
#endif

#include <com/snert/lib/io/Log.h>
#include <com/snert/lib/io/socket2.h>
#include <com/snert/lib/io/posix.h>
#include <com/snert/lib/io/Dns.h>
#include <com/snert/lib/util/md5.h>
#include <com/snert/lib/util/Text.h>

/***********************************************************************
 *** Constants
 ***********************************************************************/

#define INPUT_LINE_SIZE		1000
#define HOST_NAME_SIZE		128
#define MAIL_ADDRESS_SIZE	256

#define NON_BLOCKING_WRITE
#define NON_BLOCKING_READ

/***********************************************************************
 *** Global Variables
 ***********************************************************************/

#define _NAME			"popin"

#define CMD_STATUS		0x0001
#define CMD_LIST		0x0002
#define CMD_UIDL		0x0004
#define CMD_READ		0x0008
#define CMD_DELETE		0x0010

static int debug;
static int cmdFlags;
static char *popHost = DEFAULT_POP_HOST;
static long popPort = DEFAULT_POP_PORT;
static long socketTimeout = DEFAULT_SOCKET_TIMEOUT;

static char line[INPUT_LINE_SIZE+1];

static char usage[] =
"usage: " _NAME " [-dlrsuv][-h host][-p port] user pass [msgnum ...] >output\n"
"\n"
"-d\t\tdelete specified messages; default is leave on server\n"
"-l\t\tlist specified message sizes; default is all\n"
"-h host\t\tPOP host to contact, default localhost\n"
"-p port\t\tPOP port to connect to, default 110\n"
"-r\t\tread specified messages or all messages if none specified\n"
"-s\t\treturn total number of messages and size\n"
"-u\t\tlist specified message identifiers; default is all\n"
"-v\t\tverbose debug messages; once maillog, twice stderr\n"
"user\t\tuser account to access\n"
"pass\t\tpassword for user account\n"
"msgnum\t\tmessage number to read, delete, or get size of; without -d\n"
"\t\tor -r the default will report the size of selected messages;\n"
"\t\tif no message numbers are given, the default returns the\n"
"\t\ttotal number of messages for the account\n"
"output\t\tread messages are written to standard output. \n"
"\n"
_NAME "/1.2 Copyright 2004, 2007 by Anthony Howe.  All rights reserved.\n"
;

/***********************************************************************
 *** Supporting Cast
 ***********************************************************************/

#if ! defined(__MINGW32__)
void
syslog(int level, const char *fmt, ...)
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

int
printline(Socket2 *s, char *line)
{
	int rc = 0;

	if (debug)
		syslog(LOG_DEBUG, "> %s", line);

#if defined(NON_BLOCKING_WRITE)
# if ! defined(NON_BLOCKING_READ)
	/* Do not block on sending to the SMTP server just yet. */
	(void) socketSetNonBlocking(s, 1);
# endif

	if (socketWrite(s, (unsigned char *) line, (long) strlen(line)) == SOCKET_ERROR) {
		syslog(LOG_ERR, "SocketPrint() error: %s (%d)", strerror(errno), errno);
		goto error0;
	}

	/* Now wait for the output to be sent to the SMTP server. */
	if (!socketCanSend(s, socketTimeout)) {
		syslog(LOG_ERR, "timeout before output sent to SMTP server");
		goto error0;
	}
#else
	if (socketWrite(s, line, (long) strlen(line)) == SOCKET_ERROR) {
		syslog(LOG_ERR, "SocketPrint() error: %s (%d)", strerror(errno), errno);
		goto error0;
	}
#endif
	rc = 1;
error0:
#if ! defined(NON_BLOCKING_READ) && defined(NON_BLOCKING_WRITE)
	(void) socketSetNonBlocking(s, 0);
#endif
	return rc;
}

int
printlines(Socket2 *s, char **lines)
{
        for ( ; *lines != (char *) 0; lines++) {
        	if (!printline(s, *lines))
        		return 0;
        }

        return 1;
}

int
getSocketLine(Socket2 *s, char *line, long size)
{
	long length = -1;

#if defined(NON_BLOCKING_READ) && ! defined(NON_BLOCKING_WRITE)
	/* Do not block on reading from the SMTP server just yet. */
	(void) socketSetNonBlocking(s, 1);
#endif
	/* Erase the first 5 bytes of the line buffer, which corresponds
	 * to the 3 ASCII characters of the status code followed by either
	 * space.
	 */
	memset(line, 0, 5);

	switch (length = socketReadLine(s, line, size)) {
	case SOCKET_ERROR:
		syslog(LOG_ERR, "read error: %s (%d)", strerror(errno), errno);
		goto error0;
	case SOCKET_EOF:
		syslog(LOG_ERR, "unexpected EOF");
		goto error0;
	}

	if (debug)
		syslog(LOG_DEBUG, "< %s", line);
error0:

#if defined(NON_BLOCKING_READ) && ! defined(NON_BLOCKING_WRITE)
	(void) socketSetNonBlocking(s, 0);
#endif
	return length;
}

int
getPopResponse(Socket2 *s, char *line, long size)
{
	if (getSocketLine(s, line, size) < 0)
		return -1;

	if (strncmp(line, "-ERR", 4) == 0)
		return -1;

	if (strncmp(line, "+OK", 3) != 0)
		return -1;

	return 0;
}

int
popStatus(Socket2 *pop, long *count, long *bytes)
{
	printline(pop, "STAT\r\n");
	if (getPopResponse(pop, line, sizeof (line))) {
		syslog(LOG_ERR, "STAT failed: %s", line);
		return -1;
	}

	if (sscanf(line, "+OK %ld %ld", count, bytes) != 2) {
		syslog(LOG_ERR, "STAT syntax error");
		return -1;
	}

	return 0;
}

int
popList(Socket2 *pop, long message)
{
	long number, octets;

	snprintf(line, sizeof (line), "LIST %ld\r\n", message);
	printline(pop, line);
	if (getPopResponse(pop, line, sizeof (line))) {
		syslog(LOG_ERR, "LIST %ld failed: %s", message, line);
		return -1;
	}

	if (sscanf(line, "+OK %ld %ld", &number, &octets) != 2) {
		syslog(LOG_ERR, "LIST syntax error");
		return -1;
	}

	printf("%ld %ld\r\n", number, octets);

	return 0;
}

int
popUidl(Socket2 *pop, long message)
{
	long number;
	char message_id[256];

	snprintf(line, sizeof (line), "UIDL %ld\r\n", message);
	printline(pop, line);
	if (getPopResponse(pop, line, sizeof (line))) {
		syslog(LOG_ERR, "UIDL %ld failed: %s", message, line);
		return -1;
	}

	if (sscanf(line, "+OK %ld %255s", &number, message_id) != 2) {
		syslog(LOG_ERR, "UIDL syntax error");
		return -1;
	}

	printf("%ld %s\r\n", number, message_id);

	return 0;
}

int
popRead(Socket2 *pop, long message)
{
	long length;

	snprintf(line, sizeof (line), "RETR %ld\r\n", message);
	printline(pop, line);
	if (getPopResponse(pop, line, sizeof (line))) {
		syslog(LOG_ERR, "RETR %ld failed: %s", message, line);
		return -1;
	}

	while (0 <= (length = getSocketLine(pop, line, sizeof (line)))) {
		if ((long) sizeof (line) <= length)
			syslog(LOG_WARN, "message input line truncated");

		printf("%s\r\n", line);

		if (line[0] == '.' && line[1] == '\0') {
			break;
		}
	}

	return 0;
}

int
popDelete(Socket2 *pop, long message)
{
	snprintf(line, sizeof (line), "DELE %ld\r\n", message);
	printline(pop, line);
	if (getPopResponse(pop, line, sizeof (line))) {
		syslog(LOG_ERR, "DELE %ld failed: %s", message, line);
		return -1;
	}

	return 0;
}

int
forEach(Socket2 *pop, long start, long stop, int (*function)(Socket2 *pop, long message))
{
	if (start < 1)
		start = 1;
	if (stop < 1)
		stop = 1;

	for ( ; start <= stop; start++) {
		if ((*function)(pop, start))
			return -1;
	}

	return 0;
}

int
forEachArg(Socket2 *pop, int argc, char **argv, int start, int (*function)(Socket2 *pop, long message))
{
	long number;

	for ( ; start < argc; start++) {
		number = strtol(argv[start], NULL, 10);
		if ((*function)(pop, number))
			return -1;
	}

	return 0;
}

int
main(int argc, char **argv)
{
	Socket2 *pop;
	char *arg, *stop;
	long messages, octets;
	SocketAddress *address;
	int argi, offset, span, rc = EXIT_FAILURE;

#if defined(__BORLANDC__) || defined(__CYGWIN__)
	setmode(0, O_BINARY);
	setmode(1, O_BINARY);
#endif
	for (argi = 1; argi < argc; argi++) {
		if (argv[argi][0] != '-' || (argv[argi][1] == '-' && argv[argi][2] == '\0'))
			break;

		switch (argv[argi][1]) {
		case 'd':
			cmdFlags |= CMD_DELETE;
			break;
		case 'l':
			cmdFlags |= CMD_LIST;
			break;
		case 'r':
			cmdFlags |= CMD_READ;
			break;
		case 's':
			cmdFlags |= CMD_STATUS;
			break;
		case 'u':
			cmdFlags |= CMD_UIDL;
			break;
		case 'h':
			popHost = argv[argi][2] == '\0' ? argv[++argi] : &argv[argi][2];
			break;
		case 'p':
			arg = argv[argi][2] == '\0' ? argv[++argi] : &argv[argi][2];
			if ((popPort = strtol(arg, &stop, 10)) <= 0 || *stop != '\0') {
				fprintf(stderr, "invalid POP port number\n%s", usage);
				return EXIT_USAGE;
			}
			break;
		case 't':
			arg = argv[argi][2] == '\0' ? argv[++argi] : &argv[argi][2];
			if ((socketTimeout = strtol(arg, &stop, 10)) <= 0 || *stop != '\0') {
				fprintf(stderr, "invalid socket timeout value\n%s", usage);
				return EXIT_USAGE;
			}
			break;
		case 'v':
			LogSetProgramName(_NAME);
			socketSetDebug(1);
			debug++;
			break;
		default:
			fprintf(stderr, "invalid option -%c\n%s", argv[argi][1], usage);
			return EXIT_USAGE;
		}
	}

	if (argc < argi + 2) {
		fprintf(stderr, "missing a user account and/or password\n%s", usage);
		return EXIT_USAGE;
	}

 	if (popHost == NULL) {
		fprintf(stderr, "missing POP host name or address\n%s", usage);
		return EXIT_USAGE;
	}

	socketTimeout *= 1000;
	openlog(_NAME, LOG_PID, LOG_MAIL);

	if (1 < debug)
		LogOpen("(standard error)");

	if (socketInit()) {
		syslog(LOG_ERR, "socketInit() failed");
		goto error0;
	}

	syslog(LOG_INFO, "connecting to host=%s port=%ld", popHost, popPort);

	if ((address = socketAddressCreate(popHost, popPort)) == NULL) {
		syslog(LOG_ERR, "failed to find host %s:%ld", popHost, popPort);
		goto error0;
	}

	if ((pop = socketOpen(address, 1)) == NULL) {
		syslog(LOG_ERR, "failed to create socket to host %s:%ld", popHost, popPort);
		goto error1;
	}

	if (socketClient(pop, socketTimeout)) {
		syslog(LOG_ERR, "failed to connect to host %s:%ld", popHost, popPort);
		goto error2;
	}

	socketSetTimeout(pop, socketTimeout);

#if defined(NON_BLOCKING_READ) && defined(NON_BLOCKING_WRITE)
	/* Set non-blocking I/O once for both getPopResponse() and
	 * printline() and leave it that way.
	 */
	if (socketSetNonBlocking(pop, 1)) {
		syslog(LOG_ERR, "internal error: socketSetNonBlocking() failed: %s (%d)", strerror(errno), errno);
		goto error2;
	}
#endif

	if (getPopResponse(pop, line, sizeof (line))) {
		syslog(LOG_ERR, "host %s:%ld responded with an error: %s", popHost, popPort, line);
		goto error2;
	}

	/* Look for timestamp in welcome banner. */
	if ((offset = TextFind(line, "*<*@*>*", -1, 1)) == -1 ) {
		/* Use USER/PASS clear text login. */
		snprintf(line, sizeof (line), "USER %s\r\n", argv[argi]);
		printline(pop, line);
		if (getPopResponse(pop, line, sizeof (line))) {
			syslog(LOG_ERR, "USER %s failed: %s", argv[argi], line);
			goto error3;
		}
		argi++;

		snprintf(line, sizeof (line), "PASS %s\r\n", argv[argi]);
		printline(pop, line);
		if (getPopResponse(pop, line, sizeof (line))) {
			syslog(LOG_ERR, "PASS command failed: %s", line);
			goto error3;
		}
		argi++;
	} else {
		/* Use APOP login */
		md5_state_t md5;
		unsigned char digest[16];

		md5_init(&md5);
		for (span = offset + strcspn(line+offset, ">")+1 ; offset < span; offset++)
			md5_append(&md5, (md5_byte_t *) &line[offset], 1);
		md5_append(&md5, (md5_byte_t *) argv[argi+1], strlen(argv[argi+1]));
		md5_finish(&md5, (md5_byte_t *) digest);

		snprintf(line, sizeof (line), "APOP %s %n___32 ASCII hex digits of MD5___\r\n", argv[argi], &offset);
		md5_digest_to_string(digest, line+offset);
		line[offset+32] = '\r';
		printline(pop, line);
		if (getPopResponse(pop, line, sizeof (line))) {
			syslog(LOG_ERR, "APOP %s failed: %s", argv[argi], line);
			goto error3;
		}
		argi += 2;
	}

	syslog(LOG_INFO, "user %s logged in", argv[argi-2]);

	if (popStatus(pop, &messages, &octets))
		goto error3;

	if (cmdFlags & CMD_STATUS)
		printf("%ld %ld\r\n", messages, octets);

	if (cmdFlags & CMD_LIST) {
		if (argi < argc) {
			if (forEachArg(pop, argc, argv, argi, popList))
				goto error3;
		} else if (forEach(pop, 1, messages, popList)){
			goto error3;
		}
	}

	if (cmdFlags & CMD_UIDL) {
		if (argi < argc) {
			if (forEachArg(pop, argc, argv, argi, popUidl))
				goto error3;
		} else if (forEach(pop, 1, messages, popUidl)){
			goto error3;
		}
	}

	if (cmdFlags & CMD_READ) {
		if (argi < argc) {
			if (forEachArg(pop, argc, argv, argi, popRead))
				goto error3;
		} else if (forEach(pop, 1, messages, popRead)){
			goto error3;
		}
	}

	if (cmdFlags & CMD_DELETE) {
		if (argi < argc) {
			if (forEachArg(pop, argc, argv, argi, popDelete))
				goto error3;
		} else if (forEach(pop, 1, messages, popDelete)){
			goto error3;
		}
	}

	if (cmdFlags == 0) {
		printf("%ld %ld\r\n", messages, octets);
	}
error3:
	printline(pop, "QUIT\r\n");
	rc = getPopResponse(pop, line, sizeof (line)) != 0;
	syslog(LOG_INFO, "user %s logged out", argv[argi-2]);
error2:
	socketClose(pop);
error1:
	free(address);
error0:
	return rc;
}


