/*
 * smtpWrite.c
 *
 * SMTP Error Messages.
 *
 * Copyright 2006 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <com/snert/lib/io/socket2.h>
#include <com/snert/lib/mail/smtp.h>

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
smtpWrite(Socket2 *s, char *line, size_t size)
{
	if (socketWrite(s, (unsigned char *) line, size) == SOCKET_ERROR)
		return SMTP_ERROR_WRITE;

	/* Now wait for the output to be sent to the SMTP server. */
	if (!socketCanSend(s, socketGetTimeout(s)))
		return SMTP_ERROR_TIMEOUT;

	return SMTP_ERROR_OK;
}
