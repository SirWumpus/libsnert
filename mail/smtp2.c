/*
 * smtp2.c
 *
 * A simple SMTP engine.
 *
 * Copyright 2007 by Anthony Howe. All rights reserved.
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

#ifndef __MINGW32__
# if defined(HAVE_NETDB_H)
#  include <netdb.h>
# endif
# if defined(HAVE_SYSLOG_H)
#  include <syslog.h>
# endif
#endif

#ifdef HAVE_UNISTD_H
# ifdef __linux__
#  /* See Linux man setresgid */
#  define _GNU_SOURCE
# endif
# include <unistd.h>
#endif

#ifdef ENABLE_PDQ
# include <com/snert/lib/net/pdq.h>
#else
# include <com/snert/lib/io/Dns.h>
#endif
#include <com/snert/lib/io/Log.h>
#include <com/snert/lib/io/file.h>
#include <com/snert/lib/sys/Time.h>
#include <com/snert/lib/net/network.h>
#include <com/snert/lib/mail/smtp2.h>
#include <com/snert/lib/util/Text.h>

/***********************************************************************
 ***
 ***********************************************************************/

#define LOG_FMT		"%s "
#define LOG_ARG(s)	(s)->id_string

/***********************************************************************
 ***
 ***********************************************************************/

#ifdef HAVE_RAND_R
#define RANDOM_NUMBER(max)	((int)((double)(max) * (double) rand_r(&rand_seed) / (RAND_MAX+1.0)))
#else
#define RANDOM_NUMBER(max)	((int)((double)(max) * (double) rand() / (RAND_MAX+1.0)))
#endif

#ifndef RAND_MSG_COUNT
#define RAND_MSG_COUNT		RANDOM_NUMBER(62.0*62.0)
#endif

unsigned int rand_seed;

/***********************************************************************
 *** Session / Message ID
 ***********************************************************************/

#define TIME_CYCLE		60

static const char base62[] = "0123456789ABCDEFGHIJKLMNOPQRSYUVWXYZabcdefghijklmnopqrsyuvwxyz";

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
 * Set the next message-id.
 *
 * The message-id is composed of
 *
 *	ymd HMS ppppp sssss cc
 */
static void
next_msg_id(SMTP2 *smtp, char buffer[20])
{
	if (62 * 62 <= ++smtp->count) {
		(void) time(&smtp->start);
		smtp->count = 1;
	}

	time_encode(smtp->start, buffer);
	snprintf(buffer+6, 20-6, "%05u%05u", getpid(), smtp->id);

	buffer[16] = base62[smtp->count / 62];
	buffer[17] = base62[smtp->count % 62];
	buffer[18] = '\0';
}

/*
 * Resets the message-id to a session-id.
 */
static void
reset_msg_id(SMTP2 *smtp)
{
	smtp->id_string[16] = '0';
	smtp->id_string[17] = '0';
	smtp->id_string[18] = '\0';
}

/***********************************************************************
 *** SMTP Protocol API (multiple recipients, same destination)
 ***********************************************************************/

size_t
smtp2AssertCRLF(char *line, size_t length, size_t size)
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

/**
 * @param s
 *	A Socket2 pointer returned by socketOpen() or socketAccept().
 *
 * @param lines
 *	Used to pass back to the caller an array of pointers to C strings.
 *	The array is always terminated by a NULL pointer. Its the caller's
 *	responsibility to free() this array when done, which will also
 *	free its contents. On success there is at least one line.
 *
 * @return
 *	An SMTP reply code.
 */
