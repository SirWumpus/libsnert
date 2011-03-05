/*
 * smtpOpen.c
 *
 * Open an SMTP session and message.
 *
 * Copyright 2006 by Anthony Howe. All rights reserved.
 */

#ifndef SMTP_CONNECT_TIMEOUT
#define SMTP_CONNECT_TIMEOUT	(60 * 1000)
#endif

#ifndef SMTP_COMMAND_TIMEOUT
#define SMTP_COMMAND_TIMEOUT	(300 * 1000)
#endif

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#if defined(HAVE_SYSLOG_H) && ! defined(__MINGW32__)
# include <syslog.h>
#endif
#if defined(HAVE_UNISTD_H)
# include <unistd.h>
#endif
#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#include <com/snert/lib/io/socket2.h>
#include <com/snert/lib/io/Log.h>
#include <com/snert/lib/io/Dns.h>
#include <com/snert/lib/mail/smtp.h>
#include <com/snert/lib/mail/parsePath.h>
#include <com/snert/lib/util/Text.h>
#include <com/snert/lib/sys/Time.h>

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#define TAG_FORMAT	"%s "
#define TAG_ARGS	session->id
#define SYSLOG		if (1 < smtp_debug) syslog

static const char base62[] = "0123456789ABCDEFGHIJKLMNOPQRSYUVWXYZabcdefghijklmnopqrsyuvwxyz";

static int smtp_log;
static int smtp_debug;
static char this_addr[IPV6_STRING_LENGTH];
static char this_host[SMTP_DOMAIN_LENGTH+1];

/***********************************************************************
 ***
 ***********************************************************************/

static int
mxPrint(SMTP *session, struct smtp_connection *relay, const char *line, size_t length)
{
	SYSLOG(LOG_DEBUG, TAG_FORMAT "mx-domain=%s >> %s", TAG_ARGS, relay->domain, line);
	relay->smtp_error = smtpWrite(relay->mx, (char *) line, length);
	session->smtp_error = relay->smtp_error;

	if (smtp_log && relay->smtp_error != SMTP_ERROR_OK)
		syslog(LOG_ERR, TAG_FORMAT "mx-domain=%s %s", TAG_ARGS, relay->domain, smtpGetError(relay->smtp_error));

	return relay->smtp_error;
}

static int
mxResponse(SMTP *session, struct smtp_connection *relay, int *smtp_code)
{
	char **lines, **ln;

	if ((relay->smtp_error = smtpRead(relay->mx, &lines, smtp_code)) == SMTP_ERROR_OK) {
		if (smtp_debug) {
			for (ln = lines ; *ln != NULL; ln++)
				syslog(LOG_DEBUG, TAG_FORMAT "mx-domain=%s << %s", TAG_ARGS, relay->domain, *ln);
		}
		free(lines);
	} else if (smtp_log) {
		syslog(LOG_ERR, TAG_FORMAT "mx-domain=%s %s", TAG_ARGS, relay->domain, smtpGetError(relay->smtp_error));
	}

	session->smtp_error = relay->smtp_error;

	return relay->smtp_error;
}

static int
mxCommand(SMTP *session, struct smtp_connection *relay, const char *line, int expect)
{
	int smtp_code = 450;

	if (line != NULL && mxPrint(session, relay, line, strlen(line)))
		goto error0;

	if (mxResponse(session, relay, &smtp_code))
		goto error0;

	if (expect != smtp_code) {
		relay->smtp_error = 500 <= smtp_code ? SMTP_ERROR_REJECT : SMTP_ERROR_TEMPORARY;
	}
error0:
	return relay->smtp_error;
}

