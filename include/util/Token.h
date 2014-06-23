/*
 * Token.h
 *
 * Copyright 2001, 2006 by Anthony Howe.  All rights reserved.
 */

#ifndef __com_snert_lib_util_Token_h__
#define __com_snert_lib_util_Token_h__	1

#ifdef __cplusplus
extern "C" {
#endif

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
 *	If not set, then a run of one or more delimeters is treated as a
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
 *	TOKEN_KEEP_ESCAPES
 *
 *	The token might have backslash escapes that are suppose to be
 *	part of the token, like a regex string /RE/ where you need to
 *	keep any "\/" between the open and closing slashes. We still
 *	need to recognise escapes and not convert them to a literal.
 *
 *	TOKEN_IGNORE_QUOTES
 *
 *	Disable any special processing of quoted substrings; quotes
 *	are treated as literals.
 *
 *	TOKEN_KEEP_ASIS
 *
 *	Shorthand for TOKEN_KEEP_BACKSLASH | TOKEN_IGNORE_QUOTES.
 *
 * @return
 *	An allocated token.
 *
 * @see #TextBackslash(char)
 */
extern char *TokenNext(const char *, const char **, const char *, int);

#define TOKEN_KEEP_EMPTY	0x0001
#define TOKEN_KEEP_BACKSLASH	0x0002
#define TOKEN_IGNORE_QUOTES	0x0004
#define TOKEN_KEEP_OPEN_CLOSE	0x0008
#define TOKEN_KEEP_ASIS		(TOKEN_KEEP_BACKSLASH|TOKEN_IGNORE_QUOTES)

/**
 * <p>
 * Parse the string of delimited tokens into an array of pointers to C
 * strings. A token consists of characters not found in the set of
 * delimiters. It may contain backslash-escape sequences, which shall be
 * converted into literals or special ASCII characters. It may contain
 * single or double quoted strings, in which case the quotes shall be
 * removed, though any backslash escape sequences within the quotes are
 * left as is.
 * </p>
 *
 * @param string
 *	A quoted string. A copy is made.
 *
 * @param delims
 *	A set of delimiter characters. If NULL, then the default of set
 *	consists of space, tab, carriage-return, and line-feed (" \t\r\n").
 *
 * @param pargv
 *	Used to pass back to the caller an array of pointers to C strings.
 *	The array is always terminated by a NULL pointer. Its the caller's
 *	responsibility to free() this array when done, which will also
 *	free its contents.
 *
 * @param pargc
 *	Used to pass back the length of the array of pointers to C strings,
 *	excluding the ternminating NULL pointer. Can be NULL.
 *
 * @param pad
 *	The array passed back is prefixed with this many unused elements.
 *	The first token from string will be placed at this offset.
 *
 * @return
 *	Zero on success. Otherwise -1 on error.
 *
 * @see #TextBackslash(char)
 * @see #TokenSplitA(char *, const char *, char **, int)
 */
extern int TokenSplit(const char *, const char *, char ***, int *, int);

/**
 * <p>
 * Parse the string of delimited tokens into an array of pointers to C
 * strings. A token consists of characters not found in the set of
 * delimiters. It may contain backslash-escape sequences, which shall be
 * converted into literals or special ASCII characters. It may contain
 * single or double quoted strings, in which case the quotes shall be
 * removed, though any backslash escape sequences within the quotes are
 * left as is.
 * </p>
 *
 * @param string
 *	A quoted string. This string will be modified in place.
 *
 * @param delims
 *	A set of delimiter characters. If NULL, then the default of set
 *	consists of space, tab, carriage-return, and line-feed (" \t\r\n").
 *
 * @param argv
 *	An array of pointers to C strings to be filled in. This array is
 *	always terminated by a NULL pointer.
 *
 * @param argc
 *	The size of the argv array, which must be greater than offset.
 *
 * @return
 *	The length of the argv array filled. This is less than or equal to
 *	argc. Otherwise -1 for an invalid argument if string or argv is NULL,
 *	or if argc is too small.
 *
 * @see #TextBackslash(char)
 * @see #TokenSplit(const char *, const char *, char ***, int *, int)
 */
extern int TokenSplitA(char *, const char *, char **, int);

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
extern int TokenCount(const char *, const char *);

/**
 * @param string
 *	An unquoted string.
 *
 * @param delims
 *	A set of characters to be backslash quoted. If NULL, then the
 *	default of set consists of single and double quotes.
 *
 * @return
 *	A pointer to an allocated quoted C string or NULL. It is the
 *	caller's responsiblity to free() this string.
 */
extern char *TokenQuote(const char *, const char *);

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_util_Token_h__ */