int
smtp2Read(Socket2 *s, char ***lines)
{
	int rc, ch;
	long length;
	char *buffer, **replace;
	size_t size, offset, line_no, line_max;

	rc = SMTP_ERROR;

	if (s == NULL || lines == NULL) {
		errno = EFAULT;
		goto error0;
	}

	size = 0;
	offset = 0;
	line_no = 0;
	line_max = 0;
	*lines = NULL;

	do {
		if (line_max <= line_no || size <= offset) {
			if ((replace = realloc(*lines, sizeof (char *) * (line_max + 11) + offset + SMTP_REPLY_LINE_LENGTH)) == NULL)
				goto error1;

			size += SMTP_REPLY_LINE_LENGTH;
			line_max += 10;
			*lines = replace;
			(*lines)[line_no] = NULL;
		}

		buffer = (char *) &(*lines)[line_max + 1];
		(*lines)[line_no++] = buffer + offset;
		(*lines)[line_no] = NULL;

		switch (length = socketReadLine(s, buffer+offset, size-offset)) {
		case SOCKET_EOF:
			rc = SMTP_ERROR_EOF;
			errno = ENOTCONN;
			/*@fallthrough@*/

		case SOCKET_ERROR:
			goto error1;

		default:
			/* Did we read sufficient characters for a response code? */
			if (length < 4) {
				rc = SMTP_ERROR_IO;
				errno = EIO;
				goto error1;
			}

			ch = buffer[offset + 3];

			if (!isdigit(buffer[offset])
			&&  !isdigit(buffer[offset+1])
			&&  !isdigit(buffer[offset+2])
			&&  !isspace(ch) && ch != '-') {
				rc = SMTP_ERROR_IO;
				errno = EIO;
				goto error1;
			}
		}

		offset += length + 1;
	} while (ch == '-');

	if (0 < line_no)
		return strtol((*lines)[0], NULL, 10);
error1:
	free(*lines);
	*lines = NULL;
error0:
	return rc;
}

/**
 * @param s
 *	A Socket2 pointer returned by socketOpen() or socketAccept().
 *
 * @param line
 *	A pointer to a buffer to be write to the socket.
 *
 * @param size
 *	The length of the line buffer.
 *
 * @return
 *	Zero (0) on success or an SMTP_ERROR_ code.
 */
int
smtp2Write(Socket2 *s, char *line, size_t size)
{
	if (socketWrite(s, (unsigned char *) line, size) == SOCKET_ERROR)
		return SMTP_ERROR_IO;

#ifdef IS_THIS_NECESSARY
	/* Now wait for the output to be sent to the SMTP server. */
	if (!socketCanSend(s, socketGetTimeout(s)))
		return SMTP_ERROR_TIMEOUT;
#endif
	return SMTP_OK;
}

static int
mxPrint(SMTP2 *smtp, const char *line, size_t length)
{
	int rc;

	/* Have we already had an IO error? */
	if (smtp->flags & SMTP_FLAG_ERROR)
		return SMTP_ERROR_IO;

	if (smtp->flags & SMTP_FLAG_DEBUG)
		syslog(LOG_DEBUG, LOG_FMT ">> %lu:%s", LOG_ARG(smtp), (unsigned long) length, line);

	if ((rc = smtp2Write(smtp->mx, (char *) line, length)) != SMTP_OK) {
		if (smtp->flags & SMTP_FLAG_LOG)
			syslog(LOG_ERR, LOG_FMT "I/O error: %s (%d)", LOG_ARG(smtp), strerror(errno), errno);
		smtp->flags |= SMTP_FLAG_ERROR;
	}

	return rc;
}

static int
mxResponse(SMTP2 *smtp)
{
	int rc;
	char **lines, **ln;

	/* Have we already had an IO error? */
	if (smtp->flags & SMTP_FLAG_ERROR)
		return SMTP_ERROR_IO;

	rc = smtp2Read(smtp->mx, &lines);

	if (SMTP_IS_ERROR(rc)) {
		if (smtp->flags & SMTP_FLAG_LOG)
			syslog(LOG_ERR, LOG_FMT "I/O error: %s (%d)", LOG_ARG(smtp), strerror(errno), errno);
		smtp->flags |= SMTP_FLAG_ERROR;
	} else {
		if (smtp->flags & SMTP_FLAG_DEBUG) {
			for (ln = lines ; *ln != NULL; ln++)
				syslog(LOG_DEBUG, LOG_FMT "<< %s", LOG_ARG(smtp), *ln);
		}

		free(lines);
	}

	return rc;
}

