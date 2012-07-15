/*
 * Text.h
 *
 * Copyright 2001, 2006 by Anthony Howe.  All rights reserved.
 */

#ifndef __com_snert_lib_util_Text_h__
#define __com_snert_lib_util_Text_h__	1

#ifndef __com_snert_lib_type_Vector_h__
#include <com/snert/lib/type/Vector.h>
#endif

#ifndef __com_snert_lib_util_Buf_h__
#include <com/snert/lib/util/Buf.h>
#endif

#ifndef BUFSIZ
#include <stdio.h>
#endif

#include <string.h>

#include <com/snert/lib/util/Token.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef HAVE_STRLCPY
# define TextCopy(t,n,s)		strlcpy(t, s, n)
#else
extern size_t TextCopy(char *t, size_t tsize, const char *s);
#endif

#ifdef HAVE_STRLCAT
# define TextCat(t,n,s)			strlcat(t, s, n)
#else
extern size_t TextCat(char *t, size_t tsize, char *s);
#endif

#ifdef HAVE_STRDUP
# define TextDup			strdup
#else
extern /*@only@*/ char *TextDup(const char *);
#endif

/*
 * Comparision functions; return -ve, 0, +ve for <, ==, >.
 */
#ifdef HAVE_STRCMP
# define TextSensitiveCompare		strcmp
#else
extern int TextSensitiveCompare(const void *xp, void const *yp);
#endif

#ifdef HAVE_STRNCMP
# define TextSensitiveCompareN		strncmp
#else
extern int TextSensitiveCompareN(const void *xp, void const *yp, long len);
#endif

#ifdef HAVE_STRCASECMP
# define TextInsensitiveCompare		strcasecmp
#else
extern int TextInsensitiveCompare(const void *xp, void const *yp);
#endif

#ifdef HAVE_STRNCASECMP
# define TextInsensitiveCompareN	strncasecmp
#else
extern int TextInsensitiveCompareN(const void *xp, void const *yp, long len);
#endif

extern /*@only@*/ char *TextSubstring(const char *, long offset, long length);
extern /*@only@*/ char *TextDupN(const char *, size_t);
extern unsigned long TextHash(unsigned long, const char *);
extern const char *TextNull(const char *);
extern const char *TextEmpty(const char *);
extern const char *TextDelim(const char *, const char *);
extern int TextIsInteger(const char *, int);
extern Buf *TextExpand(Buf *s, long col);
extern char *TextHexEncode(Buf *b);

#define TextIsBlank(s)		((s) == NULL || (s)[strcspn(s, " \t\r\n\f")] == '\0')
#define TextIsEmpty(s)		((s) == NULL || *(s) == '\0')

#ifndef TextIsBlank
/**
 * @param s
 *	A C string pointer.
 *
 * @return
 *	True if the string is empty (zero length)
 *	or contains only whitespace.
 */
extern int TextIsBlank(const char *s);
#endif
#ifndef TextIsEmpty
/**
 * @param s
 *	A C string pointer.
 *
 * @return
 *	True if the string is empty (zero length).
 */
extern int TextIsEmpty(const char *s);
#endif

/**
 * <p>
 * Given the character following a backslash, return the
 * the character's ASCII escape value.
 * </p>
 * <pre>
 *   bell            \a	0x07
 *   backspace       \b	0x08
 *   escape          \e	0x1b
 *   formfeed        \f	0x0c
 *   linefeed        \n	0x0a
 *   return          \r	0x0d
 *   space           \s	0x20
 *   tab             \t	0x09
 *   vertical-tab    \v	0x0b
 * </pre>
 *
 * @param ch
 *	A character that followed a backslash.
 *
 * @return
 *	The ASCII value of the escape character or the character itself.
 */
extern int TextBackslash(char);

/**
 * <p>
 * The given string contains a list of substrings separated by the
 * specified delimiter characters. The substrings may contain quoted
 * strings and/or contain backslash-escaped characters. The common
 * backslash escape sequences are supported and return their ASCII
 * values.
 * </p>
 *
 * @param string
 *	A list represented as a string.
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
 *	[,,]		[] [] []	(empty vector)
 *	[]		[]		(empty vector)
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
 *	A vector of C strings.
 */