static Socket2 *
mxConnect(SMTP *session, const char *domain)
{
	long i;
	DnsEntry *mx;
	Vector mxlist;
	int preference;
	Socket2 *socket;
	const char *error;

	mx = NULL;
	socket = NULL;
	preference = 65535;

	if (DnsGet2(DNS_TYPE_MX, 1, domain, &mxlist, &error) != DNS_RCODE_OK) {
		if (smtp_log)
			syslog(LOG_ERR, TAG_FORMAT "domain=%s %s", TAG_ARGS, domain, error);
		return NULL;
	}

	/* Find the max. possible preference value. */
	for (i = 0; i < VectorLength(mxlist); i++) {
		if ((mx = VectorGet(mxlist, i)) == NULL)
			continue;

		/* RFC 3330 consolidates the list of special IPv4 addresses that
		 * cannot be used for public internet. We block those that cannot
		 * possibly be used for MX addresses on the public internet.
		 */
		if (mx->address_string == NULL || isReservedIPv6(mx->address, IS_IP_RESTRICTED)) {
			SYSLOG(LOG_DEBUG, TAG_FORMAT "removed MX %d %s [%s]", TAG_ARGS, mx->preference, (char *) mx->value, mx->address_string);
			VectorRemove(mxlist, i--);
			continue;
		}

		/* Look for our IP address to find our preference value,
		 * unless we are the lowest/first MX in the list.
		 */
		if (TextInsensitiveCompare(mx->address_string, this_addr) == 0)
			preference = mx->preference + (i == 0);
	}

	if (VectorLength(mxlist) <= 0)
		SYSLOG(LOG_DEBUG, TAG_FORMAT "mx-domain=%s has no acceptable MX", TAG_ARGS, domain);

	/* Try all MX of a lower preference until one answers. */
	for (i = 0; i < VectorLength(mxlist); i++) {
		if ((mx = VectorGet(mxlist, i)) == NULL)
			continue;

		if (preference <= mx->preference)
			continue;

		if (socketOpenClient(mx->value, SMTP_PORT, SMTP_CONNECT_TIMEOUT, NULL, &socket) == 0) {
			SYSLOG(LOG_DEBUG, TAG_FORMAT "mx-domain=%s connected to MX %d %s", TAG_ARGS, domain, mx->preference, (char *) mx->value);
			break;
		}
	}

#ifdef THIS_IS_SUSPECT
/* This is suspect- consider what happens when the A record points
 * to 127.0.0.1. The DNS MX lookup code already handles implicit MX 0
 * rule, so we don't need to repeat it here. If the MX pruning reduced
 * the list to zero, then thats correct.
 */
	if (socket == NULL && socketOpenClient(domain, SMTP_PORT, SMTP_CONNECT_TIMEOUT, NULL, &socket) == 0)
		SYSLOG(LOG_DEBUG, TAG_FORMAT "mx-domain=%s connected to MX %d %s", TAG_ARGS, domain, mx->preference, (char *) mx->value);
#endif
	VectorDestroy(mxlist);

	return socket;
}

/***********************************************************************
 ***
 ***********************************************************************/

size_t
smtpAssertCRLF(char *line, size_t length, size_t size)
{
	if (0 < length && line[length-1] == '\n')
		length--;
	if (0 < length && line[length-1] == '\r')
		length--;

	if (size - 3 <= length)
		length = size - 3;

	line[length++] = '\r';
	line[length++] = '\n';
	line[length  ] = '\0';

	return length;
}

void
smtpSetDebug(int flag)
{
	smtp_debug = flag;
}

void
smtpSetTimeout(SMTP *session, long ms)
{
	session->timeout = ms;
}

long
smtpGetTimeout(SMTP *session)
{
	return session->timeout;
}

void
smtpSetHelo(SMTP *session, const char *helo)
{
	if (helo == NULL || *helo == '\0') {
		if (gethostname(session->helo, sizeof (session->helo)) == 0)
			return;
		helo = "[127.0.0.1]";
	}

	TextCopy(session->helo, sizeof (session->helo), (char *) helo);
}

int
smtpSetSmartHost(SMTP *session, const char *smart_host)
{
	char *copy = NULL;

	if (smart_host != NULL && *smart_host != '\0' && (copy = strdup(smart_host)) != NULL) {
		free(session->smart_host);
		session->smart_host = copy;
		return 0;
	}

	return -1;
}

