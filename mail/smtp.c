/*
 * smtp.c
 *
 * SMTP Message Engine
 *
 * Copyright 2005, 2006 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#define _REENTRANT	1

#include <com/snert/lib/version.h>

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#if defined(HAVE_SYSLOG_H) && ! defined(__MINGW32__)
# include <syslog.h>
#endif
#if defined(HAVE_NETDB_H) && ! defined(__MINGW32__)
# include <netdb.h>
#endif

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include <com/snert/lib/crc/Luhn.h>
#include <com/snert/lib/io/Dns.h>
#include <com/snert/lib/io/Log.h>
#include <com/snert/lib/io/socket2.h>
#include <com/snert/lib/util/Text.h>
#include <com/snert/lib/mail/smtp.h>
#include <com/snert/lib/mail/tlds.h>
#include <com/snert/lib/mail/parsePath.h>

/***********************************************************************
 *** Constants
 ***********************************************************************/

#define NON_BLOCKING_WRITE
#define NON_BLOCKING_READ

/***********************************************************************
 *** Globals
 ***********************************************************************/

static int debug;
static long socketTimeout = SOCKET_CONNECT_TIMEOUT;

#define CALL_BACK_BASIC			((const char *) 0)
#define CALL_BACK_FALSE_RCPT_TEST	((const char *) 1)
#define CALL_BACK_MAX_VALUE		CALL_BACK_FALSE_RCPT_TEST

/***********************************************************************
 *** Routines
 ***********************************************************************/

void
smtpDebugSet(int flag)
{
	debug = flag;
}

void
smtpTimeoutSet(long ms)
{
	socketTimeout = ms;
}

const char *
smtpPrintLine(Socket2 *s, char *line)
{
	if (debug)
		syslog(LOG_DEBUG, "> %s", line);

	return smtpWrite(s, line, strlen(line));
}

const char *
smtpGetResponse(Socket2 *s, char *line, long size, int *code)
{
	char **lines;
	const char *error;

	if ((error = smtpRead(s, &lines, code)) != NULL) {
		*line = '\0';
		return error;
	}

	for ( ; *lines != NULL; lines++) {
		if (debug)
			syslog(LOG_DEBUG, "< %s", *lines);
	}

	TextCopy(line, size, lines[-1]);
	free(*lines);

	return NULL;
}

static const char *
smtpConnect(smtpMessage *ctx, Socket2 **client, char *helo, char *line)
{
	SocketAddress *addr;
	const char *error = NULL;

	/* Assume a broken connection. For example ovh.net will break
	 * a connection for an unknown RCPT. So in order to complete
	 * a call-back to validate a sender, reconnect to continue.
	 */
	if (debug)
		syslog(LOG_DEBUG, "connecting to SMTP %s...", ctx->mx);

	socketClose(*client);
	*client = NULL;

	if ((addr = socketAddressCreate(ctx->mx, SMTP_PORT)) == NULL) {
		error = smtpErrorMemory;
		goto error0;
	}

	if ((*client = socketOpen(addr, 1)) == NULL) {
		error = smtpErrorMemory;
		goto error1;
	}

	if (socketClient(*client, socketTimeout)) {
		error = smtpErrorConnect;
		goto error2;
	}

#if defined(NON_BLOCKING_READ) && defined(NON_BLOCKING_WRITE)
	/* Set non-blocking I/O once for both smtpGetResponse() and
	 * smtpPrintLine() and leave it that way.
	 */
	if (socketSetNonBlocking(*client, 1)) {
		error = smtpErrorNonblocking;
		goto error2;
	}
#endif
	if ((error = smtpGetResponse(*client, line, SMTP_TEXT_LINE_LENGTH, &ctx->code)) != NULL)
		goto error2;

	switch (ctx->code) {
	case 220:
		break;
	case 421:
		error = smtpErrorBusy;
		goto error2;
	case 554:
		error = smtpErrorNoService;
		goto error2;
	default:
		if (strstr(line, helo) != NULL || (ctx->this_ip != NULL && strstr(line, ctx->this_ip) != NULL))
			error = smtpErrorBlocked;
		else
			error = smtpErrorWelcome;
		goto error2;
	}

	snprintf(line, SMTP_TEXT_LINE_LENGTH, "HELO %s\r\n", helo);
	if ((error = smtpPrintLine(*client, line)) != NULL)
		goto error2;
	if ((error = smtpGetResponse(*client, line, SMTP_TEXT_LINE_LENGTH, &ctx->code)) != NULL)
		goto error2;
	if (ctx->code != 250) {
		error = smtpErrorHelo;
		goto error2;
	}
error2:
	if (error != NULL) {
		socketClose(*client);
		*client = NULL;
	}
error1:
	free(addr);
error0:
	return error;
}

