/*
 * clamstream.c
 *
 * Copyright 2005, 2006 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** You can change the stuff below if the configure script doesn't work.
 ***********************************************************************/

#ifndef CLAM_PORT
#define CLAM_PORT		3310
#endif

#ifndef SOCKET_TIMEOUT
#define SOCKET_TIMEOUT		30000
#endif

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include <com/snert/lib/io/socket2.h>
#include <com/snert/lib/util/Text.h>
#include <com/snert/lib/util/Token.h>
#include <com/snert/lib/util/getopt.h>

/***********************************************************************
 *** Constants
 ***********************************************************************/

#define _NAME		"clamstream"
#define _VERSION	"0.1"

/***********************************************************************
 *** Global Variables
 ***********************************************************************/

static char *clamdHost = "0.0.0.0";
static char buffer[SOCKET_BUFSIZ];
static long socketTimeout = SOCKET_TIMEOUT;

static char *usageMessage =
"usage: " _NAME " [-H host[,port]][-t timeout] <input\n"
"\n"
"-H host[,port]\taddress of a clamd server, default is 0.0.0.0:3310\n"
"-t timeout\tsocket timeout in seconds, default 5m\n"
"\n"
_NAME "/" _VERSION " Copyright 2005, 2006 by Anthony Howe. All rights reserved.\n"
;

/***********************************************************************
 *** Routines
 ***********************************************************************/

int
main(int argc, char **argv)
{
	size_t length;
	int rc = 2, ch;
	unsigned sessionPort;
	Socket2 *clam, *session;
	SocketAddress *caddr, *saddr;

	while ((ch = getopt(argc, argv, "H:t:v")) != -1) {
		switch (ch) {
		case 'H':
			clamdHost = optarg;
			break;
		case 't':
			socketTimeout = strtol(optarg, NULL, 10) * 1000;
			break;
		default:
			(void) fprintf(stderr, usageMessage);
			exit(2);
		}
	}

#ifdef __unix__
	(void) signal(SIGPIPE, SIG_IGN);
#endif

	if (socketInit()) {
		fprintf(stderr, "clamstrem socket initialisation error: %s (%d)\n", strerror(errno), errno);
		goto error0;
	}

	if ((caddr = socketAddressCreate(clamdHost, CLAM_PORT)) == NULL) {
		fprintf(stderr, "clamd server address error: %s (%d)\n", strerror(errno), errno);
		goto error0;
	}

	if ((clam = socketOpen(caddr, 1)) == NULL) {
		fprintf(stderr, "clamd server open error: %s (%d)\n", strerror(errno), errno);
		goto error1;
	}

	if (socketClient(clam, socketTimeout)) {
		fprintf(stderr, "clamd server connection error: %s (%d)\n", strerror(errno), errno);
		goto error2;
	}

	socketSetTimeout(clam, socketTimeout);

	if (socketWrite(clam, (unsigned char *) "STREAM\n", sizeof ("STREAM\n")-1) != sizeof ("STREAM\n")-1) {
		fprintf(stderr, "clamd server write error: %s (%d)\n", strerror(errno), errno);
		goto error2;
	}

	if (socketReadLine(clam, buffer, sizeof (buffer)) < 0) {
		fprintf(stderr, "clamd server read error: %s (%d)\n", strerror(errno), errno);
		goto error2;
	}

	if (sscanf(buffer, "PORT %u", &sessionPort) != 1) {
		fprintf(stderr, "clamd session port \"%s\" parse error\n", buffer);
		goto error2;
	}

	if ((saddr = socketAddressCreate(*clamdHost == '/' ? "0.0.0.0" : clamdHost, sessionPort)) == NULL) {
		fprintf(stderr, "clamd server address error: %s (%d)\n", strerror(errno), errno);
		goto error2;
	}

	if (*clamdHost != '/')
		(void) socketAddressSetPort(saddr, sessionPort);

	if ((session = socketOpen(saddr, 1)) == NULL) {
		fprintf(stderr, "clamd session open error: %s (%d)\n", strerror(errno), errno);
		goto error3;
	}

	if (socketClient(session, socketTimeout)) {
		fprintf(stderr, "clamd session connection error: %s (%d)\n", strerror(errno), errno);
		goto error4;
	}

	while (0 < (length = fread(buffer, 1, sizeof (buffer), stdin))) {
		if (socketWrite(session, (unsigned char *) buffer, length) != length) {
			fprintf(stderr, "clamd session write error: %s (%d)\n", strerror(errno), errno);
			goto error4;
		}
	}

	socketClose(session);
	session = NULL;

	if (socketHasInput(clam, socketTimeout)) {
		if (socketReadLine(clam, buffer, sizeof (buffer)) < 0) {
			fprintf(stderr, "clamd session read error: %s (%d)\n", strerror(errno), errno);
			goto error4;
		}

		if ((rc = strstr(buffer, "FOUND") != NULL))
			printf("%s\n", buffer);
	}
error4:
	socketClose(session);
error3:
	free(saddr);
error2:
	socketClose(clam);
error1:
	free(caddr);
error0:
	return rc;
}

