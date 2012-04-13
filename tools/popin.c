/*
 * popin.c
 *
 * RFC 1939
 *
 * Copyright 2004, 2011 by Anthony Howe.  All rights reserved.
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

#ifndef POP_HOST
#define POP_HOST		"127.0.0.1"
#endif

#ifndef POP_PORT
#define POP_PORT		110
#endif

#ifndef SOCKET_TIMEOUT
#define SOCKET_TIMEOUT		120
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
#include <com/snert/lib/io/socket3.h>
#include <com/snert/lib/io/posix.h>
#include <com/snert/lib/sys/process.h>
#include <com/snert/lib/util/Buf.h>
#include <com/snert/lib/util/md5.h>
#include <com/snert/lib/util/Text.h>
#include <com/snert/lib/util/getopt.h>

/***********************************************************************
 *** Constants
 ***********************************************************************/

#define INPUT_LINE_SIZE		1000
#define HOST_NAME_SIZE		128
#define MAIL_ADDRESS_SIZE	256

#define NON_BLOCKING_WRITE
#define NON_BLOCKING_READ

#ifndef CERT_DIR
#define CERT_DIR		NULL
#endif

#ifndef CA_CHAIN
#define CA_CHAIN		NULL
#endif

/***********************************************************************
 *** Global Variables
 ***********************************************************************/

#define _NAME			"popin"

#define CMD_STATUS		0x0001
#define CMD_LIST		0x0002
#define CMD_UIDL		0x0004
#define CMD_READ		0x0008
#define CMD_DELETE		0x0010
#define CMD_MBOX		0x0020

static int debug;
static int cmdFlags;
static char *popHost = POP_HOST;
static long popPort = POP_PORT;
static long socketTimeout = SOCKET_TIMEOUT;

#ifdef HAVE_OPENSSL_SSL_H
static int require_tls;
static int check_certificate;
static char *ca_chain = CA_CHAIN;
static char *cert_dir = CERT_DIR;
#endif

static char line[INPUT_LINE_SIZE+1];
static unsigned char data[INPUT_LINE_SIZE * 10];
static Buf buffer = { NULL, sizeof (data), 0, 0, data };

#ifdef HAVE_OPENSSL_SSL_H
static char options[] = "dlmrsuvxXc:C:h:p:t:";
#else
static char options[] = "dlmrsuvsh:p:t:";
#endif

static char usage[] =
#ifdef HAVE_OPENSSL_SSL_H
"usage: " _NAME " [-dlmrsuvxX][-c ca_pem][-C ca_dir][-h host][-p port][-t sec]\n"
"             user pass [msgnum ...] >output\n"
#else
"usage: " _NAME " [-dlmrsuv][-h host][-p port][-t sec]\n"
"             user pass [msgnum ...] >output\n"
#endif
"\n"
#ifdef HAVE_OPENSSL_SSL_H
"-c ca_pem\tCertificate Authority root certificate chain file\n"
"-C dir\t\tCertificate Authority root certificate directory\n"
#endif
"-d\t\tdelete specified messages; default is leave on server\n"
"-l\t\tlist specified message sizes; default is all\n"
"-h host\t\tPOP host to contact; default " POP_HOST "\n"
"-m\t\toutput in pseudo mbox format\n"
"-p port\t\tPOP port to connect to; default " QUOTE(POP_PORT) "\n"
"-r\t\tread specified messages or all messages if none specified\n"
"-s\t\treturn total number of messages and size\n"
"-t sec\t\tsocket timeout in seconds; default " QUOTE(SOCKET_TIMEOUT) "\n"
"-u\t\tlist specified message identifiers; default is all\n"
"-v\t\tverbose debug messages; once maillog, twice stderr\n"
#ifdef HAVE_OPENSSL_SSL_H
"-x\t\trequire TLS connection\n"
"-X\t\tverify the server certificate\n"
#endif
"user\t\tuser account to access\n"
"pass\t\tpassword for user account\n"
"msgnum\t\tmessage number to read, delete, or get size of; without -d\n"
"\t\tor -r the default will report the size of selected messages;\n"
"\t\tif no message numbers are given, the default returns the\n"
"\t\ttotal number of messages for the account\n"
"output\t\tread messages are written to standard output. \n"
"\n"
_NAME "/1.3 Copyright 2004, 2011 by Anthony Howe.  All rights reserved.\n"
;