static int
mxCommand(SMTP2 *smtp, const char *line)
{
	int rc;

	if (line != NULL) {
		rc = mxPrint(smtp, line, strlen(line));
		if (SMTP_IS_ERROR(rc))
			goto error0;
	}

	rc = mxResponse(smtp);
error0:
	return smtp->code = rc;
}

static int
smtp2Start(SMTP2 *smtp)
{
	int rc;
	socklen_t namelen;
	SocketAddress name;

	socketSetTimeout(smtp->mx, smtp->command_to);

	if (smtp->mx == NULL)
		return SMTP_ERROR;

	if ((rc = mxCommand(smtp, NULL)) != SMTP_WELCOME)
		return rc;

	namelen = sizeof (name);
	if (getsockname(smtp->mx->fd, (struct sockaddr *) &name, &namelen))
		return SMTP_ERROR;

	socketAddressGetString(&name, 0, smtp->local_ip, sizeof (smtp->local_ip));
	(void) snprintf(smtp->text, sizeof (smtp->text), "EHLO [%s]\r\n", smtp->local_ip);
	if ((rc = mxCommand(smtp, smtp->text)) != SMTP_OK) {
		(void) snprintf(smtp->text, sizeof (smtp->text), "HELO [%s]\r\n", smtp->local_ip);
		rc = mxCommand(smtp, smtp->text);
	}

	return rc;
}

static int
smtp2Connect(SMTP2 *smtp, const char *host)
{
	if (socketOpenClient(host, SMTP_PORT, smtp->connect_to, NULL, &smtp->mx) != 0)
		return SMTP_ERROR;
#ifdef __unix__
	(void) fileSetCloseOnExec(socketGetFd(smtp->mx), 1);
#endif
	if (smtp->flags & SMTP_FLAG_LOG)
		syslog(LOG_INFO, LOG_FMT "connected host=%s", LOG_ARG(smtp), host);

	return smtp2Start(smtp);
}

#ifdef ENABLE_PDQ
static int
smtp2ConnectMx(SMTP2 *smtp, const char *domain)
{
	int rc;
	PDQ_rr *list, *rr;

	list = pdqFetchMX(PDQ_CLASS_IN, domain, IS_IP_RESTRICTED|IS_IP_LAN);

	if (list == NULL && (smtp->flags & SMTP_FLAG_DEBUG))
		syslog(LOG_DEBUG, LOG_FMT "domain=%s has no acceptable MX", LOG_ARG(smtp), domain);

	rc = SMTP_ERROR_CONNECT;

	/* Try all MX of a lower preference until one answers. */
	for (rr = list; rr != NULL; rr = rr->next) {
		if (rr->rcode == PDQ_RCODE_OK
		&& smtp2Connect(smtp, ((PDQ_MX *) rr)->host.string.value) == SMTP_OK) {
			if ((smtp->domain = strdup(domain)) == NULL) {
				socketClose(smtp->mx);
				rc = SMTP_ERROR;
				break;
			}

			if (smtp->flags & SMTP_FLAG_DEBUG)
				syslog(LOG_DEBUG, LOG_FMT "connected MX %d %s", LOG_ARG(smtp), ((PDQ_MX *) rr)->preference, ((PDQ_MX *) rr)->host.string.value);
			rc = SMTP_OK;
			break;
		}
	}

	pdqFree(list);

	return rc;
}
#else
static int
smtp2ConnectMx(SMTP2 *smtp, const char *domain)
{
	long i;
	DnsEntry *mx;
	Vector mxlist;
	const char *error;
	int rc, preference;

	mx = NULL;
	preference = 65535;

	if (DnsGet2(DNS_TYPE_MX, 1, domain, &mxlist, &error) != DNS_RCODE_OK) {
		if (smtp->flags & SMTP_FLAG_LOG)
			syslog(LOG_ERR, LOG_FMT "domain=%s %s", LOG_ARG(smtp), domain, error);
		return SMTP_ERROR;
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
			syslog(LOG_DEBUG, LOG_FMT "removed MX %d %s [%s]", LOG_ARG(smtp), mx->preference, (char *) mx->value, mx->address_string);
			VectorRemove(mxlist, i--);
			continue;
		}

#ifdef HMMM
		/* Look for our IP address to find our preference value,
		 * unless we are the lowest/first MX in the list.
		 */
		if (TextInsensitiveCompare(mx->address_string, this_addr) == 0)
			preference = mx->preference + (i == 0);
#endif
	}

	if (VectorLength(mxlist) <= 0 && (smtp->flags & SMTP_FLAG_DEBUG))
		syslog(LOG_DEBUG, LOG_FMT "domain=%s has no acceptable MX", LOG_ARG(smtp), domain);

	rc = SMTP_ERROR_CONNECT;

	/* Try all MX of a lower preference until one answers. */
	for (i = 0; i < VectorLength(mxlist); i++) {
		if ((mx = VectorGet(mxlist, i)) == NULL)
			continue;

		if (preference <= mx->preference)
			continue;

		if (smtp2Connect(smtp, mx->value) == SMTP_OK) {
			if ((smtp->domain = strdup(domain)) == NULL) {
				socketClose(smtp->mx);
				rc = SMTP_ERROR;
				break;
			}

			if (smtp->flags & SMTP_FLAG_DEBUG)
				syslog(LOG_DEBUG, LOG_FMT "connected MX %d %s", LOG_ARG(smtp), mx->preference, (char *) mx->value);
			rc = SMTP_OK;
			break;
		}
	}

	VectorDestroy(mxlist);

	return rc;
}
#endif