int
smtpOpen(SMTP *session, const char *mail)
{
	struct tm gmt;
	const char *error;

	if (session == NULL) {
		errno = EFAULT;
		return session->smtp_error = SMTP_ERROR_NULL;
	}

	if (*this_host == '\0')
		networkGetMyDetails(this_host, this_addr);

	session->head = NULL;
	session->id[0] = '\0';

	if (mail == NULL)
		mail = "<>";

	if ((error = parsePath(mail, STRICT_LITERAL_PLUS, 1, &session->mail)) != NULL) {
		if (smtp_log)
			syslog(LOG_ERR, TAG_FORMAT "mail=%s %s", TAG_ARGS, mail, error+6);
		return session->smtp_error = SMTP_ERROR_ADDRESS;
	}

	if (*session->helo == '\0')
		smtpSetHelo(session, NULL);

	if (session->timeout <= 0)
		smtpSetTimeout(session, SMTP_COMMAND_TIMEOUT);

	(void) time(&session->message_date);
	gmtime_r(&session->message_date, &gmt);
	session->id[0] = base62[gmt.tm_year % 62];
	session->id[1] = base62[gmt.tm_mon];
	session->id[2] = base62[gmt.tm_mday];
	session->id[3] = base62[gmt.tm_hour];
	session->id[4] = base62[gmt.tm_min];
	session->id[5] = base62[gmt.tm_sec];

	snprintf(session->id+6, sizeof (session->id)-6, "%05d%04lx", getpid(), ((unsigned long) session >> 4) & 0xffff);

	SYSLOG(LOG_INFO, TAG_FORMAT "mail=<%s> ok", TAG_ARGS, session->mail->address.string);

	return session->smtp_error = SMTP_ERROR_OK;
}

void
smtpClose(SMTP *session)
{
	int count, some_sent;
	struct smtp_recipient *rcpt, *rcpt_next;
	struct smtp_connection *relay, *relay_next;

	if (session != NULL) {
		count = some_sent = 0;

		for (relay = session->head; relay != NULL; relay = relay_next, count++) {
			relay_next = relay->next;

			if (!(relay->smtp_error & SMTP_ERROR_IO_MASK) && relay->data_start) {
				/* Take care with the Cisco PIX "fixup smtp" bug that
				 * causes mail delivery problems when "." and CRLF
				 * arrive in separate packets.
				 */
				if (mxCommand(session, relay, ".\r\n", 250) == 0)
					some_sent++;
			}

			if (!(relay->smtp_error & SMTP_ERROR_IO_MASK))
				(void) mxCommand(session, relay, "QUIT\r\n", 221);

			for (rcpt = relay->head; rcpt != NULL; rcpt = rcpt_next) {
				rcpt_next = rcpt->next;
				free(rcpt->rcpt);
				free(rcpt);
			}

			socketSetLinger(relay->mx, 0);
			socketClose(relay->mx);
			free(relay);
		}

		SYSLOG(LOG_DEBUG, TAG_FORMAT "closed count=%d some_sent=%d", TAG_ARGS, count, some_sent);
		free(session->smart_host);
		free(session->mail);

		if (smtp_log && 0 < count) {
			if (some_sent <= 0)
				syslog(LOG_INFO, TAG_FORMAT "message rejected by all", TAG_ARGS);
			else if (some_sent < count)
				syslog(LOG_INFO, TAG_FORMAT "message sent to some, but not all", TAG_ARGS);
			else
				syslog(LOG_INFO, TAG_FORMAT "message sent", TAG_ARGS);
		}
	}
}