/***********************************************************************
 *** Supporting Cast
 ***********************************************************************/

#if ! defined(__MINGW32__)
#undef syslog
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
printline(SOCKET s, char *line)
{
	int rc = 0;
	long length = (long) strlen(line);

	if (debug)
		syslog(LOG_DEBUG, "> %ld:%s", length, line);

#if defined(NON_BLOCKING_WRITE)
# if ! defined(NON_BLOCKING_READ)
	/* Do not block on sending to the SMTP server just yet. */
	(void) socket3_set_nonblocking(s, 1);
# endif

	if (socket3_write(s, (unsigned char *) line, length, NULL) == SOCKET_ERROR) {
		syslog(LOG_ERR, "SocketPrint() error: %s (%d)", strerror(errno), errno);
		goto error0;
	}

	/* Now wait for the output to be sent to the SMTP server. */
	if (!socket3_can_send(s, socketTimeout)) {
		syslog(LOG_ERR, "timeout before output sent to SMTP server");
		goto error0;
	}
#else
	if (socket3_write(s, line, (long) strlen(line), NULL) == SOCKET_ERROR) {
		syslog(LOG_ERR, "SocketPrint() error: %s (%d)", strerror(errno), errno);
		goto error0;
	}
#endif
	rc = 1;
error0:
#if ! defined(NON_BLOCKING_READ) && defined(NON_BLOCKING_WRITE)
	(void) socket3_set_nonblocking(s, 0);
#endif
	return rc;
}

int
printlines(SOCKET s, char **lines)
{
        for ( ; *lines != (char *) 0; lines++) {
        	if (!printline(s, *lines))
        		return 0;
        }

        return 1;
}

long
socket3_read_buf(SOCKET fd, Buf *readbuf, char *buffer, size_t size, long ms)
{
	if (readbuf == NULL || buffer == NULL || size < 1)
		return SOCKET_ERROR;

	size--;

	if (readbuf->length <= readbuf->offset) {
		if (!socket3_has_input(fd, ms))
			return SOCKET_ERROR;
		readbuf->length = socket3_read(fd, readbuf->bytes, readbuf->size-1, NULL);
		if (readbuf->length == SOCKET_ERROR)
			return SOCKET_ERROR;
		if (readbuf->length == 0)
			return SOCKET_EOF;

		/* Keep the buffer null terminated. */
		readbuf->bytes[readbuf->length] = 0;
		readbuf->offset = 0;
	}

	if (readbuf->length < readbuf->offset+size)
		size = readbuf->length-readbuf->offset;

	(void) memcpy(buffer, readbuf->bytes+readbuf->offset, size);
	readbuf->offset += size;

	return size;
}