extern /*@only@*/ Vector TextSplit(const char *, const char *, int);

/**
 * <p>
 * Join a vector of strings into a string with the given delimiter.
 * </p>
 *
 * @param delim
 *	A delimiter of one or more characters.
 *
 * @param strings
 *	A list of strings to be joined.
 *
 * @return
 *	A string comprised of all the objects in string representation
 *	each separated by the string delimiter.
 */
extern /*@only@*/ char *TextJoin(const char *, Vector);

/*
 * Comparision functions; return -1 no match, 0 <= length
 */
extern long TextSensitiveStartsWith(const char *text, const char *prefix);
extern long TextInsensitiveStartsWith(const char *text, const char *prefix);

/**
 * <p>
 * Compare the trailing part of the string in a case sensitive manner.
 * </p>
 *
 * @param text
 *	The string to check.
 *
 * @param suffix
 *	The string suffix to match.
 *
 * @return
 *	Return the offset into text where suffix starts, otherwise -1
 *	on no match.
 */
extern long TextSensitiveEndsWith(const char *text, const char *suffix);

/**
 * <p>
 * Compare the trailing part of the string in a case insensitive manner.
 * </p>
 *
 * @param text
 *	The string to check.
 *
 * @param suffix
 *	The string suffix to match.
 *
 * @return
 *	Return the offset into text where suffix starts, otherwise -1
 *	on no match.
 */
extern long TextInsensitiveEndsWith(const char *text, const char *suffix);

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
extern long TextInputLine(FILE *fp, /*@out@*/ char *line, long size);

extern long TextInputLine2(FILE *fp, /*@out@*/ char *line, long size, int keep_nl);

/**
 * <p>
 * Read a line of input until a newline (CRLF or LF) is read or the buffer
 * is filled. The newline is removed from the input and the buffer is always
 * null terminated.
 * </p>
 *
 * @param fd
 *	The file handle from which to read input.
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
extern long TextReadLine(int fd, /*@out@*/ char *line, long size);

extern long TextReadLine2(int fd, /*@out@*/ char *line, long size, int keep_nl);

/*
 * Search routines.
 */
extern int TextCountOccurences(const char *str, const char *sub);
extern long TextSensitiveFind(const char *text, const char *sub);
extern long TextInsensitiveFind(const char *text, const char *sub);

/*
 * In-place alterations.
 */
extern void TextInvert(char *str, long length);
extern void TextLower(char *str, long length);
extern void TextUpper(char *str, long length);
extern void TextReverse(char *str, long length);
extern void TextTransliterate(char *str, const char *from_set, const char *to_set, long length);

/**
 * Scan backwards from an offset in the string looking for the
 * last non-delimiter ahead of a delimiter character.
 *
 * @param string
 *	String to scan backwards through.
 *
 * @param offset
 *	Offset in string of a previously matching non-delimiter.
 *
 * @param delims
 *	String of delimiter characters to match.
 *
 * @return
 *	The offset from the start of the string of the last matching
 *	non-delimiter found while scanning backwards.
 */
extern int strlrcspn(const char *string, size_t offset, const char *delims);

/**
 * Scan backwards from an offset in the string looking for the
 * last matching delimiter ahead of a non-delimiter character.
 *
 * @param string
 *	String to scan backwards through.
 *
 * @param offset
 *	Offset in string of a previously matching delimiter.
 *
 * @param delims
 *	String of delimiter characters to match.
 *
 * @return
 *	The offset from the start of the string of the last matching
 *	delimiter found while scanning backwards.
 */
extern int strlrspn(const char *string, size_t offset, const char *delims);

/**
 * Natural string compare.
 *
 * @param s1
 *	A C string.
 *
 * @param s2
 *	A C string.
 *
 * @return
 *	An integer greater than, equal to, or less than 0, according to
 *	whether the string s1 is greater than, equal to, or less than the
 *	string s2 according to natural sorting order.
 */
int strnatcmp(const char *s1, const char *s2);

