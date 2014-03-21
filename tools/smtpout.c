/*
 * smtpout.c
 *
 * Copyright 2002, 2006 by Anthony Howe.  All rights reserved.
 *
 *
 * Description
 * -----------
 *
 *	smtpout [-v][-h host][-p port][-f from] to ... <message
 *
 * A command-line SMTP client intended for scripted use. Previous bundled
 * with LibSnert as `mailout', the tool was renamed to compliment the
 * `popin' tool.
 *
 * See also
 * --------
 *
 *	popin.c
 *	mail-cycle.sh
 */

#ifndef DEFAULT_SMTP_HOST
#define DEFAULT_SMTP_HOST		"127.0.0.1"
#endif

#ifndef DEFAULT_SMTP_PORT
#define DEFAULT_SMTP_PORT		25
#endif

#ifndef DEFAULT_SOCKET_TIMEOUT
#define DEFAULT_SOCKET_TIMEOUT		120000
#endif

#ifndef DEFAULT_SENDER
#define DEFAULT_SENDER			""
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

#ifdef __unix__
# include <syslog.h>
#endif

#ifdef HAVE_IO_H
# include <io.h>
#endif

#include <com/snert/lib/io/Log.h>
#include <com/snert/lib/io/Dns.h>
#include <com/snert/lib/io/posix.h>
#include <com/snert/lib/io/socket2.h>
#include <com/snert/lib/util/Text.h>

/***********************************************************************
 *** Constants
 ***********************************************************************/

#define INPUT_LINE_SIZE		1000
#define HOST_NAME_SIZE		128
#define MAIL_ADDRESS_SIZE	256

/***********************************************************************
 *** Global Variables
 ***********************************************************************/

static int debug;
static char *sender = DEFAULT_SENDER;
static char *smtpHost = DEFAULT_SMTP_HOST;
static long smtpPort = DEFAULT_SMTP_PORT;
static long socketTimeout = DEFAULT_SOCKET_TIMEOUT;

static char line[INPUT_LINE_SIZE+1];
static char hostname[HOST_NAME_SIZE+1];

static char usage[] =
"usage: smtpout [-v][-h host][-p port][-f from] to ... <message\n"
"\n"
"-f from\t\tsender email address, default is <>\n"
"-h host\t\tSMTP host to contact, default localhost\n"
"-p port\t\tSMTP port to connect to, default 25\n"
"-v\t\tverbose debug messages\n"
"to ...\t\tone or more recipient email addresses\n"
"message\t\tmessage content is read from standard input.\n"
"\n"
"smtpout/1.11 Copyright 2002, 2006 by Anthony Howe.  All rights reserved.\n"
;

/***********************************************************************
 *** Supporting Cast
 ***********************************************************************/

#ifdef CHECK_ADDRESS_SYNTAX
#include <com/snert/lib/mail/MailSpan.h>

int
isMailAddress(const char *mail)
{
	long length;

	if (*mail == '\0')
		return EXIT_FAILURE;

	length = MailSpanLocalPart(mail);
	if (mail[length] != '@')
		return 0;

	mail = &mail[length + 1];
	length = MailSpanDomainName(mail, 1);
	if (mail[length] != '\0')
		return 0;

	return EXIT_FAILURE;
}
#endif

