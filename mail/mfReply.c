/*
 * mfReply.c
 *
 * Copyright 2002, 2006 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#if defined(HAVE_LIBMILTER_MFAPI_H) && defined(HAVE_PTHREAD_CREATE)

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#ifdef SET_SMFI_VERSION
/* Have to define this before loading libmilter
 * headers to enable the extra handlers.
 */
# define SMFI_VERSION		SET_SMFI_VERSION
#endif

#include <libmilter/mfapi.h>

#include <com/snert/lib/mail/limits.h>
#include <com/snert/lib/mail/mf.h>
#include <com/snert/lib/mail/smf.h>
#include <com/snert/lib/mail/smtp2.h>
#include <com/snert/lib/util/Text.h>

#include <syslog.h>

/**
 * @param ctx
 *	A milter context.
 *
 * @param rcode
 *	A pointer to a C string used for the SMTP return code. If NULL,
 *	then the return code is taken from the formatted reply; otherwise
 *	450 is assumed.
 *
 * @param xcode
 *	A pointer to a C string used for the SMTP extended return code.
 *	If NULL, then the extended return code is taken from the formatted
 *	reply; otherwise 4.7.1 or 5.7.1 is assumed depending on rcode.
 *
 * @param fmt
 *	A pointer to a C printf format string.
 *
 * @param args
 *	A va_list of printf arguments.
 *
 * @return
 *	If the SMTP return code is 5xy, return SMFIS_REJECT; else assume
 *	the SMTP return code is 4xy and return SMFIS_TEMPFAIL. If malloc()
 *	fails then the formatted SMTP text reply will not be set.
 */
sfsistat
mfReplyV(SMFICTX *ctx, const char *rcode, const char *xcode, const char *fmt, va_list args)
{
	int span;
	sfsistat rc;
	char *buffer, *reply;

	errno = 0;

	if ((buffer = malloc(SMTP_REPLY_LINE_LENGTH)) == NULL) {
		/* We cannot format the reply, but we can still return
		 * a useful SMFIS_ response that the milter can then
		 * pass to sendmail.
		 */
		return rcode != NULL && SMTP_ISS_PERM(rcode) ? SMFIS_REJECT : SMFIS_TEMPFAIL;
	}

	/* Format the reply and possibly the return code. */
	(void) vsnprintf(buffer, SMTP_REPLY_LINE_LENGTH, fmt, args);

	/* Remove non-printable characters like CR, which smfi_setreply() fails on. */
	for (reply = buffer; *reply != '\0'; reply++) {
		if (isspace(*reply))
			*reply = ' ';
	}

	if (rcode == NULL) {
		span = strspn(buffer, "0123456789");
		if (span == 3 && (isspace(buffer[span]) || buffer[span] == '\0')) {
			rcode = buffer;
			reply = buffer + span + (buffer[span] != '\0');
			((char *) rcode)[span] = '\0';
		} else {
			rcode = "450";
		}
	} else {
		reply = buffer;
	}

	if (xcode == NULL) {
		span = strspn(reply, "0123456789.");
		if (5 <= span && (isspace(reply[span]) || reply[span] == '\0')) {
			xcode = reply;
			reply = reply + span + (reply[span] != '\0');
			((char *) xcode)[span] = '\0';
		} else {
			xcode = SMTP_ISS_PERM(rcode) ? "5.7.1" : "4.7.1";
		}
	}

	rc = SMTP_ISS_PERM(rcode) ? SMFIS_REJECT : SMFIS_TEMPFAIL;
	(void) smfi_setreply(ctx, (char *) rcode, (char *) xcode, reply);
	free(buffer);

	return rc;
}

/**
 * @param ctx
 *	A milter context.
 *
 * @param rcode
 *	A pointer to a C string used for the SMTP return code. If NULL,
 *	then the return code is taken from the formatted reply; otherwise
 *	450 is assumed.
 *
 * @param xcode
 *	A pointer to a C string used for the SMTP extended return code.
 *	If NULL, then the extended return code is taken from the formatted
 *	reply; otherwise 4.7.1 or 5.7.1 is assumed depending on rcode.
 *
 * @param fmt
 *	A pointer to a C printf format string.
 *
 * @param ...
 *	A list of printf arguments.
 *
 * @return
 *	If the SMTP return code is 5xy, return SMFIS_REJECT; else assume
 *	the SMTP return code is 4xy and return SMFIS_TEMPFAIL. If malloc()
 *	fails then the formatted SMTP text reply will not be set.
 */
sfsistat
mfReply(SMFICTX *ctx, const char *rcode, const char *xcode, const char *fmt, ...)
{
	sfsistat rc;
	va_list args;

	va_start(args, fmt);
	rc = mfReplyV(ctx, rcode, xcode, fmt, args);
	va_end(args);

	return rc;
}

#ifdef HAVE_SMFI_SETMLREPLY

/**
 * @param ctx
 *	A milter context.
 *
 * @param rcode
 *	A pointer to a C string used for the SMTP return code. If NULL,
 *	then the return code is taken from the formatted reply; otherwise
 *	450 is assumed.
 *
 * @param xcode
 *	A pointer to a C string used for the SMTP extended return code.
 *	If NULL, then the extended return code is taken from the formatted
 *	reply; otherwise 4.7.1 or 5.7.1 is assumed depending on rcode.
 *
 * @param lines
 *	A NULL terminated array of pointers to a C strings. At most the
 *	first 32 strings are used.
 *
 * @return
 *	If the SMTP return code is 5xy, return SMFIS_REJECT; else assume
 *	the SMTP return code is 4xy and return SMFIS_TEMPFAIL.
 */