long
socket3_read_line(SOCKET fd, Buf *readbuf, char *line, size_t size, long ms)
{
	long offset;
	unsigned char *nl;

	if (readbuf == NULL || line == NULL || size < 1)
		return SOCKET_ERROR;

	size--;

	for (offset = 0; offset < size; ) {
		if (readbuf->length <= readbuf->offset) {
			if (!socket3_has_input(fd, ms)) {
				if (0 < offset)
					break;
				return SOCKET_ERROR;
			}

			/* Peek at a chunk of data. */
			readbuf->length = socket3_peek(fd, readbuf->bytes, readbuf->size-1, NULL);
			if (readbuf->length < 0) {
				if (IS_EAGAIN(errno) || errno == EINTR) {
					errno = 0;
					nap(1, 0);
					continue;
				}
				return SOCKET_ERROR;
			}

			/* Find length of line. */
			readbuf->bytes[readbuf->length] = 0;
			if ((nl = (unsigned char *)strchr((char *)readbuf->bytes, '\n')) != NULL)
				readbuf->length = nl - readbuf->bytes + 1;

			/* Read only the line. */
			readbuf->length = socket3_read(fd, readbuf->bytes, readbuf->length, NULL);
			if (readbuf->length < 0) {
				if (IS_EAGAIN(errno) || errno == EINTR) {
					errno = 0;
					nap(1, 0);
					continue;
				}
				return SOCKET_ERROR;
			}
			if (readbuf->length == 0) {
				/* EOF with partial line read? */
				if (0 < offset)
					break;
				errno = ENOTCONN;
				return SOCKET_EOF;
			}

			/* Keep the buffer null terminated. */
			readbuf->bytes[readbuf->length] = 0;
			readbuf->offset = 0;
		}

		/* Copy from read buffer into the line buffer. */
		line[offset++] = (char) readbuf->bytes[readbuf->offset++];

		if (line[offset-1] == '\n')
			break;
	}

	line[offset] = '\0';

	return offset;
}

int
getSocketLine(SOCKET s, char *line, long size)
{
	long length = -1;

#if defined(NON_BLOCKING_READ) && ! defined(NON_BLOCKING_WRITE)
	/* Do not block on reading from the SMTP server just yet. */
	(void) socket3_set_nonblocking(s, 1);
#endif
	/* Erase the first 5 bytes of the line buffer, which corresponds
	 * to the 3 ASCII characters of the status code followed by either
	 * space.
	 */
	memset(line, 0, 5);

	switch (length = socket3_read_line(s, &buffer, line, size, socketTimeout)) {
	case SOCKET_ERROR:
		syslog(LOG_ERR, "read error: %s (%d)", strerror(errno), errno);
		goto error0;
	case SOCKET_EOF:
		syslog(LOG_ERR, "unexpected EOF");
		goto error0;
	}

	if (line[length-1] == '\n') {
		length--;
		if (line[length-1] == '\r')
			length--;
		line[length] = '\0';
	}

	if (debug)
		syslog(LOG_DEBUG, "< %ld:%s", length, line);
error0:

#if defined(NON_BLOCKING_READ) && ! defined(NON_BLOCKING_WRITE)
	(void) socket3_set_nonblocking(s, 0);
#endif
	return length;
}

int
getPopResponse(SOCKET s, char *line, long size)
{
	if (getSocketLine(s, line, size) < 0)
		return -1;

	if (strncmp(line, "-ERR", 4) == 0)
		return -1;

	if (strncmp(line, "+OK", 3) != 0)
		return -1;

	return 0;
}

#ifdef HAVE_OPENSSL_SSL_H
int
popStartTLS(SOCKET pop)
{
	printline(pop, "STLS\r\n");
	if (getPopResponse(pop, line, sizeof (line))) {
		syslog(LOG_ERR, "STLS failed: %s", line);
		return -1;
	}
	if (socket3_start_tls(pop, 0, socketTimeout))
		syslog(LOG_ERR, "socket3_start_tls() failed");

	return 0;
}
#endif

int
popStatus(SOCKET pop, long *count, long *bytes)
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

	if (0 < debug)
		syslog(LOG_DEBUG, "messages=%ld, bytes=%ld", *count, *bytes);

	return 0;
}

int
popList(SOCKET pop, long message)
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
popUidl(SOCKET pop, long message)
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
popRead(SOCKET pop, long message)
{
	long length;

	snprintf(line, sizeof (line), "RETR %ld\r\n", message);
	printline(pop, line);
	if (getPopResponse(pop, line, sizeof (line))) {
		syslog(LOG_ERR, "RETR %ld failed: %s", message, line);
		return -1;
	}

	if (cmdFlags & CMD_MBOX) {
		time_t now;
		(void) time(&now);
		printf("From - %s", ctime(&now));
	}

	while (0 <= (length = getSocketLine(pop, line, sizeof (line)))) {
		if ((long) sizeof (line) <= length)
			syslog(LOG_WARN, "message input line truncated");

		if ((cmdFlags & CMD_MBOX) != CMD_MBOX)
			printf("%s\r\n", line);

		if (line[0] == '.' && line[1] == '\0') {
			break;
		}

		if ((cmdFlags & CMD_MBOX) == CMD_MBOX) {
			if (strncmp(line, "From ", sizeof ("From ")-1) == 0)
				fputc('>', stdout);
			printf("%s\r\n", line);
		}
	}

	if (cmdFlags & CMD_MBOX)
		fputs("\r\n", stdout);

	return 0;
}