int
smtpAddRcpt(SMTP *session, const char *rcpt)
{
	const char *error;
	struct smtp_recipient *recip;
	struct smtp_connection *relay;

	if (session == NULL || rcpt == NULL) {
		session->smtp_error = SMTP_ERROR_NULL;
		errno = EFAULT;
		goto error1;
	}

	if ((recip = calloc(1, sizeof (*recip))) == NULL) {
		session->smtp_error = SMTP_ERROR_MEMORY;
		goto error1;
	}

	if ((error = parsePath(rcpt, STRICT_LITERAL_PLUS, 0, &recip->rcpt)) != NULL) {
		session->smtp_error = SMTP_ERROR_ADDRESS;
		goto error2;
	}

	if (session->smart_host != NULL && session->head != NULL) {
		relay = session->head;
	} else {
		/* Find an open connection for recipient's domain. */
		for (relay = session->head; relay != NULL; relay = relay->next) {
			if (TextInsensitiveCompare(relay->domain, recip->rcpt->domain.string) == 0)
				break;
		}
	}

	if (relay == NULL) {
		/* Open new connection for recipient's domain. */
		if ((relay = calloc(1, sizeof (*relay))) == NULL) {
			session->smtp_error = SMTP_ERROR_MEMORY;
			goto error3;
		}

		if (session->smart_host == NULL) {
			if (recip->rcpt->domain.length <= 0) {
				/* Recipient address requires a domain. */
				session->smtp_error = SMTP_ERROR_ADDRESS;
				goto error4;
			}

			/* This is used for error reporting in mxCommand(). */
			relay->domain = recip->rcpt->domain.string;

			if ((relay->mx = mxConnect(session, recip->rcpt->domain.string)) == NULL) {
				session->smtp_error = SMTP_ERROR_CONNECT;
				goto error4;
			}
		} else if (socketOpenClient(session->smart_host, SMTP_PORT, SMTP_CONNECT_TIMEOUT, NULL, &relay->mx)) {
			session->smtp_error = relay->smtp_error = SMTP_ERROR_CONNECT;
			goto error4;
		} else {
			/* Use the smart host for error reporting in mxCommand(). */
			relay->domain = session->smart_host;
		}

		(void) socketSetNagle(relay->mx, 0);
		(void) socketSetNonBlocking(relay->mx, 1);
		socketSetTimeout(relay->mx, session->timeout);

		if (mxCommand(session, relay, NULL, 220))
			goto error5;

		snprintf(session->line, sizeof (session->line), "HELO %s\r\n", session->helo);
		if (mxCommand(session, relay, session->line, 250))
			goto error5;

		snprintf(session->line, sizeof (session->line), "MAIL FROM:<%s>\r\n", session->mail->address.string);
		if (mxCommand(session, relay, session->line, 250))
			goto error5;

		/* Add relay to list. */
		relay->next = session->head;
		session->head = relay;
	}

	snprintf(session->line, sizeof (session->line), "RCPT TO:<%s>\r\n", recip->rcpt->address.string);
	if (mxCommand(session, relay, session->line, 250)) {
                if (relay->smtp_error == SMTP_ERROR_REJECT)
                        session->smtp_error = SMTP_ERROR_RCPT;
		goto error3;
	}

	if (smtp_log)
		syslog(LOG_INFO, TAG_FORMAT "rcpt=<%s> ok", TAG_ARGS, recip->rcpt->address.string);

	/* Add recipient to list. */
	recip->next = relay->head;
	relay->head = recip;

	return SMTP_ERROR_OK;
error5:
	socketClose(relay->mx);
error4:
	free(relay);
error3:
	free(recip->rcpt);
error2:
	free(recip);
error1:
	if (smtp_log)
		syslog(LOG_ERR, TAG_FORMAT "rcpt=%s %s", TAG_ARGS, rcpt, smtpGetError(session->smtp_error));

	return session->smtp_error;
}

