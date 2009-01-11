/*
 * sendform.c
 *
 * Copyright 2004, 2005 by Anthony Howe.  All rights reserved.
 */

#define NDEBUG 1

#define SMTP_PORT		25

#define NON_BLOCKING_READ
#define NON_BLOCKING_WRITE

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __unix__
#include <syslog.h>
#endif

#include <com/snert/lib/io/Log.h>
#include <com/snert/lib/io/posix.h>
#include <com/snert/lib/io/socket2.h>
#include <com/snert/lib/util/Text.h>

/***********************************************************************
 *** Global Variables
 ***********************************************************************/

typedef struct email {
	struct email *next;
	char email[256];
} Email;

typedef struct {
	Email *rcpt;
	char subject[256];
	char smtp_host[256];
	char error_url[512];
	char thanks_url[512];
} Cfg;

static int debug;
static int isCGI;
static int isNPH;
static size_t maxWidth;
static Cfg cfg;
static char *(*_GET)[2];
static char *(*_POST)[2];
static char *sender = "";
static char line[512];
static char format[20];
static char *postInput;
static char hostname[256];
static long socketTimeout = 120000;

static char *usage =
"\033[1musage: sendform [-cn][file.cfg]\033[0m\n"
"\n"
"-c\t\tis a CGI, write Content-Type header\n"
"-n\t\tis a non-parsed header CGI, implies -c\n"
"file.cfg\ta configuration file\n"
"\n"
"A configuration file can contain the following fields:\n"
"\n"
"\tsubject:\t$subject\n"
"\tsmtp-host:\t$host\n"
"\terror-url:\t$URL\n"
"\tthanks-url:\t$URL\n"
"\trecipient:\t$email\n"
"\n"
"There can be more than one recipient line. If the form contains the field\n"
"`sender', then the mail will appear to be from that address. If smpt-host\n"
"is not specified, then 127.0.0.1 will be used.\n"
"\n"
"If the environment variable PATH_TRANSLATED is defined, then it refers to\n"
"a configuration file to be used. So for example a web based form might look\n"
"like this:\n"
"\n"
"\t<form method=\"POST\" action=\"sendform.cgi/path/to/file.cfg\">\n"
"\tEmail: <input type=\"text\" name=\"sender\" value=\"\"><br/>\n"
"\tTel:   <input type=\"text\"  name=\"tel\" value=\"\"><br/>\n"
"\t<input type=\"submit\" value=\"SEND\">\n"
"\t</form>\n"
"\n"
"If the executable is called nph-coutner.cgi its equivalent to -n. If the\n"
"executable is called counter.cgi its equivalent to -c.\n"
"\n"
"Standard input is read for URL encoded form content, transformed, and mailed.\n"
"to each recipient specified in the the configuration file.\n"
"\n"
"\033[1msendform/1.1 Copyright 2004, 2005 by Anthony Howe. All rights reserved.\033[0m\n"
;

/***********************************************************************
 *** Routines
 ***********************************************************************/

/**
 * @param tp
 *	A pointer to pointer of char. Decoded bytes from the source
 *	string are copied to this destination. The destination buffer
 *	must be as large as the source. The copied string is '\0'
 *	terminated and the pointer passed back points to next byte
 *	after the terminating '\0'.
 *
 * @param sp
 * 	A pointer to pointer of char. The URL encoded bytes are copied
 *	from this source buffer to the destination. The copying stops
 *	after an equals-sign, ampersand, or on a terminating '\0' and
 *	this pointer is passed back.
 */
void
cgiUrlDecode(char **tp, char **sp)
{
	int hex;
	char *t, *s;

	for (t = *tp, s = *sp; *s != '\0'; t++, s++) {
		switch (*s) {
		case '=':
		case '&':
			s++;
			break;
		case '+':
			*t = ' ';
			continue;
		case '%':
			if (sscanf(s+1, "%2x", &hex) == 1) {
				*t = (char) hex;
				s += 2;
				continue;
			}
			/*@fallthrough@*/
		default:
			*t = *s;
			continue;
		}
		break;
	}

	/* Terminate decoded string. */
	*t = '\0';

	/* Pass back the next unprocessed location.
	 * For the source '\0' byte, we stop on that.
	 */
	*tp = t+1;
	*sp = s;
}