/**
 * Natural string caseless compare.
 *
 * @param s1
 *	A C string.
 *
 * @param s1
 *	A C string.
 *
 * @return
 *	An integer greater than, equal to, or less than 0, according to
 *	whether the string s1 is greater than, equal to, or less than the
 *	string s2 according to natural sorting order.
 */
int strnatcasecmp(const char *s1, const char *s2);

extern int strnatcmp0(const unsigned char *a, const unsigned char *b, int fold_case);

/**
 * Match or find the first occurence of "needle" in "haystack".
 *
 * @param haystack
 *	A C string to search.
 *
 * @param needle
 *	The C string pattern to match. An astrisk (*) acts as wildcard,
 *	scanning over zero or more bytes. A question-mark (?) matches
 *	any single character.
 *
 *	"abc"		exact match for "abc"
 *
 *	"abc*"		match "abc" at start of string
 *
 *	"*abc"		match "abc" at the end of string
 *
 *	"abc*def"	match "abc" at the start and match "def"
 *			at the end, maybe with stuff in between.
 *
 *	"*abc*def*"	find "abc", then find "def"
 *
 * @param hay_size
 *	How much of haystack to search or -1 for the maximum size or
 *	until a null byte is found.
 *
 * @param caseless
 *	Set true for case insensitive matching.
 *
 * @return
 *	True if a match was found.
 */
extern int TextMatch(const char *haystack, const char *needle, long hay_size, int caseless);

/**
 * Find the first occurence of "needle" in "haystack".
 *
 * @param haystack
 *	A C string to search.
 *
 * @param needle
 *	The C string pattern to find.
 *
 *	An astrisk (*) acts as wildcard, scanning over zero or more
 *	bytes. A question-mark (?) matches any single character; a
 *	space ( ) will match any single white space character.
 *
 *	A left square bracket ([) starts a character class that ends
 *	with a right square bracket (]) and matches one character from
 *	the class. If the first character of the class is a carat (^),
 *	then the remainder of character class is negated. If the first
 *	character (after a carat if any) is a right square bracket, then
 *	the right square bracket is a literal and loses any special
 *	meaning. If the first character (after a carat and/or right
 *	square bracket) is a hypen (-), then the hyphen is a literal and
 *	loses any special meaning. A range expression expressed as a
 *	start character followed by a hyphen followed by an end
 *	character matches a character in character-set order between
 *	start and end characters inclusive.
 *
 *	A backslash followed by any character treats that character as a
 *	literal (it loses any special meaning).
 *
 *	(If you need more than that, think about using regex(3) instead.)
 *
 *	"abc"		exact match for "abc"
 *
 *	"abc*"		match "abc" at start of string
 *
 *	"*abc"		match "abc" at the end of string
 *
 *	"abc*def"	match "abc" at the start and match "def"
 *			at the end, maybe with stuff in between.
 *
 *	"*abc*def*"	find "abc", then find "def"
 *
 *	"a[]]c"		exact match for "a]c", same as "a]c"
 *
 *	"[abc]"		match a single "a", "b", or "c".
 *
 *	"[^abc]"	match a single charcater except "a", "b", or "c".
 *
 *	"[a-z]"		match a single character "a" through "z" (assumes ASCII)
 *
 *	"[0-9]"		match a single digit "0" through "9" (assumes ASCII)
 *
 *	"[-ac]"		match a single charcater "-", "a", or "c".
 *
 *	"[]-ac]		match a single charcater "]", "-", "a", or "c".
 *
 *	"[^-ac]"	match a single charcater except "-", "a", or "c".
 *
 *	"[^]-ac]	match a single charcater execpt "]", "-", "a", or "c".
 *
 * @param hay_size
 *	How much of haystack to search or -1 for the maximum size or
 *	until a null byte is found.
 *
 * @param caseless
 *	Set true for case insensitive matching.
 *
 * @return
 *	Offset into haystack or -1 if not found.
 */
extern long TextFind(const char *haystack, const char *needle, long hay_size, int caseless);

extern long TextFindQuote(const char *string, char *buffer, size_t size);

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_util_Text_h__ */