static const char *
smtpSendFalseRcpt(smtpMessage *ctx, Socket2 **client, char *helo, char *line)
{
	int span, accepted;
	char *false_rcpt;
	const char *error;

	error = NULL;

	snprintf(line, SMTP_TEXT_LINE_LENGTH, "MAIL FROM:<%s>\r\n", ctx->mail);
	if ((error = smtpPrintLine(*client, line)) != NULL)
		goto error0;
	if ((error = smtpGetResponse(*client, line, SMTP_TEXT_LINE_LENGTH, &ctx->code)) != NULL)
		goto error0;
	if (ctx->code != 250) {
		error = smtpErrorMail;
		goto error0;
	}

	span = strcspn(ctx->rcpt, "@");
	if (ctx->rcpt[span] != '@') {
		error = smtpErrorRcpt;
		goto error0;
	}

	if ((false_rcpt = malloc(strlen(ctx->rcpt)+2)) == NULL) {
		error = smtpErrorMemory;
		goto error0;
	}

	/* Generate a false address, which is the local-part reversed,
	 * case inverted, with an LUHN check digit appended.
	 */
	strncpy(false_rcpt, ctx->rcpt, span);
	false_rcpt[span] = '\0';
	TextReverse(false_rcpt, span);
	TextInvert(false_rcpt, span);
	false_rcpt[span] = LuhnGenerate(false_rcpt) + '0';
	false_rcpt[span+1] = '@';
	TextCopy(false_rcpt+span+2, SMTP_TEXT_LINE_LENGTH-span-2, (char *) ctx->rcpt+span+1);

	snprintf(line, SMTP_TEXT_LINE_LENGTH, "RCPT TO:<%s>\r\n", false_rcpt);
	free(false_rcpt);

	if ((error = smtpPrintLine(*client, line)) != NULL)
		goto error0;
	if ((error = smtpGetResponse(*client, line, SMTP_TEXT_LINE_LENGTH, &ctx->code)) != NULL) {
		if (error != smtpErrorReadTimeout && error != smtpErrorInput)
			/* Handles case were a server breaks connection for
			 * an invalid recipient. ovh.net is an example.
			 */
			error = smtpConnect(ctx, client, helo, line);
		goto error0;
	}

	accepted = ctx->code == 250;

	if (strstr(line, "4.7.1") != NULL) {
		error = smtpErrorGreyList;
		goto error0;
	}

	if ((error = smtpPrintLine(*client, "RSET\r\n")) != NULL)
		goto error0;
	if ((error = smtpGetResponse(*client, line, SMTP_TEXT_LINE_LENGTH, &ctx->code)) != NULL) {
		if (error != smtpErrorReadTimeout)
			/* Handles case were a server breaks connection if
			 * a RSET is sent. ovh.net is an example.
			 */
			error = smtpConnect(ctx, client, helo, line);
		goto error0;
	}
	if (ctx->code != 250) {
		error = smtpErrorRset;
		goto error0;
	}

	if (accepted)
		error = smtpErrorAnyRcpt;
error0:
	return error;
}

/*
 *
 */