sfsistat
mfMultiLineReplyA(SMFICTX *ctx, const char *rcode, const char *xcode, char **lines)
{
	sfsistat rc;
	int i, span, length;
	char *line0, r_code[4], x_code[10], *ln[33];

	length = 0;
	line0 = lines[0];

	if (rcode == NULL) {
		span = strspn(line0, "0123456789");
		if (span == 3 && (isspace(line0[span]) || line0[span] == '-' || line0[span] == '\0')) {
			length = TextCopy(r_code, sizeof (r_code), line0);
			length += (line0[span] != '\0');
			line0 += length;
			rcode = r_code;
		} else {
			rcode = "450";
		}
	}

	if (xcode == NULL) {
		span = strspn(line0, "0123456789.");
		if (5 <= span && (isspace(line0[span]) || line0[span] == '\0')) {
			length += TextCopy(x_code, sizeof (x_code), line0);
			length += (line0[span] != '\0');
			line0 += length;
			xcode = x_code;
		} else {
			xcode = SMTP_ISS_PERM(rcode) ? "5.7.1" : "4.7.1";
		}
	}

	/* Remove leading return code and extended return code if necessary. */
	for (i = 0; i < 32 && lines[i] != NULL; i++)
		ln[i] = lines[i] + length;
	for ( ; i <= 32; i++)
		ln[i] = NULL;

	rc = SMTP_ISS_PERM(rcode) ? SMFIS_REJECT : SMFIS_TEMPFAIL;

	(void) smfi_setmlreply(
		ctx, rcode, xcode,
		ln[ 0],ln[ 1],ln[ 2],ln[ 3],ln[ 4],ln[ 5],ln[ 6],ln[ 7],
		ln[ 8],ln[ 9],ln[10],ln[11],ln[12],ln[13],ln[14],ln[15],
		ln[16],ln[17],ln[18],ln[19],ln[20],ln[21],ln[22],ln[23],
		ln[24],ln[25],ln[26],ln[27],ln[28],ln[29],ln[30],ln[31],
		NULL
	);

	return rc;
}

/**
 * @param ctx
 *	A milter context.
 *
 * @param rcode
 *	A pointer to a C string used for the SMTP return code. If NULL,
 *	then the return code is taken from the formatted reply; otherwise
 *	450 is assumed.
 *
 * @param xcode
 *	A pointer to a C string used for the SMTP extended return code.
 *	If NULL, then the extended return code is taken from the formatted
 *	reply; otherwise 4.7.1 or 5.7.1 is assumed depending on rcode.
 *
 * @param args
 *	A NULL terminated va_list of pointers to a C strings. At most the
 *	first 32 strings are used.
 *
 * @return
 *	If the SMTP return code is 5xy, return SMFIS_REJECT; else assume
 *	the SMTP return code is 4xy and return SMFIS_TEMPFAIL.
 */
sfsistat
mfMultiLineReplyV(SMFICTX *ctx, const char *rcode, const char *xcode, va_list args)
{
	int i;
	char *s, *ln[33];

	for (i = 0; i < 32 && (s = va_arg(args, char *)) != NULL; i++)
		ln[i] = s;
	for ( ; i <= 32; i++)
		ln[i] = NULL;

	return mfMultiLineReplyA(ctx, rcode, xcode, ln);
}

/**
 * @param ctx
 *	A milter context.
 *
 * @param rcode
 *	A pointer to a C string used for the SMTP return code. If NULL,
 *	then the return code is taken from the formatted reply; otherwise
 *	450 is assumed.
 *
 * @param xcode
 *	A pointer to a C string used for the SMTP extended return code.
 *	If NULL, then the extended return code is taken from the formatted
 *	reply; otherwise 4.7.1 or 5.7.1 is assumed depending on rcode.
 *
 * @param ...
 *	A NULL terminated variable argument list of pointers to a C strings.
 *	Each string may contain multiple lines delimited by CRLF. At most
 *	the first 32 lines are used.
 *
 * @return
 *	If the SMTP return code is 5xy, return SMFIS_REJECT; else assume
 *	the SMTP return code is 4xy and return SMFIS_TEMPFAIL.
 */
sfsistat
mfMultiLineReply(SMFICTX *ctx, const char *rcode, const char *xcode, ...)
{
	long j;
	char *s;
	sfsistat rc;
	va_list args;
	Vector lines = NULL, list = NULL;

	va_start(args, xcode);

	while ((s = va_arg(args, char *)) != NULL) {
		VectorDestroy(lines);

		if ((lines = TextSplit(s, "\r\n", 0)) == NULL) {
			rc = mfReply(ctx, rcode, xcode, "generic error");
			goto error1;
		}

		for (j = 0; j < VectorLength(lines); j++) {
			if (VectorAdd(list, VectorGet(lines, j))) {
				rc = mfReply(ctx, rcode, xcode, "generic error");
				goto error2;
			}
		}
	}

	rc = mfMultiLineReplyA(ctx, rcode, xcode, (char **) VectorBase(list));
error2:
	VectorDestroy(lines);
error1:
	VectorDestroy(list);

	va_end(args);

	return rc;
}

#endif /* HAVE_SMFI_SETMLREPLY */

#endif /* HAVE_LIBMILTER_MFAPI_H */