/**
 * @param urlencoded
 *	A URL encoded string such as the query string portion of an HTTP
 *	request or HTML form data ie. application/x-www-form-urlencoded.
 *
 * @return
 *	A pointer to array 2 of pointer to char. The first column of the
 *	table are the field names and the second column their associated
 *	values. The array is NULL terminated. The array pointer returned
 *	must be released with a single call to free().
 */
char *(*
cgiParseForm(char *urlencoded))[2]
{
	size_t length;
	int nfields, i;
	char *s, *t, *(*out)[2];

	if (urlencoded == NULL)
		return NULL;

	nfields = 1;
	for (s = urlencoded; *s != '\0'; s++) {
		if (*s == '&')
			nfields++;
	}

	if (debug) syslog(LOG_DEBUG, "nfields=%d sizeof *out=%lu", nfields, (long unsigned)  sizeof (*out));

	if ((out = malloc((nfields + 1) * sizeof (*out) + strlen(urlencoded) + 1)) == NULL)
		return NULL;

	s = urlencoded;
	t = (char *) &out[nfields+1];

	for (i = 0; i < nfields; i++) {
		out[i][0] = t;
		cgiUrlDecode(&t, &s);

		out[i][1] = t;
		cgiUrlDecode(&t, &s);

		if (strcmp(out[i][0], "sender") == 0)
			sender = out[i][1];

		length = strlen(out[i][0]);
		if (maxWidth < length)
			maxWidth = length;

		if (debug) syslog(LOG_DEBUG, "name={%s} 0x%lx value={%s} 0x%lx", out[i][0], (long) out[i][0], out[i][1], (long) out[i][1]);
	}

	out[i][0] = NULL;
	out[i][1] = NULL;

	return out;
}

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
		syslog(LOG_ERR, "printline() error %d", errno);
		goto error0;
	}

	/* Now wait for the output to be sent to the SMTP server. */
	if (!socketCanSend(s, socketTimeout)) {
		syslog(LOG_ERR, "timeout before output sent to SMTP server");
		goto error0;
	}
#else
	if (socketWrite(s, line, (long) strlen(line)) == SOCKET_ERROR) {
		syslog(LOG_ERR, "printline() error %d", errno);
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
getSmtpResponse(Socket2 *s, int code, char *line, long size)
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
	return value == code && line + 3 == stop;
}