int
popDelete(SOCKET pop, long message)
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
forEach(SOCKET pop, long start, long stop, int (*function)(SOCKET pop, long message))
{
	if (start < 1)
		start = 1;

	for ( ; start <= stop; start++) {
		if ((*function)(pop, start))
			return -1;
	}

	return 0;
}

int
forEachArg(SOCKET pop, int argc, char **argv, int start, int (*function)(SOCKET pop, long message))
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
	SOCKET pop;
	long messages, octets;
	SocketAddress *address;
	int ch, offset, span, rc = EXIT_FAILURE;

#if defined(__BORLANDC__) || defined(__CYGWIN__)
	setmode(0, O_BINARY);
	setmode(1, O_BINARY);
#endif
	while ((ch = getopt(argc, argv, options)) != -1) {
		switch (ch) {
#ifdef HAVE_OPENSSL_SSL_H
		case 'c':
			ca_chain = optarg;
			break;
		case 'C':
			cert_dir = optarg;
			break;
		case 'x':
			require_tls++;
			break;
		case 'X':
			check_certificate++;
			break;
#endif
		case 'd':
			cmdFlags |= CMD_DELETE;
			break;
		case 'l':
			cmdFlags |= CMD_LIST;
			break;
		case 'r':
			cmdFlags |= CMD_READ;
			break;
		case 'm':
			cmdFlags |= CMD_MBOX;
			break;
		case 's':
			cmdFlags |= CMD_STATUS;
			break;
		case 'u':
			cmdFlags |= CMD_UIDL;
			break;
		case 'h':
			popHost = optarg;
			break;
		case 'p':
			popPort = strtol(optarg, NULL, 10);
			break;
		case 't':
			socketTimeout = strtol(optarg, NULL, 10);
			break;
		case 'v':
			LogSetProgramName(_NAME);
			debug++;
			break;
		default:
			fprintf(stderr, usage);
			return EXIT_USAGE;
		}
	}

	if (argc < optind + 2) {
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

	if (socket3_init_tls()) {
		syslog(LOG_ERR, "socket3_init_tls() failed");
		goto error0;
	}
#ifdef HAVE_OPENSSL_SSL_H
	if (socket3_set_ca_certs(cert_dir, ca_chain)) {
		syslog(LOG_ERR, "socket3_set_ca_certs() failed");
		goto error1;
	}
#endif
	syslog(LOG_INFO, "connecting to host=%s port=%ld", popHost, popPort);

	if ((address = socketAddressCreate(popHost, popPort)) == NULL) {
		syslog(LOG_ERR, "failed to find host %s:%ld", popHost, popPort);
		goto error1;
	}

	if ((pop = socket3_open(address, 1)) < 0) {
		syslog(LOG_ERR, "failed to create socket to host %s:%ld", popHost, popPort);
		goto error2;
	}

	if (socket3_client(pop, address, socketTimeout)) {
		syslog(LOG_ERR, "failed to connect to host %s:%ld", popHost, popPort);
		goto error3;
	}

#if defined(NON_BLOCKING_READ) && defined(NON_BLOCKING_WRITE)
	/* Set non-blocking I/O once for both getPopResponse() and
	 * printline() and leave it that way.
	 */
	if (socket3_set_nonblocking(pop, 1)) {
		syslog(LOG_ERR, "internal error: socketSetNonBlocking() failed: %s (%d)", strerror(errno), errno);
		goto error3;
	}
#endif

	if (getPopResponse(pop, line, sizeof (line))) {
		syslog(LOG_ERR, "host %s:%ld responded with an error: %s", popHost, popPort, line);
		goto error3;
	}

#ifdef HAVE_OPENSSL_SSL_H
	if (require_tls && popStartTLS(pop)) {
		syslog(LOG_ERR, "TLS connection required");
		goto error3;
	}

	if (check_certificate && !socket3_is_cn_tls(pop, popHost)) {
		syslog(LOG_ERR, "invalid certificate for %s", popHost);
		goto error3;
	}
#endif
	/* Look for timestamp in welcome banner. */
	if ((offset = TextFind(line, "*<*@*>*", -1, 1)) == -1 ) {
		/* Use USER/PASS clear text login. */
		snprintf(line, sizeof (line), "USER %s\r\n", argv[optind]);
		printline(pop, line);
		if (getPopResponse(pop, line, sizeof (line))) {
			syslog(LOG_ERR, "USER %s failed: %s", argv[optind], line);
			goto error4;
		}
		optind++;

		snprintf(line, sizeof (line), "PASS %s\r\n", argv[optind]);
		printline(pop, line);
		if (getPopResponse(pop, line, sizeof (line))) {
			syslog(LOG_ERR, "PASS command failed: %s", line);
			goto error4;
		}
		optind++;
	} else {
		/* Use APOP login */
		md5_state_t md5;
		unsigned char digest[16];

		md5_init(&md5);
		for (span = offset + strcspn(line+offset, ">")+1 ; offset < span; offset++)
			md5_append(&md5, (md5_byte_t *) &line[offset], 1);
		md5_append(&md5, (md5_byte_t *) argv[optind+1], strlen(argv[optind+1]));
		md5_finish(&md5, (md5_byte_t *) digest);

		snprintf(line, sizeof (line), "APOP %s %n___32 ASCII hex digits of MD5___\r\n", argv[optind], &offset);
		md5_digest_to_string(digest, line+offset);
		line[offset+32] = '\r';
		printline(pop, line);
		if (getPopResponse(pop, line, sizeof (line))) {
			syslog(LOG_ERR, "APOP %s failed: %s", argv[optind], line);
			goto error4;
		}
		optind += 2;
	}

	syslog(LOG_INFO, "user %s logged in", argv[optind-2]);

	if (popStatus(pop, &messages, &octets))
		goto error4;

	if (cmdFlags & CMD_STATUS)
		printf("%ld %ld\r\n", messages, octets);

	if (cmdFlags & CMD_LIST) {
		if (optind < argc) {
			if (forEachArg(pop, argc, argv, optind, popList))
				goto error4;
		} else if (forEach(pop, 1, messages, popList)){
			goto error4;
		}
	}

	if (cmdFlags & CMD_UIDL) {
		if (optind < argc) {
			if (forEachArg(pop, argc, argv, optind, popUidl))
				goto error4;
		} else if (forEach(pop, 1, messages, popUidl)){
			goto error4;
		}
	}

	if (cmdFlags & CMD_READ) {
		if (optind < argc) {
			if (forEachArg(pop, argc, argv, optind, popRead))
				goto error4;
		} else if (forEach(pop, 1, messages, popRead)){
			goto error4;
		}
	}

	if (cmdFlags & CMD_DELETE) {
		if (optind < argc) {
			if (forEachArg(pop, argc, argv, optind, popDelete))
				goto error4;
		} else if (forEach(pop, 1, messages, popDelete)){
			goto error4;
		}
	}

	if (cmdFlags == 0) {
		printf("%ld %ld\r\n", messages, octets);
	}
error4:
	printline(pop, "QUIT\r\n");
	rc = getPopResponse(pop, line, sizeof (line)) != 0;
	syslog(LOG_INFO, "user %s logged out", argv[optind-2]);
error3:
	socket3_close(pop);
error2:
	free(address);
error1:
	socket3_fini();
error0:
	return rc;
}