static SMTP2 *
smtp2Create(unsigned connect_ms, unsigned command_ms, int flags)
{
	SMTP2 *smtp;

	if (socketInit())
		return NULL;

	if (rand_seed == 0)
		srand(rand_seed = time(NULL));

	if ((smtp = malloc(sizeof (*smtp))) != NULL) {
		smtp->mx = NULL;
		smtp->count = 0;
		smtp->flags = flags;
		smtp->domain = NULL;
		smtp->sender = NULL;
		smtp->connect_to = connect_ms;
		smtp->command_to = command_ms;
		smtp->id = (unsigned short) RAND_MSG_COUNT;

		/* The smtp-id is a message-id with cc=00, is composed of
		 *
		 *	ymd HMS ppppp sssss cc
		 *
		 * Since the value of sssss can roll over very quuickly on
		 * some systems, incorporating timestamp and process info
		 * in the smtp-id should facilitate log searches.
		 */

		(void) time(&smtp->start);
		time_encode(smtp->start, smtp->id_string);
		snprintf(smtp->id_string+6, 20-6, "%05u%05u00", getpid(), smtp->id);
	}

	return smtp;
}

void
smtp2Close(void *_smtp)
{
	if (_smtp != NULL) {
		if (!(((SMTP2 *) _smtp)->flags & SMTP_FLAG_DATA))
			(void) mxCommand(_smtp, "QUIT\r\n");
		socketClose(((SMTP2 *) _smtp)->mx);
		free(((SMTP2 *) _smtp)->domain);
		free(((SMTP2 *) _smtp)->sender);
		free(_smtp);
	}
}

SMTP2 *
smtp2Open(const char *host, unsigned connect_ms, unsigned command_ms, int flags)
{
	SMTP2 *smtp;

	if ((smtp = smtp2Create(connect_ms, command_ms, flags)) != NULL) {
		if (smtp2Connect(smtp, host) != SMTP_OK || (smtp->domain = strdup(host)) == NULL) {
			smtp2Close(smtp);
			smtp = NULL;
		}
	}

	return smtp;
}

SMTP2 *
smtp2OpenMx(const char *domain, unsigned connect_ms, unsigned command_ms, int flags)
{
	SMTP2 *smtp;

	if ((smtp = smtp2Create(connect_ms, command_ms, flags)) != NULL) {
		if (smtp2ConnectMx(smtp, domain) != SMTP_OK) {
			smtp2Close(smtp);
			smtp = NULL;
		}
	}

	return smtp;
}