const char *
smtpSendString(smtpMessage *ctx, const char *buffer)
{
	int span;
	size_t offset;
	Socket2 *client = NULL;
	char *line, *hostname, *helo;
	const char *error = NULL, *alt_error = NULL;

	if (ctx == NULL) {
		errno = EFAULT;
		return smtpErrorNullArgument;
	}

	if (debug)
		syslog(
			LOG_DEBUG, "enter smtpSendString(%lx, \"%.15s...\") ",
			(long) ctx, buffer <= CALL_BACK_MAX_VALUE ? "" : buffer
		);

	if (ctx->mx == NULL || ctx->mail == NULL || ctx->rcpt == NULL) {
		error = smtpErrorNullArgument;
		errno = EFAULT;
		goto error0;
	}

	if (*ctx->rcpt == '\0' || *ctx->mx == '\0') {
		error = smtpErrorEmptyArgument;
		errno = EINVAL;
		goto error0;
	}

	if ((line = malloc(SMTP_TEXT_LINE_LENGTH+2)) == NULL) {
		error = smtpErrorMemory;
		goto error0;
	}

	if ((hostname = malloc(SMTP_DOMAIN_LENGTH)) == NULL) {
		error = smtpErrorMemory;
		goto error1;
	}

	helo = (char *) ctx->helo;
	if (helo == NULL || *helo == '\0' || strchr(helo, '.') == NULL) {
		if (gethostname(hostname, SMTP_DOMAIN_LENGTH)) {
			error = smtpErrorNotSent;
			goto error2;
		}

		helo = hostname;
		if (*helo == '\0' || strchr(helo, '.') == NULL)
			helo = "[127.0.0.1]";
	}

	if ((error = smtpConnect(ctx, &client, helo, line)) != NULL)
		goto error2;

	if (buffer == CALL_BACK_FALSE_RCPT_TEST) {
		/* This is an extra test for performing a call-back. */
		error = smtpSendFalseRcpt(ctx, &client, helo, line);
		if (error != NULL && error != smtpErrorAnyRcpt)
			goto error4;

		/* Remember the result of the false RCPT test. */
		alt_error = error;
	}

try_again_once_after_reconnect:

	snprintf(line, SMTP_TEXT_LINE_LENGTH, "MAIL FROM:<%s>\r\n", ctx->mail);
	if ((error = smtpPrintLine(client, line)) != NULL)
		goto error4;
	if ((error = smtpGetResponse(client, line, SMTP_TEXT_LINE_LENGTH, &ctx->code)) != NULL) {
		/* This handles the case where a server chooses to break
		 * the connection on the next MAIL command after a RSET.
		 * The MX for stacey.ca (in1.magma.ca) is an example.
		 */
		if (buffer == CALL_BACK_FALSE_RCPT_TEST && error != smtpErrorReadTimeout) {
			if ((error = smtpConnect(ctx, &client, helo, line)) == NULL) {
				buffer = CALL_BACK_BASIC;
				goto try_again_once_after_reconnect;
			}
		}
		goto error4;
	}
	if (ctx->code != 250) {
		error = smtpErrorMail;
		goto error5;
	}

	snprintf(line, SMTP_TEXT_LINE_LENGTH, "RCPT TO:<%s>\r\n", ctx->rcpt);
	if ((error = smtpPrintLine(client, line)) != NULL)
		goto error4;
	if ((error = smtpGetResponse(client, line, SMTP_TEXT_LINE_LENGTH, &ctx->code)) != NULL)
		goto error4;
	if (ctx->code != 250) {
		error = strstr(line, "4.7.1") == NULL ? smtpErrorRcpt : smtpErrorGreyList;
		goto error4;
	}

	if (buffer <= CALL_BACK_MAX_VALUE) {
		error = alt_error;
		goto error5;
	}

	if ((error = smtpPrintLine(client, "DATA\r\n")) != NULL)
		goto error4;
	if ((error = smtpGetResponse(client, line, SMTP_TEXT_LINE_LENGTH, &ctx->code)) != NULL)
		goto error4;
	if (ctx->code != 354) {
		error = smtpErrorData;
		goto error5;
	}

	/* Send the message a line at a time. */
	for (offset = 0; buffer[offset] != '\0'; offset += span) {
		/* Find length of line segment. */
		span = strcspn(buffer + offset, "\n");
		if (buffer[offset+span] == '\n')
			span++;

		/* Handle dot transparency. */
		if (buffer[offset] == '.' && socketWrite(client, ".", 1) != 1) {
			error = smtpErrorOutput;
			goto error4;
		}

		/* Send line segment. */
		if (socketWrite(client, (char *) (buffer + offset), span) != span) {
			error = smtpErrorOutput;
			goto error4;
		}

		if (!socketCanSend(client, socketTimeout)) {
			error = smtpErrorWriteTimeout;
			goto error4;
		}
	}

	/* Make sure the last line is terminated by a newline. */
	if (0 < offset && buffer[offset-1] != '\n') {
		if ((error = smtpPrintLine(client, "\r\n")) != NULL)
			goto error4;
	}

	if ((error = smtpPrintLine(client, ".\r\n")) != NULL)
		goto error4;
	if ((error = smtpGetResponse(client, line, SMTP_TEXT_LINE_LENGTH, &ctx->code)) != NULL)
		goto error4;
	if (ctx->code != 250) {
		error = smtpErrorMessage;
		goto error5;
	}

	error = alt_error;
error5:
	/* DO NOT wait for the SMTP respone from the QUIT command. Many
	 * SMTP server implementations simply drop the connection when
	 * they receive the QUIT command. If the server returned 421 or
	 * 554 for the welcome message, they might have dropped the
	 * connection immediately after instead of waiting for the QUIT
	 * like RFC 2821 section 3.1 Session Initiation paragraph 3 says
	 * they should.
	 */
	(void) smtpPrintLine(client, "QUIT\r\n");
error4:
	socketClose(client);
error2:
	free(hostname);
error1:
	free(line);
error0:
	if (debug)
		syslog(
			LOG_DEBUG, "exit  smtpSendString(%lx, \"%.15s...\") error=%s",
			(long) ctx, buffer <= CALL_BACK_MAX_VALUE ? "" : buffer,
			error == NULL ? "NULL" : error
		);

	ctx->error = error;

	return error;
}

