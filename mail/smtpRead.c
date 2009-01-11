/*
 * smtpRead.c
 *
 * Read an SMTP response containing one or more lines.
 *
 * Copyright 2006 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <com/snert/lib/io/socket2.h>
#include <com/snert/lib/mail/smtp.h>

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
 * @param code
 *	A pointer to an integer used to return the SMTP response code.
 *	The value return is undefined in case of an error.
 *
 * @return
 *	Zero (0) on success or an SMTP_ERROR_ code, in which case lines
 *	will be NULL.
 */
int
smtpRead(Socket2 *s, char ***lines, int *code)
{
	long length;
	int ch, error;
	char *buffer, **replace;
	size_t size, offset, line_no, line_max;

	if (s == NULL || lines == NULL || code == NULL) {
		errno = EFAULT;
		return SMTP_ERROR_NULL;
	}

	size = 0;
	offset = 0;
	line_no = 0;
	line_max = 0;
	*lines = NULL;

	do {
		if (line_max <= line_no || size <= offset) {
			if ((replace = realloc(*lines, sizeof (char *) * (line_max + 11) + offset + SMTP_REPLY_LINE_LENGTH)) == NULL) {
				error = SMTP_ERROR_MEMORY;
				goto error0;
			}

			size += SMTP_REPLY_LINE_LENGTH;
			line_max += 10;
			*lines = replace;
			(*lines)[line_no] = NULL;
		}

		buffer = (char *) &(*lines)[line_max + 1];
		(*lines)[line_no++] = buffer + offset;
		(*lines)[line_no] = NULL;

		switch (length = socketReadLine(s, buffer+offset, size-offset)) {
		case SOCKET_ERROR:
			error = errno == ETIMEDOUT ? SMTP_ERROR_TIMEOUT : SMTP_ERROR_READ;
			goto error0;
		case SOCKET_EOF:
			error = SMTP_ERROR_EOF;
			errno = ENOTCONN;
			goto error0;
		default:
			/* Did we read sufficient characters for a response code? */
			if (length < 4) {
				error = SMTP_ERROR_UNDERFLOW;
				goto error0;
			}

			ch = buffer[offset + 3];

			if (!isdigit(buffer[offset])
			&&  !isdigit(buffer[offset+1])
			&&  !isdigit(buffer[offset+2])
			&&  !isspace(ch) && ch != '-') {
				error = SMTP_ERROR_READ;
				errno = EIO;
				goto error0;
			}
		}

		offset += length + 1;
	} while (ch == '-');

	if (0 < line_no)
		*code = (int) strtol((*lines)[0], NULL, 10);

	return SMTP_ERROR_OK;
error0:
	free(*lines);
	*lines = NULL;
	*code = 451;

	return error;
}

/***********************************************************************
 *** END
 ***********************************************************************/