int
smtp2Mail(SMTP2 *smtp, const char *sender)
{
	next_msg_id(smtp, smtp->id_string);

	if (sender == NULL) {
		(void) snprintf(smtp->text, sizeof (smtp->text), "postmaster@[%s]", smtp->local_ip);
		sender = smtp->text;
	}

	/* Save the sender for when we need to add a From; header if required. */
	if (smtp->sender == NULL && (smtp->sender = strdup(sender)) == NULL)
		return SMTP_ERROR;

	(void) snprintf(smtp->text, sizeof (smtp->text), "MAIL FROM:<%s>\r\n", smtp->sender);
	return mxCommand(smtp, smtp->text);
}

int
smtp2Rcpt(SMTP2 *smtp, const char *recipient)
{
	(void) snprintf(smtp->text, sizeof (smtp->text), "RCPT TO:<%s>\r\n", recipient);
	return mxCommand(smtp, smtp->text);
}

int
smtp2Data(SMTP2 *smtp)
{
	int rc;

	if ((rc = mxCommand(smtp, "DATA\r\n")) == SMTP_WAITING)
		smtp->flags |= SMTP_FLAG_DATA;

	return rc;
}

int
smtp2Dot(SMTP2 *smtp)
{
	int rc;

	if (smtp->flags & SMTP_FLAG_DATA) {
		smtp->flags &= ~SMTP_FLAG_DATA;
		rc = mxCommand(smtp, ".\r\n");
		reset_msg_id(smtp);
		return rc;
	}

	return smtp2Rset(smtp);
}

int
smtp2Noop(SMTP2 *smtp)
{
	return mxCommand(smtp, "NOOP\r\n");
}

int
smtp2Rset(SMTP2 *smtp)
{
	reset_msg_id(smtp);
	free(smtp->sender);
	smtp->sender = NULL;
	smtp->flags &= (SMTP_FLAG_LOG|SMTP_FLAG_DEBUG|SMTP_FLAG_TRY_ALL);
	return mxCommand(smtp, "RSET\r\n");
}

int
smtp2Print(SMTP2 *smtp, const char *line, size_t length)
{
	int rc;

	/* Handle SMTP dot transparency by copying to a working buffer. */
	if (0 < length && line[0] == '.') {
		if (sizeof (smtp->text)-2 <= length)
			length = sizeof (smtp->text)-2;

		memcpy(smtp->text+1, line, length);
		smtp->text[++length] = '\0';
		smtp->text[0] = '.';
		line = smtp->text;
	}

	if (!(smtp->flags & SMTP_FLAG_DATA)) {
		if ((rc = mxCommand(smtp, "DATA\r\n")) != SMTP_WAITING)
			return rc;

		smtp->flags |= SMTP_FLAG_DATA;
	}

	/* Check for required headers. */
	if (!(smtp->flags & SMTP_FLAG_EOH)) {
		if (line[0] == '\r' && line[1] == '\n') {
			/* End-of-headers. Add any missing required headers. */
			smtp->flags |= SMTP_FLAG_EOH;

			if (!(smtp->flags & SMTP_FLAG_DATE)) {
				char timestamp[40];
				TimeStamp(&smtp->start, timestamp, sizeof (timestamp));
				(void) smtp2Printf(smtp, "Date: %s\r\n", timestamp);
			}

			if (!(smtp->flags & SMTP_FLAG_FROM))
				(void) smtp2Printf(smtp, "From: <%s>\r\n", smtp->sender);

			if (!(smtp->flags & SMTP_FLAG_SUBJECT))
				(void) mxPrint(smtp, "Subject: \r\n", sizeof ("Subject: \r\n")-1);

			if (!(smtp->flags & SMTP_FLAG_MSGID))
				(void) smtp2Printf(smtp, "Message-ID: <%s@[%s]>\r\n", smtp->id_string, smtp->local_ip);
		} else if (!(smtp->flags & SMTP_FLAG_SUBJECT) && TextMatch(line, "Subject:*", length, 1)) {
			smtp->flags |= SMTP_FLAG_SUBJECT;
		} else if (!(smtp->flags & SMTP_FLAG_FROM) && TextMatch(line, "From:*", length, 1)) {
			smtp->flags |= SMTP_FLAG_FROM;
		} else if (!(smtp->flags & SMTP_FLAG_DATE) && TextMatch(line, "Date:*", length, 1)) {
			smtp->flags |= SMTP_FLAG_DATE;
		} else if (!(smtp->flags & SMTP_FLAG_MSGID) && TextMatch(line, "Message-ID:*", length, 1)) {
			smtp->flags |= SMTP_FLAG_MSGID;
		}
	}

	return mxPrint(smtp, line, length);
}

