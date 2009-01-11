/*
 * smtpError.c
 *
 * SMTP Error Messages.
 *
 * Copyright 2005, 2006 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>
#include <com/snert/lib/mail/smtp.h>

static const char *smtp_error_internal[] = {
	"OK",
	"null argument",
	"empty argument",
	"out of memory",
	"unspecified internal error"
};

static const char *smtp_error_smtp[] = {
	"OK",
	"server busy, try again later",
	"no SMTP service",
	"address syntax error",
	"SMTP command temporary failure",
	"SMTP command rejected",
	"recipient rejected",
	"message rejected",
	"appears to be blocking our IP address",
	"appears to use grey-listing",
	"appears to accepts any RCPT",
	"unspecified SMTP error"
};

static const char *smtp_error_io[] = {
	"OK",
	"failed to connect",
	"unexpected EOF",
	"read error",
	"write error",
	"I/O timeout",
	"read underflow",
	"unspecified I/O error"
};

const char *
smtpGetError(unsigned smtp_error)
{
	if (smtp_error & SMTP_ERROR_INTERNAL_MASK)
		return smtp_error_internal[smtp_error >> SMTP_ERROR_INTERNAL_SHIFT];

	if (smtp_error & SMTP_ERROR_SMTP_MASK)
		return smtp_error_smtp[smtp_error >> SMTP_ERROR_SMTP_SHIFT];

	if (smtp_error & SMTP_ERROR_IO_MASK)
		return smtp_error_io[smtp_error >> SMTP_ERROR_IO_SHIFT];

	return "(unknown error)";
}