int
smtpPrint(SMTP *session, const char *line, size_t length)
{
	struct smtp_connection *relay;
	int count, some_sent, need_crlf;

	if (session == NULL || line == NULL) {
		errno = EFAULT;
		return -1;
	}

	/* Handle SMTP dot transparency by copying to a working buffer. */
	if (0 < length && line[0] == '.') {
		if (sizeof (session->text)-2 <= length)
			length = sizeof (session->text)-2;

		memcpy(session->text+1, line, length);
		session->text[++length] = '\0';
		session->text[0] = '.';
		line = session->text;
	}

	need_crlf = ! (1 < length && line[length-1] == '\n' && line[length-2] == '\r');

	count = some_sent = 0;

	for (relay = session->head; relay != NULL; relay = relay->next, count++) {
		if ((relay->smtp_error & SMTP_ERROR_IO_MASK))
			continue;

		if (!relay->data_start) {
			relay->data_start = 1;
			if (mxCommand(session, relay, "DATA\r\n", 354))
				continue;
		}

		if (mxPrint(session, relay, line, length))
			continue;

		if (need_crlf && mxPrint(session, relay, "\r\n", sizeof ("\r\n")-1))
			continue;

		some_sent++;
	}

	return some_sent == 0 ? -1 : 0;
}

int
smtpPrintfV(SMTP *session, const char *fmt, va_list args)
{
	int length;

	if (session == NULL || fmt == NULL) {
		errno = EFAULT;
		return -1;
	}

	length = vsnprintf(session->text, sizeof (session->text)-3, fmt, args);
	length = smtpAssertCRLF(session->text, length, sizeof (session->text));

	return smtpPrint(session, session->text, length);
}

int
smtpPrintf(SMTP *session, const char *fmt, ...)
{
	int error;
	va_list args;

	va_start(args, fmt);
	error = smtpPrintfV(session, fmt, args);
	va_end(args);

	return error;
}

#ifdef TEST

#include <stdio.h>
#include <com/snert/lib/util/getopt.h>

static char usage[] =
"usage: smtp [-v][-h helo][-s host][-t timeout] mail rcpt... < message\n"
"\n"
"-h helo\t\tHELO argument to be used.\n"
"-s host\t\ta smart host[,port] address to send through\n"
"-t timeout\tsocket timeout in seconds, default 60\n"
"-v\t\tverbose logging to maillog\n"
"\n"
LIBSNERT_COPYRIGHT "\n"
;

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

char text[SMTP_TEXT_LINE_LENGTH+1];

int
main(int argc, char **argv)
{
	int ch, i;
	SMTP session;
	size_t length;

	openlog("smtp-cli", LOG_PID, LOG_MAIL);

	memset(&session, 0, sizeof (session));
#ifdef SET_DEFAULT_SMART_HOST
	smtpSetSmartHost(&session, "127.0.0.1");
#endif

	while ((ch = getopt(argc, argv, "h:s:t:v")) != -1) {
		switch (ch) {
		case 'h':
			smtpSetHelo(&session, optarg);
			break;
		case 's':
			smtpSetSmartHost(&session, optarg);
			break;
		case 't':
			smtpSetTimeout(&session, strtol(optarg, NULL, 10) * 1000);
			break;
		case 'v':
			LogSetProgramName("smtp-cli");
			LogOpen("(standard error)");
			socketSetDebug(1);
			smtpSetDebug(1);
			break;
		default:
			(void) fprintf(stderr, usage);
			exit(64);
		}
	}

	if (argc < optind + 2) {
		(void) fprintf(stderr, usage);
		exit(64);
	}

	if (socketInit()) {
		syslog(LOG_ERR, "socketInit() %s (%d)", strerror(errno), errno);
		exit(71);
	}

	if (smtpOpen(&session, argv[optind++]))
		exit(1);

	for (i = optind; i < argc; i++) {
		if (smtpAddRcpt(&session, argv[i]))
			exit(1);
	}

	while (fgets(text, sizeof (text)-2, stdin) != NULL) {
		length = smtpAssertCRLF(text, strlen(text), sizeof (text));
		if (smtpPrint(&session, text, length))
			break;
	}

	smtpClose(&session);

	return 0;
}
#endif /* TEST */
