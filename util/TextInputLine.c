/*
 * TextInputLine.c
 *
 * Copyright 2001, 2005 by Anthony Howe. All rights reserved.
 */

#include <com/snert/lib/version.h>
#include <com/snert/lib/util/Text.h>

/***********************************************************************
 ***
 ***********************************************************************/

/**
 * <p>
 * Read a line of input until a newline (CRLF or LF) is read or the buffer
 * is filled. The buffer is always null terminated.
 * </p>
 *
 * @param fp
 *	The FILE * from which to read input.
 *
 * @param line
 *	The input buffer.
 *
 * @param size
 *	The size of the input buffer.
 *
 * @param keep_nl
 *	True if the LF or CRLF newline should be kept in the buffer.
 *
 * @return
 * 	Return the length of the input buffer. otherwise -1 on error.
 */
long
TextInputLine2(FILE *fp, char *line, long size, int keep_nl)
{
	long i;

	for (i = 0, --size; i < size; ++i) {
		line[i] = (char) fgetc(fp);

		if (feof(fp) || ferror(fp))
			return -1;

		if (line[i] == '\n') {
			/* Newline found. */
			i += keep_nl;
			line[i] = '\0';
			if (!keep_nl && 0 < i && line[i-1] == '\r')
				line[--i] = '\0';
			break;
		}
	}

	line[i] = '\0';

	return i;
}

/**
 * <p>
 * Read a line of input until a newline (CRLF or LF) is read or the buffer
 * is filled. The newline is removed from the input and the buffer is always
 * null terminated.
 * </p>
 *
 * @param fp
 *	The FILE * from which to read input.
 *
 * @param line
 *	The input buffer.
 *
 * @param size
 *	The size of the input buffer.
 *
 * @return
 * 	Return the length of the input buffer. otherwise -1 on error.
 */
long
TextInputLine(FILE *fp, char *line, long size)
{
	return TextInputLine2(fp, line, size, 0);
}