int
smtp2PrintfV(SMTP2 *smtp, const char *fmt, va_list args)
{
	int length;

	length = vsnprintf(smtp->text, sizeof (smtp->text)-3, fmt, args);

	return smtp2Print(smtp, smtp->text, length);
}

int
smtp2Printf(SMTP2 *smtp, const char *fmt, ...)
{
	int rc;
	va_list args;

	va_start(args, fmt);
	rc = smtp2PrintfV(smtp, fmt, args);
	va_end(args);

	return rc;
}

/***********************************************************************
 *** Mail Message API (multiple recipients, multiple destinations)
 ***********************************************************************/

/*
 * @param fn
 *	smtp2 function to call for each SMTP session.
 *
 * @param expect
 *	The SMTP response code expected from all SMTP sessions.
 *
 * @return
 *	SMTP_OK if all sessions reported success. Otherwise SMTP_ERROR.
 *	When a session fails, then the caller must walk the SMTP session
 *	list checking individual response codes.
 */
static int
mailFunction(Mail *mail, int (*fn)(SMTP2 *), int expect)
{
	int count, errors;
	SMTP2 *smtp, *next;

	for (count = errors = 0, smtp = mail->list; smtp != NULL; smtp = next, count++) {
		next = smtp->next;
		if ((*fn)(smtp) != expect)
			errors++;
	}

	/* Do we continue on partial success? */
	if ((mail->flags & SMTP_FLAG_TRY_ALL) && errors < count)
		return expect;

	/* Either complete success or partial failure. */
	return errors == 0 ? expect : SMTP_ERROR;
}

Mail *
mailOpen(unsigned connect_ms, unsigned command_ms, int flags)
{
	Mail *mail;

	if ((mail = calloc(1, sizeof (*mail))) != NULL) {
		mail->flags = flags;
		mail->connect_to = connect_ms;
		mail->command_to = command_ms;
	}

	return mail;
}

static int
smtp2Close2(SMTP2 *smtp)
{
	smtp2Close(smtp);
	return SMTP_GOODBYE;
}

void
mailClose(void *_mail)
{
	if (_mail != NULL) {
		(void) mailFunction(_mail, smtp2Close2, SMTP_GOODBYE);
		free(((Mail *) _mail)->sender);
		free(_mail);
	}
}

int
mailMail(Mail *mail, const char *sender)
{
	SMTP2 *smtp;
	int rc = SMTP_OK;

	/* Reset any active sessions. */
	mailRset(mail);

	/* Save the sender for creating new sessions based on recipient. */
	if (sender != NULL && (mail->sender = strdup(sender)) == NULL)
		return SMTP_ERROR;

	/* Start a new transaction when we have any active sessions. */
	if (mail->list != NULL) {
		for (smtp = mail->list; smtp != NULL; smtp = smtp->next) {
			if (smtp2Mail(smtp, mail->sender) != SMTP_OK)
				rc = SMTP_ERROR;
		}
	}

	return rc;
}

int
mailRcpt(Mail *mail, const char *recipient)
{
	SMTP2 *smtp;
	char *domain;

	if (recipient == NULL || (domain = strchr(recipient, '@')) == NULL)
		return SMTP_ERROR;
	domain++;

	/* Look for an already open connection for this domain. */
	for (smtp = mail->list; smtp != NULL; smtp = smtp->next) {
		if (TextInsensitiveCompare(smtp->domain, domain) == 0)
			return smtp2Rcpt(smtp, recipient);
	}

	/* Open a new connection for this recipient's domain. */
	if ((smtp = smtp2OpenMx(domain, mail->connect_to, mail->command_to, mail->flags)) == NULL)
		return SMTP_ERROR_CONNECT;

	/* Start the first transaction. */
	if (smtp2Mail(smtp, mail->sender) != SMTP_OK) {
		smtp2Close(smtp);
		return SMTP_ERROR;
	}

	/* Add to the link list of active sessions. */
	smtp->next = mail->list;
	mail->list = smtp;

	/* Then send the recipient for this transaction. */
	return smtp2Rcpt(smtp, recipient);
}

