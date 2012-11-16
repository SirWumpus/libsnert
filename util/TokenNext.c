/*
 * TokenNext.c
 *
 * Copyright 2004, 2006 by Anthony Howe. All rights reserved.
 */

#include <stdlib.h>
#include <string.h>

#include <com/snert/lib/util/Text.h>

/***********************************************************************
 ***
 ***********************************************************************/

/**
 * <p>
 * Parse the string for the next token. A token consists of characters
 * not found in the set of delimiters. It may contain backslash-escape
 * sequences, which shall be converted into literals or special ASCII
 * characters. It may contain single or double quoted strings, in which
 * case the quotes shall be removed, though any backslash escape
 * sequences within the quotes are left as is.
 * </p>
 *
 * @param string
 *	A quoted string.
 *
 * @param stop
 *	A pointer to a C string pointer. The point were the scan stops
 *	is passed back through this argument. This pointer can be NULL.
 *
 * @param delims
 *	A set of delimiter characters.
 *
 * @param flags
 *
 *	TOKEN_KEEP_EMPTY
 *
 *	If false, then a run of one or more delimeters is treated as a
 *	single delimeter separating tokens. Otherwise each delimeter
 *	separates a token that may be empty.
 *
 *	string		true		false
 *	-------------------------------------------
 *	[a,b,c]		[a] [b] [c]	[a] [b] [c]
 *	[a,,c]		[a] [] [c]	[a] [c]
 *	[a,,]		[a] [] [] 	[a]
 *	[,,]		[] [] []	(null)
 *	[]		[]		(null)
 *
 *	TOKEN_KEEP_QUOTES
 *
 *	If set, then do not strip quotes.
 *
 *	TOKEN_KEEP_BACKSLASH
 *
 *	If set, then do not strip backslash escape.
 *
 *	TOKEN_KEEP_ESCAPES
 *
 *	If set, then do not strip backslash escape nor quotes.
 *
 * @return
 *	An allocated token string.
 *
 * @see #TextBackslash(char)
 */
char *
TokenNext(const char *string, const char **stop, const char *delims, int flags)
{
	char *token, *t;
	const char *s;
	int quote = 0, escape = 0, length;

	if (string == NULL) {
		if (stop != NULL)
			*stop = NULL;
		return NULL;
	}

	if (delims == NULL)
		/* Default is C white space. */
		delims = " \t\r\n\f";

	/* Skip leading delimiters? */
	if (!(flags & TOKEN_KEEP_EMPTY)) {
		/* Find start of next token. */
		string += strspn(string, delims);

		if (*string == '\0') {
			if (stop != NULL)
				*stop = NULL;
			return NULL;
		}
	}

	/* Find end of token. */
	for (s = string; *s != '\0'; ++s) {
		if (escape) {
			escape = 0;
			continue;
		}

		switch (*s) {
		case '"': case '\'':
			if (quote == 0)
				quote = *s;
			else if (*s == quote)
				quote = 0;
			continue;
		case '\\':
			escape = 1;
			continue;
		}

		if (quote == 0 && strchr(delims, *s) != NULL)
			break;
	}

	token = malloc((s - string) + 1);
	if (token == NULL)
		return NULL;

	/* Copy token, removing quotes and backslashes. */
	for (t = token; string < s; ++string) {
		if (escape) {
			*t++ = (char) TextBackslash(*string);
			escape = 0;
			continue;
		}

		switch (*string) {
		case '"': case '\'':
			if (flags & TOKEN_KEEP_QUOTES)
				*t++ = *string;
			if (quote == 0)
				quote = *string;
			else if (*string == quote)
				quote = 0;
			else if (!(flags & TOKEN_KEEP_QUOTES))
				*t++ = *string;
			continue;
		case '\\':
			if (flags & TOKEN_KEEP_BACKSLASH)
				*t++ = *string;
			else
				escape = 1;
			continue;
		}

		if (quote == 0 && strchr(delims, *string) != NULL)
			break;

		*t++ = *string;
	}
	*t = '\0';

	if (*s == '\0') {
		/* Token found and end of string reached.
		 * Next iteration should return no token.
		 */
		s = NULL;
	} else {
		length = strspn(s, delims);
		if (flags & TOKEN_KEEP_EMPTY) {
			/* Consume only a single delimter. */
			s += length <= 0 ? 0 : 1;
		} else {
			/* Consume one or more delimeters. */
			s += length;
		}
	}

	if (stop != NULL)
		*stop = s;

	return token;
}