static int
mailTo(Cfg *cfg)
{
	int rc;
	long i;
	time_t now;
	Email *rcpt;
	Socket2 *smtp;
	struct tm *ltime;

	rc = -1;

	if (socketOpenClient(cfg->smtp_host, SMTP_PORT, socketTimeout, NULL, &smtp) != 0)
		goto error0;

#if defined(__BORLANDC__)
	setmode(SocketGetFd(smtp), O_BINARY);
#endif
#if defined(NON_BLOCKING_READ) && defined(NON_BLOCKING_WRITE)
	/* Set non-blocking I/O once for both getSmtpResponse() and
	 * printline() and leave it that way.
	 */
	if (socketSetNonBlocking(smtp, 1))
		goto error1;
#endif
	/* Read welcome response. */
	if (!getSmtpResponse(smtp, 220, line, sizeof (line))) {
		goto error1;
	}

#if defined(__BORLANDC__)
#else
#endif
	(void) gethostname(hostname, sizeof (hostname));
	(void) snprintf(line, sizeof (line), "HELO %s\r\n", hostname);
	if (!printline(smtp, line)
	|| !getSmtpResponse(smtp, 250, line, sizeof (line))) {
		goto error1;
	}

	(void) snprintf(line, sizeof (line), "MAIL FROM:<%s>\r\n", sender == NULL ? "" : sender);
	if (!printline(smtp, line)
	|| !getSmtpResponse(smtp, 250, line, sizeof (line))) {
		goto error1;
	}

	for (rcpt = cfg->rcpt; rcpt != NULL; rcpt = rcpt->next) {
		(void) snprintf(line, sizeof (line), "RCPT TO:<%s>\r\n", rcpt->email);
		if (!printline(smtp, line)
		|| !getSmtpResponse(smtp, 250, line, sizeof (line))) {
			goto error1;
		}
	}

	if (!printline(smtp, "DATA\r\n")
	|| !getSmtpResponse(smtp, 354, line, sizeof (line))) {
		goto error1;
	}

	snprintf(line, sizeof (line), "Subject: %s\r\n", cfg->subject);
	(void) printline(smtp, line);

	snprintf(line, sizeof (line), "From: \"sendform\" <%s>\r\n", sender == NULL ? "sendform" : sender);
	(void) printline(smtp, line);

	time(&now);
	ltime = localtime(&now);
	(void) strftime(line, sizeof (line), "Date: %a, %d %b %Y %H:%M:%S %Z\r\n", ltime);
	(void) printline(smtp, line);

	(void) printline(smtp, "\r\n");

	if (_GET != NULL) {
		for (i = 0; _GET[i][0] != NULL; i++) {
			(void) snprintf(line, sizeof (line), format, _GET[i][0], _GET[i][1]);
			if (!printline(smtp, line)) {
				goto error1;
			}
		}
	}

	if (_POST != NULL) {
		for (i = 0; _POST[i][0] != NULL; i++) {
			(void) snprintf(line, sizeof (line), format, _POST[i][0], _POST[i][1]);
			if (!printline(smtp, line)) {
				goto error1;
			}
		}
	}

	if (!printline(smtp, ".\r\n")
	|| !getSmtpResponse(smtp, 250, line, sizeof (line))) {
		goto error1;
	}

	(void) printline(smtp, "QUIT\r\n");

	rc = 0;
error1:
	socketClose(smtp);
error0:
	return rc;
}

static int
sendform(char *filename)
{
	int rc;
	FILE *fp;
	Email *rcpt, *next;

	rc = -1;

	if (filename == NULL)
		return 0;

	if ((fp = fopen(filename, "r")) == NULL)
		goto error0;

	memset(&cfg, 0, sizeof (cfg));

	while (0 <= TextInputLine(fp, line, sizeof (line))) {
		sscanf(line, "subject: %255[^\r\n]", cfg.subject);
		sscanf(line, "smtp-host: %255s", cfg.smtp_host);
		sscanf(line, "error-url: %511s", cfg.error_url);
		sscanf(line, "thanks-url: %511s", cfg.thanks_url);

		if (strncmp(line, "recipient", sizeof ("recipient")-1) == 0) {
			if ((rcpt = malloc(sizeof (*rcpt))) == NULL)
				goto error2;

			sscanf(line, "recipient: %255s", rcpt->email);
			rcpt->next = cfg.rcpt;
			cfg.rcpt = rcpt;
		}
	}

	if (debug) {
		syslog(LOG_DEBUG, "sender='%s' 0x%lx", sender, (long) sender);
		syslog(LOG_DEBUG, "subject=%s", cfg.subject);
		syslog(LOG_DEBUG, "smtp-host=%s", cfg.smtp_host);
		syslog(LOG_DEBUG, "error-url=%s", cfg.error_url);
		syslog(LOG_DEBUG, "thanks-url=%s", cfg.thanks_url);
		for (rcpt = cfg.rcpt; rcpt != NULL; rcpt = rcpt->next) {
			syslog(LOG_DEBUG, "recipient=%s", rcpt->email);
		}
	}

	rc = mailTo(&cfg);
error2:
	for (rcpt = cfg.rcpt; rcpt != NULL; rcpt = next) {
		next = rcpt->next;
		free(rcpt);
	}

	fclose(fp);
error0:
	return rc;
}

