/*
 * TokenCount.c
 *
 * Copyright 2004, 2005 by Anthony Howe. All rights reserved.
 */

#include <string.h>

#include <com/snert/lib/util/Token.h>

/***********************************************************************
 *** 
 ***********************************************************************/

/**
 * <p>
 * Parse the string for the number of delimited tokens it contains. A
 * token consists of characters not found in the set of delimiters. It
 * may contain backslash-escape sequences, which shall be converted into
 * literals or special ASCII characters. It may contain single or double
 * quoted strings, in which case the quotes shall be removed, though any
 * backslash escape sequences within the quotes are left as is.
 * </p>
 *
 * @param string
 *	A quoted string.
 *
 * @param delims
 *	A set of delimiter characters. If NULL, then the default of set
 *	consists of space, tab, carriage-return, and line-feed (" \t\r\n").
 * 
 * @return
 *	The number of tokens contained in the string.
 *
 * @see #TextBackslash(char)
 */
int
TokenCount(const char *string, const char *delims)
{
	int count, quote, escape;
	
	if (string == NULL)
		return 0;
		
	if (delims == NULL)
		delims = " \t\r\n";

	quote = escape = 0;
	
	/* Find start of first token. */
	string += strspn(string, delims);
	count = *string != '\0';	
	
	for ( ; *string != '\0'; string++) {
		if (escape) {
			escape = 0;
			continue;
		}
		
		switch (*string) {			
		case '"': case '\'':		
			quote = *string == quote ? 0 : *string;
			continue;
			
		case '\\':
			escape = 1;
			continue;

		default:
			if (quote == 0 && strchr(delims, *string) != NULL) {
				string += strspn(string, delims)-1;
				count += string[1] != '\0';
				continue;
			}
		}
	}
	
	return count;
}