/*
 *
 */
const char *
smtpSendMessageV(smtpMessage *ctx, const char *fmt, va_list args)
{
	long i;
	Dns dns;
	char *buffer;
	Vector mxList;
	ParsePath *path;
	DnsEntry *mx = NULL;
	const char *error = NULL;

	if (debug)
		syslog(
			LOG_DEBUG, "enter smtpSendMessageV(%lx, \"%.15s...\", va_list)",
			(long) ctx, fmt <= CALL_BACK_MAX_VALUE ? "" : fmt
		);

	if (fmt <= CALL_BACK_FALSE_RCPT_TEST) {
		/* Call-back only test. */
		buffer = (char *) fmt;
	} else {
		/* Create a buffer for the message with enough space
		 * to insert all the variable arguments into it.
		 */
		if ((buffer = malloc(SMTP_MINIMUM_MESSAGE_LENGTH)) == NULL) {
			ctx->error = smtpErrorMemory;
			goto error0;
		}

		/* Format the message. */
		vsnprintf(buffer, SMTP_MINIMUM_MESSAGE_LENGTH, fmt, args);
	}

	if (ctx->mx != NULL) {
		/* Send message to a specific host. */
		error = smtpSendString(ctx, buffer);
		goto error1;
	}

	/* Otherwise get the MX list for the recipient's domain. */
	if ((error = parsePath(ctx->rcpt, STRICT_LENGTH, 0, &path)) != NULL)
		goto error1;

	if ((dns = DnsOpen()) == NULL) {
		error = DnsGetError(dns);
		goto error2;
	}

	if ((mxList = DnsGet(dns, DNS_TYPE_MX, 1, path->domain.string)) == NULL) {
		error = DnsGetError(dns);
		goto error3;
	}

	if (debug)
		syslog(LOG_DEBUG, "MX list length=%ld", VectorLength(mxList));

	/* Try each MX in turn. */
	for (i = 0; i < VectorLength(mxList); i++) {
		if ((mx = VectorGet(mxList, i)) == NULL)
			continue;

retry_single_mx_once:

		if ((ctx->mx = mx->address_string) == NULL) {
			syslog(LOG_WARN, "MX %d %s has no IP", mx->preference, (char *) mx->value);
			continue;
		}

		if (!ctx->using_internal_mx) {
			if (isReservedIPv6(mx->address, IS_IP_ANY)) {
				syslog(LOG_WARN, "MX %d %s [%s] reserved IP", mx->preference, (char *) mx->value, mx->address_string);
				continue;
			}

			if (!hasValidTLD(mx->value)) {
				syslog(LOG_WARN, "MX %d %s [%s] invalid TLD", mx->preference, (char *) mx->value, mx->address_string);
				continue;
			}
		}

		if ((error = smtpSendString(ctx, buffer)) == NULL)
			break;

		/* Break on success or session errors, but
		 * not for connection related errors.
		 */
		if (error == NULL
		||  error == smtpErrorHelo
		||  error == smtpErrorMail
		||  error == smtpErrorRcpt
		||  error == smtpErrorData
		||  error == smtpErrorRset
		||  error == smtpErrorMessage
		||  error == smtpErrorAnyRcpt
		||  error == smtpErrorGreyList
		)
			break;
	}

	/* For a single MX server, repeat the connection attempt once. */
	if (buffer <= CALL_BACK_MAX_VALUE && i == 1 && mx != NULL && mx->address != NULL
	&& error != NULL && error != smtpErrorNullArgument && error != smtpErrorEmptyArgument) {
		sleep(30);

		/* This goto breaks with a long standing coding style I've
		 * maintained - that is to use goto's only to jump forward,
		 * never backwards and especially not into a loop. One day
		 * the above loop and this goto will have to be rewritten
		 * to correct this lapse.
		 */
		goto retry_single_mx_once;
	}

	if (VectorLength(mxList) <= i)
		error = smtpErrorNotSent;

	VectorDestroy(mxList);
error3:
	DnsClose(dns);
error2:
	free(path);
error1:
	if (CALL_BACK_MAX_VALUE < buffer)
		free(buffer);
error0:
	if (debug)
		syslog(
			LOG_DEBUG, "exit  smtpSendMessageV(%lx, \"%.15s...\", va_list) error=%s",
			(long) ctx, fmt <= CALL_BACK_MAX_VALUE ? "" : fmt, error == NULL ? "NULL" : error
		);

	ctx->error = error;

	return error;
}

