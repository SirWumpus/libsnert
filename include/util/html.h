/*
 * html.h
 *
 * Copyright 2009, 2010 by Anthony Howe.  All rights reserved.
 */

#ifndef __com_snert_lib_util_html_h__
#define __com_snert_lib_util_html_h__	1

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************************
 ***
 ***********************************************************************/

/**
 * <p>
 * Parse the string for the next HTML token range. A token consists of
 * either a text block (non-HTML tag) or an HTML tag from opening left
 * angle bracket (&lt;) to closing right angle bracket (&gt;) inclusive,
 * taking into account single and double quoted attribute strings, which
 * may contain backslash-escape sequences. No conversion is done.
 * </p>
 *
 * @param start
 *	A pointer to a C string pointer where the scan is to start. It
 *	is updated to where text block begins or start of an HTML tag
 *	after skipping leading whitespace.
 *
 * @param stop
 *	A pointer to a C string pointer. The point were the scan stops
 *	is passed back through this argument.
 *
 * @param state
 *	A pointer to an int type used to hold parsing state. Initialise
 *	to zero when parsing the start of a chunk of HTML.
 *
 * @return
 *	True if a text block or (partial) HTML tag was found. Otherwise
 *	false if any of the pointers are NULL or the start of string
 *	points to the end of string NUL byte.
 */
extern int htmlTokenRange(const char **start, const char **stop, int *state);

/**
 * <p>
 * Parse the string for the next HTML token. A token consists of
 * either a text block (non-HTML tag) or an HTML tag from opening left
 * angle bracket (&lt;) to closing right angle bracket (&gt;) inclusive,
 * taking into account single and double quoted attribute strings, which
 * may contain backslash-escape sequences. No conversion is done.
 * </p>
 *
 * @param start
 *	A C string pointer where the scan is to start.
 *
 * @param stop
 *	A pointer to a C string pointer. The point were the scan stops
 *	is passed back through this argument.
 *
 * @param state
 *	A pointer to an int type used to hold parsing state. Initialise
 *	to zero when parsing the start of a chunk of HTML.
 *
 * @return
 *	An allocated C string contain a text block or HTML tag from
 *	left angle to right angle bracket inclusive. NULL on error or
 *	end of parse string.
 */
extern char *htmlTokenNext(const char *start, const char **stop, int *state);

/**
 * @param source
 *	A C string pointer.
 *
 * @param length
 *	The length of the string to decode or -1 for the whole string.
 *
 * @param buffer
 *	A pointer to buffer where the decoded source is copied to.
 *	The buffer will be NUL terminated. Note that &shy; (soft-hyphen)
 *	is decoded and discarded.
 *
 * @param size
 *	The size of the decode buffer.
 *
 * @return
 *	The length of the decoded string in the buffer.
 */
extern size_t htmlEntityDecode(const char *source, size_t length, char *buffer, size_t size);

/**
 * @param source
 *	A C string pointer.
 *
 * @param length
 *	The length of the string to encode or -1 for the whole string.
 *
 * @param buffer
 *	A pointer to buffer where the encoded source is copied to.
 *	The buffer will be NUL terminated.
 *
 * @param size
 *	The size of the encoding buffer.
 *
 * @return
 *	The length of the encoded string in the buffer. If the length
 *	is greater than or equal to the size of the buffer, then the
 *	buffer was too small and the encoded string is incomplete.
 */
extern size_t htmlEntityEncode(const char *source, size_t length, char *buffer, size_t size);

/***********************************************************************
 ***
 ***********************************************************************/

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_util_html_h__ */
