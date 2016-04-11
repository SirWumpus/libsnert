/*
 * smtp2.c
 *
 * A simple SMTP engine.
 *
 * Copyright 2007, 2016 by Anthony Howe. All rights reserved.
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
#include <stddef.h>

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
# undef _GNU_SOURCE
# define _GNU_SOURCE
# include <unistd.h>
#endif

#include <com/snert/lib/net/pdq.h>
#include <com/snert/lib/io/Log.h>
#include <com/snert/lib/io/file.h>
#include <com/snert/lib/sys/Time.h>
#include <com/snert/lib/net/network.h>
#include <com/snert/lib/mail/smtp2.h>
#include <com/snert/lib/util/Text.h>
#include <com/snert/lib/util/b64.h>

#ifdef DEBUG_MALLOC
#include <com/snert/lib/util/DebugMalloc.h>
#endif

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
SMTP_Reply_Code
smtp2Read(Socket2 *s, char ***lines)
{
	int ch;
	long length;
	SMTP_Reply_Code rc;
	char *buffer, **table;
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

	/* Silence "may be used uninitialized in this function" warning.
	 * the do-while and the top if-block are always entered, thus
	 * ensuring that table and buffer are initialised to something
	 * useful.
	 */
	table = NULL;
	buffer = NULL;

	do {
		if (line_max <= line_no || size <= offset + SMTP_REPLY_LINE_LENGTH) {
			if ((table = realloc(*lines, sizeof (char *) * (line_max + 11) + size + SMTP_REPLY_LINE_LENGTH)) == NULL)
				goto error1;

			*lines = table;
			memmove(&table[line_max + 11], &table[line_max + 1], offset);

			line_max += 10;
			size += SMTP_REPLY_LINE_LENGTH;
			buffer = (char *) &table[line_max + 1];
		}

		/* Save only the offset of the line in the buffer, since
		 * the line pointer table and buffer might be reallocated
		 */
		table[line_no++] = (char *) offset;

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

	/* Add in the base of the buffer to each line's offset. */
	for (ch = 0; ch < line_no; ch++)
		table[ch] = buffer + (ptrdiff_t) table[ch];
	table[ch] = NULL;

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
SMTP_Reply_Code
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

static SMTP_Reply_Code
mxPrint(SMTP2 *smtp, const char *line, size_t length)
{
	SMTP_Reply_Code rc;

	/* Have we already had an IO error? */
	if (smtp->flags & SMTP_FLAG_ERROR)
		return SMTP_ERROR_IO;

	if (smtp->flags & SMTP_FLAG_DEBUG)
		syslog(LOG_DEBUG, LOG_FMT ">> %lu:%s", LOG_ARG(smtp), (unsigned long) length, line);

	if ((rc = smtp2Write(smtp->mx, (char *) line, length)) != SMTP_OK) {
		if (smtp->flags & SMTP_FLAG_INFO)
			syslog(LOG_ERR, LOG_FMT "I/O error: %s (%d)", LOG_ARG(smtp), strerror(errno), errno);
		smtp->flags |= SMTP_FLAG_ERROR;
	}

	return rc;
}

static SMTP_Reply_Code
mxResponse(SMTP2 *smtp)
{
	SMTP_Reply_Code rc;
	char **lines, **ln;

	/* Have we already had an IO error? */
	if (smtp->flags & SMTP_FLAG_ERROR)
		return SMTP_ERROR_IO;

	rc = smtp2Read(smtp->mx, &lines);

	if (SMTP_IS_ERROR(rc)) {
		if (smtp->flags & SMTP_FLAG_INFO)
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

static SMTP_Reply_Code
mxCommand(SMTP2 *smtp, const char *line)
{
	SMTP_Reply_Code rc;

	if (line != NULL) {
		rc = mxPrint(smtp, line, strlen(line));
		if (SMTP_IS_ERROR(rc))
			goto error0;
	}

	rc = mxResponse(smtp);
error0:
	return smtp->code = rc;
}

static SMTP_Reply_Code
smtp2Start(SMTP2 *smtp)
{
	SMTP_Reply_Code rc;
	socklen_t namelen;
	SocketAddress name;

	socketSetTimeout(smtp->mx, smtp->command_to);

	if (smtp->mx == NULL)
		return SMTP_ERROR;

	if (mxCommand(smtp, NULL) != SMTP_WELCOME)
		return smtp->code;

	namelen = sizeof (name);
	if (getsockname(smtp->mx->fd, (struct sockaddr *) &name, &namelen))
		return SMTP_ERROR;

	if (*smtp->helo_host == '\0')
		socketAddressGetString(&name, SOCKET_ADDRESS_WITH_BRACKETS, smtp->helo_host, sizeof (smtp->helo_host));
	(void) snprintf(smtp->text, sizeof (smtp->text), "EHLO %s\r\n", smtp->helo_host);
	if ((rc = mxCommand(smtp, smtp->text)) == SMTP_OK) {
		smtp->flags |= SMTP_FLAG_EHLO;
	} else {
		smtp->flags &= ~SMTP_FLAG_EHLO;
		(void) snprintf(smtp->text, sizeof (smtp->text), "HELO %s\r\n", smtp->helo_host);
		rc = mxCommand(smtp, smtp->text);
	}

	return rc;
}

static SMTP_Reply_Code
smtp2Connect(SMTP2 *smtp, const char *host)
{
	if (smtp->flags & SMTP_FLAG_DEBUG)
		syslog(LOG_DEBUG, LOG_FMT "connecting host=%s", LOG_ARG(smtp), host);

	if ((smtp->mx = socketConnect(host, SMTP_PORT, smtp->connect_to)) == NULL)
		return errno == ETIMEDOUT ? SMTP_ERROR_TIMEOUT : SMTP_ERROR_CONNECT;

	(void) fileSetCloseOnExec(socketGetFd(smtp->mx), 1);
	(void) socketSetNonBlocking(smtp->mx, 1);
	(void) socketSetLinger(smtp->mx, 0);

	if (smtp->flags & SMTP_FLAG_INFO)
		syslog(LOG_INFO, LOG_FMT "connected host=%s", LOG_ARG(smtp), host);

	return smtp2Start(smtp);
}

static SMTP_Reply_Code
smtp2ConnectMx(SMTP2 *smtp, const char *domain)
{
	SMTP_Reply_Code rc;
	PDQ_rr *list, *rr;

	if (smtp->flags & SMTP_FLAG_DEBUG)
		syslog(LOG_DEBUG, LOG_FMT "MX lookup domain=%s", LOG_ARG(smtp), domain);

	list = pdqFetchMX(PDQ_CLASS_IN, domain, IS_IP_RESTRICTED|IS_IP_LAN);

	if (list == NULL && (smtp->flags & SMTP_FLAG_DEBUG))
		syslog(LOG_DEBUG, LOG_FMT "domain=%s has no acceptable MX", LOG_ARG(smtp), domain);

	if (list->section == PDQ_SECTION_QUERY) {
		if (((PDQ_QUERY *)list)->rcode == PDQ_RCODE_NXDOMAIN) {
			syslog(LOG_ERR, LOG_FMT "domain=%s does not exist", LOG_ARG(smtp), domain);
			return SMTP_ERROR_NXDOMAIN;
		}
		if (((PDQ_QUERY *)list)->ancount == 0) {
			syslog(LOG_ERR, LOG_FMT "domain=%s has no MX", LOG_ARG(smtp), domain);
			return SMTP_ERROR_NOMX;
		}
	}

	rc = SMTP_ERROR_CONNECT;

	/* Try all MX of a lower preference until one answers. */
	for (rr = list; rr != NULL; rr = rr->next) {
		if (rr->section != PDQ_SECTION_ANSWER || rr->type != PDQ_TYPE_MX)
			continue;			
		if ((rc = smtp2Connect(smtp, ((PDQ_MX *) rr)->host.string.value)) == SMTP_OK) {
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

	switch (rc) {
	case SMTP_OK:
		break;
	case SMTP_ERROR_CONNECT:
		syslog(LOG_ERR, LOG_FMT "connection error MX domain=%s", LOG_ARG(smtp), domain);
		break;
	case SMTP_ERROR_TIMEOUT:
		syslog(LOG_ERR, LOG_FMT "connection timeout MX domain=%s", LOG_ARG(smtp), domain);
		break;
	default:
		syslog(LOG_ERR, LOG_FMT "error (%d) MX domain=%s", LOG_ARG(smtp), rc, domain);		
	}

	pdqListFree(list);

	return rc;
}

static SMTP2 *
smtp2Create(unsigned connect_ms, unsigned command_ms, int flags, const char *helo)
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

		(void) snprintf(smtp->helo_host, sizeof (smtp->helo_host), "%s", TextEmpty(helo));
		
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
		(void) snprintf(smtp->id_string+6, 20-6, "%05u%05u00", getpid(), smtp->id);
	}

	return smtp;
}

void
smtp2Close(void *_smtp)
{
	SMTP2 *smtp = _smtp;

	if (smtp != NULL) {
		if (!(smtp->flags & SMTP_FLAG_DATA) && smtp->mx != NULL)
			(void) mxCommand(smtp, "QUIT\r\n");
		socketClose(smtp->mx);
		free(smtp->domain);
		free(smtp->sender);
		free(smtp);
	}
}

SMTP2 *
smtp2Open(const char *host, unsigned connect_ms, unsigned command_ms, int flags, const char *helo)
{
	SMTP2 *smtp;

	if ((smtp = smtp2Create(connect_ms, command_ms, flags, helo)) != NULL) {
		if (host == NULL || *host == '\0')
			host = "127.0.0.1";

		if (smtp2Connect(smtp, host) != SMTP_OK || (smtp->domain = strdup(host)) == NULL) {
			smtp2Close(smtp);
			smtp = NULL;
		}
	}

	return smtp;
}

SMTP2 *
smtp2OpenMx(const char *domain, unsigned connect_ms, unsigned command_ms, int flags, const char *helo)
{
	SMTP2 *smtp;

	if ((smtp = smtp2Create(connect_ms, command_ms, flags, helo)) != NULL) {		
		if (smtp2ConnectMx(smtp, domain) != SMTP_OK) {
			smtp2Close(smtp);
			smtp = NULL;
		}
	}

	return smtp;
}

SMTP_Reply_Code
smtp2Auth(SMTP2 *smtp, const char *user, const char *pass)
{
	B64 b64;
	size_t auth_length;

	if (!(smtp->flags & SMTP_FLAG_EHLO))
		return SMTP_UNKNOWN_COMMAND;

	if (user == NULL)
		user = "";
	if (pass == NULL)
		pass = "";

	/* RFC 2595 section 6
	 *
	 *   message         = [authorize-id] NUL authenticate-id NUL password
	 *   authenticate-id = 1*UTF8-SAFE      ; MUST accept up to 255 octets
	 *   authorize-id    = 1*UTF8-SAFE      ; MUST accept up to 255 octets
	 *   password        = 1*UTF8-SAFE      ; MUST accept up to 255 octets
	 */
	b64Init();
	b64Reset(&b64);
	auth_length = TextCopy(smtp->text, sizeof (smtp->text), "AUTH PLAIN ");
	b64EncodeBuffer(&b64, (unsigned char *) "", 1, smtp->text, sizeof (smtp->text), &auth_length);
	b64EncodeBuffer(&b64, (unsigned char *) user, strlen(user)+1, smtp->text, sizeof (smtp->text), &auth_length);
	b64EncodeBuffer(&b64, (unsigned char *) pass, strlen(pass), smtp->text, sizeof (smtp->text), &auth_length);
	b64EncodeFinish(&b64, smtp->text, sizeof (smtp->text), &auth_length, 0);
	(void) TextCopy(smtp->text+auth_length, sizeof (smtp->text)-auth_length, "\r\n");

	return mxCommand(smtp, smtp->text);
}

SMTP_Reply_Code
smtp2Mail(SMTP2 *smtp, const char *sender)
{
	next_msg_id(smtp, smtp->id_string);

	if (sender == NULL) {
		(void) snprintf(smtp->text, sizeof (smtp->text), "postmaster@%s", smtp->helo_host);
		sender = smtp->text;
	}

	/* Save the sender for when we need to add a From; header if required. */
	if (smtp->sender == NULL && (smtp->sender = strdup(sender)) == NULL)
		return SMTP_ERROR;

	(void) snprintf(smtp->text, sizeof (smtp->text), "MAIL FROM:<%s>\r\n", smtp->sender);
	return mxCommand(smtp, smtp->text);
}

SMTP_Reply_Code
smtp2Rcpt(SMTP2 *smtp, const char *recipient)
{
	(void) snprintf(smtp->text, sizeof (smtp->text), "RCPT TO:<%s>\r\n", recipient);
	return mxCommand(smtp, smtp->text);
}

SMTP_Reply_Code
smtp2Data(SMTP2 *smtp)
{
	SMTP_Reply_Code rc;
	
	if (smtp->flags & SMTP_FLAG_DATA)
		return SMTP_WAITING;
		
	if ((rc = mxCommand(smtp, "DATA\r\n")) == SMTP_WAITING)
		smtp->flags |= SMTP_FLAG_DATA;

	return rc;
}

SMTP_Reply_Code
smtp2Dot(SMTP2 *smtp)
{
	SMTP_Reply_Code rc;

	if (smtp->flags & SMTP_FLAG_DATA) {
		smtp->flags &= ~SMTP_FLAG_DATA;
		rc = mxCommand(smtp, ".\r\n");
		reset_msg_id(smtp);
		return rc;
	}

	return smtp2Rset(smtp);
}

SMTP_Reply_Code
smtp2Noop(SMTP2 *smtp)
{
	return mxCommand(smtp, "NOOP\r\n");
}

SMTP_Reply_Code
smtp2Rset(SMTP2 *smtp)
{
	reset_msg_id(smtp);
	free(smtp->sender);
	smtp->sender = NULL;
	smtp->flags &= (SMTP_FLAG_INFO|SMTP_FLAG_DEBUG|SMTP_FLAG_TRY_ALL);
	return mxCommand(smtp, "RSET\r\n");
}

SMTP_Reply_Code
smtp2Print(SMTP2 *smtp, const char *line, size_t length)
{
	SMTP_Reply_Code rc;

	/* Handle SMTP dot transparency by copying to a working buffer. */
	if (0 < length && line[0] == '.') {
		if (sizeof (smtp->text)-2 <= length)
			length = sizeof (smtp->text)-2;

		memcpy(smtp->text+1, line, length);
		smtp->text[++length] = '\0';
		smtp->text[0] = '.';
		line = smtp->text;
	}

	if (!(smtp->flags & SMTP_FLAG_DATA) && (rc = smtp2Data(smtp)) != SMTP_WAITING)
		return rc;

	/* Check for required headers. */
	if (!(smtp->flags & SMTP_FLAG_EOH)) {
		if (line[0] == '\r' && line[1] == '\n') {
			/* End-of-headers. Add any missing required headers. */
			smtp->flags |= SMTP_FLAG_EOH;

			if (!(smtp->flags & SMTP_FLAG_DATE)) {
				char timestamp[TIME_STAMP_MIN_SIZE];
				TimeStamp(&smtp->start, timestamp, sizeof (timestamp));
				(void) smtp2Printf(smtp, "Date: %s\r\n", timestamp);
			}

			if (!(smtp->flags & SMTP_FLAG_FROM))
				(void) smtp2Printf(smtp, "From: <%s>\r\n", smtp->sender);

			if (!(smtp->flags & SMTP_FLAG_SUBJECT))
				(void) mxPrint(smtp, "Subject: \r\n", sizeof ("Subject: \r\n")-1);

			if (!(smtp->flags & SMTP_FLAG_MSGID))
				(void) smtp2Printf(smtp, "Message-ID: <%s@%s>\r\n", smtp->id_string, smtp->helo_host);
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

SMTP_Reply_Code
smtp2PrintfV(SMTP2 *smtp, const char *fmt, va_list args)
{
	int length;

	length = vsnprintf(smtp->text, sizeof (smtp->text)-3, fmt, args);

	return smtp2Print(smtp, smtp->text, length);
}

SMTP_Reply_Code
smtp2Printf(SMTP2 *smtp, const char *fmt, ...)
{
	SMTP_Reply_Code rc;
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
static SMTP_Reply_Code
mailFunction(Mail *mail, SMTP_Reply_Code (*fn)(SMTP2 *), int expect)
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
mailOpen(unsigned connect_ms, unsigned command_ms, int flags, const char *helo)
{
	Mail *mail;

	if ((mail = calloc(1, sizeof (*mail))) != NULL) {
		mail->flags = flags;
		mail->connect_to = connect_ms;
		mail->command_to = command_ms;
		(void) snprintf(mail->helo_host, sizeof (mail->helo_host), "%s", TextEmpty(helo));
	}

	return mail;
}

static SMTP_Reply_Code
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

SMTP_Reply_Code
mailMail(Mail *mail, const char *sender)
{
	SMTP2 *smtp;
	SMTP_Reply_Code rc = SMTP_OK;

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

SMTP_Reply_Code
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
	if ((smtp = smtp2OpenMx(domain, mail->connect_to, mail->command_to, mail->flags, mail->helo_host)) == NULL)
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

SMTP_Reply_Code
mailData(Mail *mail)
{
	return mailFunction(mail, smtp2Data, SMTP_WAITING);
}

SMTP_Reply_Code
mailDot(Mail *mail)
{
	return mailFunction(mail, smtp2Dot, SMTP_OK);
}

SMTP_Reply_Code
mailNoop(Mail *mail)
{
	return mailFunction(mail, smtp2Noop, SMTP_OK);
}

SMTP_Reply_Code
mailRset(Mail *mail)
{
	free(mail->sender);
	mail->sender = NULL;
	return mailFunction(mail, smtp2Rset, SMTP_OK);
}

SMTP_Reply_Code
mailPrint(Mail *mail, const char *line, size_t length)
{
	SMTP2 *smtp;
	SMTP_Reply_Code rc = SMTP_ERROR;

	for (smtp = mail->list; smtp != NULL; smtp = smtp->next)
		rc = smtp2Print(smtp, line, length);

	return rc;
}

SMTP_Reply_Code
mailPrintfV(Mail *mail, const char *fmt, va_list args)
{
	SMTP2 *smtp;
	SMTP_Reply_Code rc = SMTP_ERROR;

	for (smtp = mail->list; smtp != NULL; smtp = smtp->next) {
#ifdef HAVE_MACRO_VA_COPY
		va_list copy_args;
		va_copy(copy_args, args);
		rc = smtp2PrintfV(smtp, fmt, copy_args);
		va_end(args);
#else
		rc = smtp2PrintfV(smtp, fmt, args);
#endif
	}

	return rc;
}

SMTP_Reply_Code
mailPrintf(Mail *mail, const char *fmt, ...)
{
	SMTP_Reply_Code rc;
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
"usage: " _NAME " [-aev][-f mail][-h host[:port]][-H helo][-t timeout] rcpt... < message\n"
"\n"
"-a\t\ttry sending to all recipients even if some fail\n"
"-e\t\ttest connection and envelope details only\n"
"-f from\t\tMAIL FROM: address\n"
"-h host[:port]\tconnect to this SMTP host\n"
"-H helo\t\tEHLO/HELO argument to use; default IP-domain-literal\n"
"-t timeout\tSMTP command timeout in seconds, default 5 minutes\n"
"-v\t\tverbose debug messages; x1 log, x2 debug, x3 stderr\n"
"\n"
LIBSNERT_COPYRIGHT "\n"
;

Mail *message;
SMTP2 *session;
char *helo_host;
char *mail_from;
char *smart_host;
int envelope_test;
unsigned connect_to = SMTP_WELCOME_TO*1000;
unsigned command_to = SMTP_COMMAND_TO*1000;
char text[SMTP_TEXT_LINE_LENGTH+1];
char headers[SMTP_MAX_SIZEOF_HEADERS];

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

		if (!envelope_test) {
			if (from == NULL) {
				/* Read in the message headers and find the original sender. */
				for (offset = 0; offset+SMTP_TEXT_LINE_LENGTH < sizeof (headers); offset += length) {
					if (fgets(headers+offset, sizeof (headers)-offset, stdin) == NULL)
						break;

					length = smtp2AssertCRLF(headers+offset, strlen(headers+offset), sizeof (headers)-offset);

					if (headers[offset] == '\r' && headers[offset+1] == '\n') {
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
		}

		if ((*cmd->mail)(context, from) != SMTP_OK)
			exit(1);

		free(sender);

		for (i = 1; i < argc; i++) {
			if ((*cmd->rcpt)(context, argv[i]) != SMTP_OK)
				exit(1);
		}
		
		if (envelope_test)
			break;

		if (0 < offset) {
			if ((*cmd->print)(context, headers, offset) != SMTP_OK)
				exit(1);
		}

		while (fgets(text, sizeof (text)-2, stdin) != NULL) {
			length = smtp2AssertCRLF(text, strlen(text), sizeof (text));
			if ((*cmd->print)(context, text, strlen(text)) != SMTP_OK)
				break;
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

	while ((ch = getopt(argc, argv, "aef:h:H:t:v")) != -1) {
		switch (ch) {
		case 'a':
			flags |= SMTP_FLAG_TRY_ALL;
			break;
		case 'e':
			envelope_test = 1;
			break;
		case 'f':
			mail_from = optarg;
			break;
		case 'h':
			smart_host = optarg;
			break;
		case 'H':
			helo_host = optarg;
			break;
		case 't':
			command_to = (unsigned) strtol(optarg, NULL, 10) * 1000;
			connect_to = command_to;
			break;
		case 'v':
			debug++;
			switch (debug) {
			case 3:
			case 2: flags |= SMTP_FLAG_DEBUG;
			case 1: flags |= SMTP_FLAG_INFO;
			case 0: break;
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
		socketSetDebug(1);

		if (2 < debug)
			LogOpen("(standard error)");
	}

	if (smart_host == NULL) {
		message = mailOpen(connect_to, command_to, flags, helo_host);
		sendMessage(message, &mail_cmds, mail_from, argc-optind+1, argv+optind-1);
	} else {
		session = smtp2Open(smart_host, connect_to, command_to, flags, helo_host);
		sendMessage(session, &smtp2_cmds, mail_from, argc-optind+1, argv+optind-1);
	}

	return 0;
}
#endif /* TEST */

/***********************************************************************
 *** END
 ***********************************************************************/