const char *
smtpSendMessage(smtpMessage *ctx, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	(void) smtpSendMessageV(ctx, fmt, args);
	va_end(args);

	return ctx->error;
}

const char *
smtpSendMailV(const char *host, const char *mail, const char *rcpt, const char *msg, va_list args)
{
	smtpMessage ctx;

	memset(&ctx, 0, sizeof (ctx));

	ctx.mx = host;
	ctx.mail = mail;
	ctx.rcpt = rcpt;

	return smtpSendMessageV(&ctx, msg, args);
}

const char *
smtpSendMail(const char *host, const char *mail, const char *rcpt, const char *msg, ...)
{
	va_list args;
	const char *error;

	va_start(args, msg);
	error = smtpSendMailV(host, mail, rcpt, msg, args);
	va_end(args);

	return error;
}

#ifdef TEST
#include <stdio.h>
#include <com/snert/lib/util/getopt.h>

static char usage[] =
"usage: smtp [-ixv][-h helo][-m mx][-n dns,...][-t timeout] mail rcpt [message]\n"
"\n"
"-h helo\t\tHELO argument to be used.\n"
"-i\t\tinternal MX, skip reserved IP & TLD checks\n"
"-m mx\t\tuse this specifix MX\n"
"-n dns,...\tuse these DNS servers for MX lookups\n"
"-t timeout\tsocket timeout in seconds, default 60\n"
"-x\t\tperform false RCPT test during a call-back\n"
"-v\t\tverbose logging to maillog\n"
"\n"
"mail\t\tsender's MAIL FROM: address without angle brackets\n"
"rcpt\t\trecipient's RCPT TO: address without angle brackets\n"
"message\t\ta brief test message; ignored if -x specified. If\n"
"\t\tno message argument given, then perform call-back test.\n"
"\n"
LIBSNERT_COPYRIGHT "\n"
;

static char basic[] =
"From: SMTP Test <%s>\r\n"
"To: <%s>\r\nSubject: smtpSendMail() test\r\n"
"Precedence: bulk\r\n"
"Priority: normal\r\n"
"\r\n"
"%s";

int
main(int argc, char **argv)
{
	int ch;
	Vector dns;
	smtpMessage ctx;
	const char *error, *callback;

	callback = NULL;
	memset(&ctx, 0, sizeof (ctx));

	while ((ch = getopt(argc, argv, "h:im:n:t:vx")) != -1) {
		switch (ch) {
		case 'h':
			ctx.helo = optarg;
			break;
		case 'i':
			ctx.using_internal_mx = 1;
			break;
		case 'm':
			ctx.mx = optarg;
			break;
		case 'n':
			if ((dns = TextSplit(optarg, ",", 0)) == NULL) {
				fprintf(stderr, "out of memory\n");
				exit(71);
			}

			DnsSetNameServers((char **) VectorBase(dns));
			VectorDestroy(dns);
			break;
		case 't':
			smtpTimeoutSet(strtol(optarg, NULL, 10) * 1000);
			break;
		case 'v':
			openlog("smtp", LOG_PID, LOG_MAIL);
			smtpDebugSet(1);
			DnsSetDebug(1);
			socketSetDebug(1);
			break;
		case 'x':
			callback = CALL_BACK_FALSE_RCPT_TEST;
			break;
		default:
			(void) fprintf(stderr, usage);
			return 64;
		}
	}

	if (socketInit()) {
		fprintf(stderr, "socketInit() failed: %s (%d)\n", strerror(errno), errno);
		return 71;
	}

	if (optind+2 == argc || callback == CALL_BACK_FALSE_RCPT_TEST) {
		ctx.mail = argv[optind];
		ctx.rcpt = argv[optind+1];
		error = smtpSendMessage(&ctx, callback);
	}

	else if (optind+3 == argc) {
		error = smtpSendMail(
			ctx.mx, argv[optind], argv[optind+1], basic,
			argv[optind], argv[optind+1], argv[optind+2]
		);
	}

	else {
		(void) fprintf(stderr, usage);
		return 64;
	}

	socketFini();

	if (error != NULL) {
		fprintf(stderr, "%s\n", error);
		return 1;
	}

	return 0;
}
#endif /* TEST */