int
printline(Socket2 *s, char *line)
{
	int rc = 0;

	if (debug)
		syslog(LOG_DEBUG, "> %s", line);

	if (socketWrite(s, (unsigned char *) line, (long) strlen(line)) == SOCKET_ERROR) {
		syslog(LOG_ERR, "socket write error: %s (%d)", strerror(errno), errno);
		goto error0;
	}

	/* Now wait for the output to be sent to the SMTP server. */
	if (!socketCanSend(s, socketTimeout)) {
		syslog(LOG_ERR, "timeout before output sent to SMTP server");
		goto error0;
	}
	rc = 1;
error0:
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
getSmtpResponse(Socket2 *s, char *line, long size, int *code)
{
	char *stop;
	long length, value;

	/* Ideally we should collect _all_ the response lines into a variable
	 * length buffer (see com/snert/lib/util/Buf.h), but I don't see any
	 * real need for it just yet.
	 */

	socketSetTimeout(s, socketTimeout);

	do {
		/* Reset this return code for each input line of a
		 * multiline response in case there is a read error.
		 */
		value = 450;
		stop = line;

		/* Erase the first 4 bytes of the line buffer, which
		 * corresponds with the 3 ASCII digit status code
		 * followed by either a ASCII hyphen or space.
		 */
		line[0] = line[1] = line[2] = line[4] = '\0';

		switch (length = socketReadLine(s, line, size)) {
		case SOCKET_ERROR:
			syslog(LOG_ERR, "read error : %s (%d)", strerror(errno), errno);
			goto error0;
		case SOCKET_EOF:
			syslog(LOG_ERR, "unexpected EOF");
			goto error0;
		}

		/* Did we read sufficient characters for a response code? */
		if (length < 4) {
			syslog(LOG_ERR, "truncated response, length=%ld", length);
			goto error0;
		}

		if (debug)
			syslog(LOG_DEBUG, "< %s", line);

		if ((value = strtol(line, &stop, 10)) == 421) {
			break;
		}
	} while (line + 3 == stop && line[3] == '-');
error0:
	if (code != NULL)
		*code = value;

	return value;
}

int
main(int argc, char **argv)
{
	long length;
	Socket2 *client;
	int argi, code;
	char *arg, *stop;
	SocketAddress *address;

#if defined(__BORLANDC__) || defined(__CYGWIN__)
	setmode(1, O_BINARY);
#endif
	for (argi = 1; argi < argc; argi++) {
		if (argv[argi][0] != '-' || (argv[argi][1] == '-' && argv[argi][2] == '\0'))
			break;

		switch (argv[argi][1]) {
		case 'f':
			sender = argv[argi][2] == '\0' ? argv[++argi] : &argv[argi][2];
			break;
		case 'h':
			smtpHost = argv[argi][2] == '\0' ? argv[++argi] : &argv[argi][2];
			break;
		case 'p':
			arg = argv[argi][2] == '\0' ? argv[++argi] : &argv[argi][2];
			if ((smtpPort = strtol(arg, &stop, 10)) <= 0 || *stop != '\0') {
				fprintf(stderr, "invalid SMTP port number\n%s", usage);
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
			DnsSetDebug(1);
			debug = 1;
			break;
		default:
			fprintf(stderr, "invalid option -%c\n%s", argv[argi][1], usage);
			return EXIT_USAGE;
		}
	}

	if (argc <= argi) {
		fprintf(stderr, "missing a recipient address\n%s", usage);
		return EXIT_USAGE;
	}

 	if (smtpHost == NULL) {
		fprintf(stderr, "missing SMTP host name or address\n%s", usage);
		return EXIT_USAGE;
	}

#ifdef CHECK_ADDRESS_SYNTAX
	if (!isMailAddress(sender)) {
		syslog(LOG_ERR, "sender <%s> syntax error", sender);
		return EXIT_USER;
	}
#endif
	socketTimeout *= 1000;

	openlog("smtpout", LOG_PID, LOG_MAIL);

	if (socketInit()) {
		syslog(LOG_ERR, "socketInit() failed");
		return EXIT_FAILURE;
	}

	syslog(LOG_INFO, "connecting to host=%s port=%ld", smtpHost, smtpPort);

	if ((address = socketAddressCreate(smtpHost, smtpPort)) == NULL) {
		syslog(LOG_ERR, "failed to find host %s:%ld", smtpHost, smtpPort);
		return EXIT_HOST;
	}

	if ((client = socketOpen(address, 1)) == NULL) {
		syslog(LOG_ERR, "failed to create socket to host %s:%ld", smtpHost, smtpPort);
		return EXIT_HOST;
	}

	if (socketClient(client, socketTimeout)) {
		syslog(LOG_ERR, "failed to connect to host %s:%ld", smtpHost, smtpPort);
		return EXIT_HOST;
	}

	/* Set non-blocking I/O once for both getSmtpResponse() and
	 * printline() and leave it that way.
	 */
	if (socketSetNonBlocking(client, 1)) {
		syslog(LOG_ERR, "internal error: socketSetNonBlocking() failed: %s (%d)", strerror(errno), errno);
		return EXIT_IO;
	}

	if (getSmtpResponse(client, line, sizeof (line), &code) != 220) {
		syslog(LOG_ERR, "host %s:%ld responded with a busy signal: %s", smtpHost, smtpPort, line);
		return EXIT_HOST;
	}

	(void) gethostname(hostname, HOST_NAME_SIZE);
	snprintf(line, sizeof (line), "HELO %s\r\n", hostname);
	printline(client, line);
	if (getSmtpResponse(client, line, sizeof (line), &code) != 250) {
		syslog(LOG_ERR, "host %s:%ld did not accept HELO: %s", smtpHost, smtpPort, line);
		return EXIT_FAILURE;
	}

	snprintf(line, sizeof (line), "MAIL FROM:<%s>\r\n", sender);
	printline(client, line);
	if (getSmtpResponse(client, line, sizeof (line), &code) != 250) {
		syslog(LOG_ERR, "host %s:%ld will not accept MAIL FROM:<%s>: %s", smtpHost, smtpPort, sender, line);
		return EXIT_USER;
	}
	syslog(LOG_INFO, "sender <%s>", sender);

	for ( ; argi < argc; argi++) {
#ifdef CHECK_ADDRESS_SYNTAX
		if (!isMailAddress(argv[argi])) {
			syslog(LOG_ERR, "recipient <%s> syntax error", argv[argi]);
			return EXIT_USER;
		}
#endif
		snprintf(line, sizeof (line), "RCPT TO:<%s>\r\n", argv[argi]);
		printline(client, line);
		if (getSmtpResponse(client, line, sizeof (line), &code) != 250) {
			syslog(LOG_ERR, "host %s:%ld will not accept RCPT TO:<%s>: %s", smtpHost, smtpPort, argv[argi], line);
			return EXIT_USER;
		}
		syslog(LOG_INFO, "recipient <%s>",  argv[argi]);
	}

	printline(client, "DATA\r\n");
	if (getSmtpResponse(client, line, sizeof (line), &code) != 354) {
		syslog(LOG_ERR, "host %s:%ld did not accept DATA command: %s", smtpHost, smtpPort,  line);
		return EXIT_FAILURE;
	}

	while (0 <= (length = TextReadLine(FILENO_STDIN, line, sizeof (line)))) {
		if (line[0] == '.') {
			if (line[1] == '\0')
				break;
			socketWrite(client, (unsigned char *) ".", 1);
		}

		if ((long) sizeof (line)-3 <= length) {
			syslog(LOG_WARN, "message input line truncated");
			length = sizeof (line)-3;
		}

		line[length++] = '\r';
		line[length++] = '\n';
		line[length] = '\0';

		printline(client, line);
	}

	printline(client, ".\r\n");
	if (getSmtpResponse(client, line, sizeof (line), &code) != 250) {
		syslog(LOG_ERR, "host %s:%ld did not accept message: %d %s", smtpHost, smtpPort, code, line);
		return EXIT_FAILURE;
	}

	printline(client, "QUIT\r\n");
	syslog(LOG_INFO, "message sent");
#ifdef DONT_DO_THIS
	(void) socketReadLine(client, line, sizeof (line));
#endif
	socketClose(client);
	free(address);

	if (debug)
		syslog(LOG_DEBUG, "done");

	return EXIT_SUCCESS;
}