int
mailData(Mail *mail)
{
	return mailFunction(mail, smtp2Data, SMTP_WAITING);
}

int
mailDot(Mail *mail)
{
	return mailFunction(mail, smtp2Dot, SMTP_OK);
}

int
mailNoop(Mail *mail)
{
	return mailFunction(mail, smtp2Noop, SMTP_OK);
}

int
mailRset(Mail *mail)
{
	free(mail->sender);
	mail->sender = NULL;
	return mailFunction(mail, smtp2Rset, SMTP_OK);
}

int
mailPrint(Mail *mail, const char *line, size_t length)
{
	SMTP2 *smtp;
	int rc = SMTP_ERROR;

	for (smtp = mail->list; smtp != NULL; smtp = smtp->next)
		rc = smtp2Print(smtp, line, length);

	return rc;
}

int
mailPrintfV(Mail *mail, const char *fmt, va_list args)
{
	SMTP2 *smtp;
	int rc = SMTP_ERROR;

	for (smtp = mail->list; smtp != NULL; smtp = smtp->next)
		rc = smtp2PrintfV(smtp, fmt, args);

	return rc;
}

int
mailPrintf(Mail *mail, const char *fmt, ...)
{
	int rc;
	va_list args;

	va_start(args, fmt);
	rc = mailPrintfV(mail, fmt, args);
	va_end(args);

	return rc;
}

/***********************************************************************
 *** Test CLI
 ***********************************************************************/

#ifdef TEST

#include <com/snert/lib/mail/parsePath.h>
#include <com/snert/lib/util/getopt.h>

#ifndef SMTP_MAX_SIZEOF_HEADERS
#define SMTP_MAX_SIZEOF_HEADERS		(8 * 1024)
#endif

#define _NAME			"smtp2"

static char usage[] =
"usage: " _NAME " [-av][-f mail][-h host[:port]][-t timeout] rcpt... < message\n"
"\n"
"-a\t\ttry sending to all recipients even if some fail\n"
"-f from\t\tMAIL FROM: address\n"
"-h host[:port]\tconnect to this SMTP host\n"
"-t timeout\tSMTP command timeout in seconds, default 5 minutes\n"
"-v\t\tverbose debug messages; 1 log, 2 debug, 3 stderr\n"
"\n"
LIBSNERT_COPYRIGHT "\n"
;

Mail *message;
SMTP2 *session;
char *mail_from;
char *smart_host;
unsigned command_to = SMTP_COMMAND_TO;
char text[SMTP_TEXT_LINE_LENGTH+1];
char headers[SMTP_MAX_SIZEOF_HEADERS];

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

void
atExitCleanUp(void)
{
	smtp2Close(session);
	mailClose(message);
}

typedef int (*mail_fn)(void *, const char *);
typedef int (*rcpt_fn)(void *, const char *);
typedef int (*print_fn)(void *, const char *, size_t);
typedef int (*dot_fn)(void *);

typedef struct {
	mail_fn mail;
	rcpt_fn rcpt;
	print_fn print;
	dot_fn dot;
} CommandSet;

CommandSet mail_cmds = {
	(mail_fn) mailMail,
	(rcpt_fn) mailRcpt,
	(print_fn) mailPrint,
	(dot_fn) mailDot
};

CommandSet smtp2_cmds = {
	(mail_fn) smtp2Mail,
	(rcpt_fn) smtp2Rcpt,
	(print_fn) smtp2Print,
	(dot_fn) smtp2Dot
};