static void
redirectTo(char *url)
{
	char *p;

	if (isNPH) {
		p = getenv("SERVER_PROTOCOL");
		printf("%s 303 See Other\r\n", p == NULL ? "1.0" : p);
		isCGI = 1;
	}

	if (isCGI) {
		printf("Location: %s\r\n\r\n", url);
		printf("<html>\n<body>\n<a href='%s'>%s</a>\n</body>\n</html>\n", url, url);
	}
}

int
main(int argc, char **argv)
{
	char *pt;
	long length;
	int ch, argi;

	openlog("sendform", LOG_PID, LOG_MAIL);

#if defined(__BORLANDC__)
	_fmode = O_BINARY;
#endif
#if defined(__BORLANDC__)
	setmode(0, O_BINARY);
	setmode(1, O_BINARY);
#endif

	if ((pt = strrchr(argv[0], '/')) == NULL
	&&  (pt = strrchr(argv[0], '\\')) == NULL)
		pt = argv[0];
	else
		pt++;

	isNPH = strncmp(pt, "nph-", 4) == 0;
	isCGI = strstr(pt, ".cgi") != NULL;

	if ((pt = getenv("QUERY_STRING")) != NULL && strstr(pt, "debug=yes") != NULL) {
		debug = 1;
	}

	for (argi = 1; argi < argc; argi++) {
		if (argv[argi][0] != '-')
			break;

		switch (argv[argi][1]) {
		case 'c':
			isCGI = 1;
			break;
		case 'n':
			isNPH = 1;
			break;
		case 'v':
			debug = 1;
			break;
		default:
			fprintf(stderr, "invalid option -%c\n%s", argv[argi][1], usage);
			return 2;
		}
	}

	/* Look for web form input. */
	if ((pt = getenv("CONTENT_LENGTH")) != NULL)
		length = strtol(pt, NULL, 10);
	else
		length = BUFSIZ;

	if ((pt = getenv("PATH_TRANSLATED")) == NULL && argc <= argi) {
		fprintf(stderr, "%s", usage);
		return 2;
	}

	if (socketInit()) {
		syslog(LOG_ERR, "socketInit() error");
		return 1;
	}

	if ((postInput = malloc(length+1)) == NULL) {
		syslog(LOG_ERR, "failed to allocate memory");
		return 1;
	}

	if (pt == NULL) {
		if (debug) syslog(LOG_DEBUG, "no PATH_TRANSLATED, read from standard input");
		for (length = 0; length < BUFSIZ && (ch = fgetc(stdin)) != EOF; length++) {
#ifndef NDEBUG
			if (debug) syslog(LOG_DEBUG, "ch=%d %c", ch, ch);
#endif
			postInput[length] = (char) ch;
		}
	} else {
		if (debug) syslog(LOG_DEBUG, "PATH_TRANSLATED=%s, read from standard input", pt);
		if ((length = (long) read(0, postInput, (size_t) length)) < 0) {
			syslog(LOG_ERR, "error reading content");
			return 1;
		}
	}

	if (debug) syslog(LOG_DEBUG, "length=%ld", length);
	postInput[length] = '\0';

	maxWidth = 0;
	_GET = cgiParseForm(getenv("QUERY_STRING"));
	_POST = cgiParseForm(postInput);
	snprintf(format, sizeof (format), "%%%lus: %%s\r\n", (long) maxWidth);

	if (sendform(pt)) {
		syslog(LOG_ERR, "error, PATH_TRANSLATED=%s", pt);
		redirectTo(cfg.error_url);
		return 0;
	} else if (argi < argc && sendform(argv[argi])) {
		syslog(LOG_ERR, "error, configuration file %s", argv[argi]);
		redirectTo(cfg.error_url);
		return 0;
	}

	redirectTo(cfg.thanks_url);
	if (debug) syslog(LOG_DEBUG, "OK");

	return 0;
}