void
sendMessage(void *context, CommandSet *cmd, const char *from, int argc, char **argv)
{
	int i;
	ParsePath *sender;
	size_t length, offset;

	if (context == NULL)
		exit(1);

	do {
		offset = 0;
		sender = NULL;
		if (from == NULL) {
			/* Read in the message headers and find the original sender. */
			for (offset = 0; offset+SMTP_TEXT_LINE_LENGTH < sizeof (headers); offset += length) {
				if (fgets(headers+offset, sizeof (headers)-offset, stdin) == NULL)
					return;

				length = smtp2AssertCRLF(headers+offset, strlen(headers+offset), sizeof (headers)-offset);

				if (headers[offset] == '\r' && headers[offset] == '\n') {
					offset += length;
					break;
				}

				if (TextMatch(headers+offset, "Return-Path:*", length, 1)) {
					if (parsePath(headers+offset+sizeof ("Return-Path:")-1, 0, 1, &sender) != NULL)
						exit(1);
				} else if (TextMatch(headers+offset, "Sender:*", length, 1)) {
					if (parsePath(headers+offset+sizeof ("Sender:")-1, 0, 1, &sender) != NULL)
						exit(1);
				} else if (TextMatch(headers+offset, "From:*", length, 1)) {
					if (parsePath(headers+offset+sizeof ("From:")-1, 0, 1, &sender) != NULL)
						exit(1);
				}

				if (sender != NULL) {
					from = sender->address.string;
					offset += length;
					break;
				}
			}
		} else if ((i = fgetc(stdin)) != EOF) {
			ungetc(i, stdin);
		} else {
			break;
		}

		if ((*cmd->mail)(context, from) != SMTP_OK)
			exit(1);

		free(sender);

		for (i = 1; i < argc; i++) {
			if ((*cmd->rcpt)(context, argv[i]) != SMTP_OK)
				exit(1);
		}

		if (0 < offset) {
			if ((*cmd->print)(context, headers, offset) != SMTP_OK)
				exit(1);
		}

		while (fgets(text, sizeof (text)-2, stdin) != NULL) {
			if (text[0] == '.' && (text[1] == '\n' || text[1] == '\r'))
				break;
			length = smtp2AssertCRLF(text, strlen(text), sizeof (text));
			if ((*cmd->print)(context, text, strlen(text)) != SMTP_OK)
				exit(1);
		}

		if ((*cmd->dot)(context) != SMTP_OK)
			exit(1);

		if (sender != NULL)
			from = NULL;
	} while (!feof(stdin) && !ferror(stdin));
}

int
main(int argc, char **argv)
{
	int ch, debug = 0, flags = 0;

	openlog(_NAME, LOG_PID, LOG_MAIL);

	while ((ch = getopt(argc, argv, "af:h:t:v")) != -1) {
		switch (ch) {
		case 'a':
			flags |= SMTP_FLAG_TRY_ALL;
			break;
		case 'f':
			mail_from = optarg;
			break;
		case 'h':
			smart_host = optarg;
			break;
		case 't':
			command_to = (unsigned) strtol(optarg, NULL, 10) * 1000;
			break;
		case 'v':
			debug++;
			switch (debug) {
			case 1: flags |= SMTP_FLAG_LOG; break;
			case 2: flags |= SMTP_FLAG_DEBUG; break;
			}
			break;
		default:
			(void) fprintf(stderr, usage);
			exit(64);
		}
	}

	if (argc < optind + 1) {
		(void) fprintf(stderr, usage);
		exit(64);
	}

	if (atexit(atExitCleanUp))
		exit(1);

	if (0 < debug) {
		LogSetProgramName(_NAME);
		LogSetLevel(LOG_DEBUG);
		setlogmask(LOG_UPTO(LOG_DEBUG));
		socketSetDebug(1);

		if (2 < debug)
			LogOpen("(standard error)");
	}

	if (smart_host == NULL) {
		sendMessage(
			message = mailOpen(SMTP_CONNECT_TO, command_to, flags), &mail_cmds,
			mail_from, argc-optind+1, argv+optind-1
		);
	} else {
		sendMessage(
			session = smtp2Open(smart_host, SMTP_CONNECT_TO, command_to, flags), &smtp2_cmds,
			mail_from, argc-optind+1, argv+optind-1
		);
	}

	return 0;
}
#endif /* TEST */

/***********************************************************************
 *** END
 ***********************************************************************/
