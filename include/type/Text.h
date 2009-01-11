/*
 * Text.h
 *
 * Copyright 2001, 2006 by Anthony Howe.  All rights reserved.
 */

#ifndef __com_snert_lib_type_Text_h__
#define __com_snert_lib_type_Text_h__	1

#ifdef __cplusplus
extern "C" {
#endif

#ifndef __com_snert_lib_type_Vector_h__
#include <com/snert/lib/type/Vector.h>
#endif

#ifndef __com_snert_lib_util_Buf_h__
#include <com/snert/lib/util/Buf.h>
#endif

#ifndef BUFSIZ
#include <stdio.h>
#endif

typedef struct text {
	OBJECT_OBJECT;

	/**
	 * Return the base of the C string.
	 *
	 * @param self
	 *	This object.
	 *
	 * @return
	 *	The base of the C string.
	 */
	/*@observer@*//*@notnull@*/ char *(*string)(struct text *self);

	/**
	 * Return the length of the C string.
	 *
	 * @param self
	 *	This object.
	 *
	 * @return
	 *	The length of the C string, excluding the terminating
	 *	null byte.
	 *
	 * @see
	 *	strlen()
	 */
	long (*length)(struct text *self);

	/**
	 * @param self
	 *	This object.
	 *
	 * @return
	 *	True if this string is empty or blank.
	 */
	int (*isBlank)(struct text *self);

	/**
	 * @param self
	 *	This object.
	 *
	 * @param radix
	 *	The integer radix from 2 to 36 inclusive, or 0 to check
	 *	for a C string integer in base 8 (leading 0), 10, or 16
	 *	(leading 0x).
	 *
	 * @return
	 *	True if this string is an integer in the specified radix.
	 */
	int (*isInteger)(struct text *self, int radix);

	/**
	 * @param self
	 *	This object.
	 *
	 * @param index
	 *	The index of the octet to return.
	 *
	 * @return
	 *	The octet at the given index, otherwise -1 if index is
	 *	out of bounds.
	 */
	int (*getOctet)(struct text *self, long index);

	/**
	 * @param self
	 *	This object.
	 *
	 * @param index
	 *	The index of the octet to return.
	 *
	 * @param ch
	 *	The octet to replace the one at the given index.
	 *
	 * @return
	 *	Zero for success, otherwise -1 if index is out of bounds.
	 */
	int (*setOctet)(struct text *self, long index, int ch);

	/**
	 * @param self
	 *	This object.
	 *
	 * @param string
	 *	The string to append to the end of this.
	 *
	 * @return
	 *	Zero for success, otherwise a non-zero number on error.
	 */
	int (*append)(struct text *self, /*@null@*/ struct text *string);
	int (*appendC)(struct text *self, const char *string);

	/**
	 * @param self
	 *	This object.
	 *
	 * @return
	 *	This string's hash code.
	 */
	HashcodeFunction hashcodeCase;
	HashcodeFunction hashcodeIgnoreCase;

	/**
	 * @param self
	 *	This object.
	 *
	 * @param other
	 *	Some other string to compare with
	 *
	 * @return
	 *	True if they are equal.
	 *
	 * @notes
	 *	To compare against a C string:
	 *
	 *		strcmp(text->_string, cstring) == 0
	 *		strcmp(text->string(text), cstring) == 0
	 */
	EqualsFunction equalsCase;
	EqualsFunction equalsIgnoreCase;

	/**
	 * @param self
	 *	This object.
	 *
	 * @param other
	 *	Some other string to compare against.
	 *
	 * @return
	 *	Zero if they are equal, a negative value if this string is
	 *	less than the other, or a positive value if this string is
	 *	greater than the other
	 *
	 * @notes
	 *	To compare against a C string:
	 *
	 *		strcmp(text->_string, cstring)
	 *		strcmp(text->string(text), cstring)
	 */
	CompareFunction compareCase;
	CompareFunction compareIgnoreCase;

	int (*compareCaseC)(struct text *self, const char *other);
	int (*compareIgnoreCaseC)(struct text *self, const char *other);

	/**
	 * @param self
	 *	This object.
	 *
	 * @param other
	 *	Some other string to compare against.
	 *
	 * @param length
	 *	Maximum number of octets to compare.
	 *
	 * @return
	 *	Zero if they are equal, a negative value if this string is
	 *	less than the other, or a positive value if this string is
	 *	greater than the other
	 *
	 * @notes
	 *	To compare against a C string:
	 *
	 *		strncmp(text->_string, cstring, length)
	 *		strncmp(text->string(text), cstring, length)
	 */
	int (*compareCaseN)(void *self, /*@null@*/ void *other, long length);
	int (*compareIgnoreCaseN)(void *self, /*@null@*/ void *other, long length);

	int (*compareCaseNC)(struct text *self, const char *other, long length);
	int (*compareIgnoreCaseNC)(struct text *self, const char *other, long length);

	/**
	 * Return true if the default methods for compare(), equals(), and
	 * hashcode() are case-insensitive comparisons.
	 *
	 * @param self
	 *	This object.
	 *
	 * @return
	 *	True for case-insensitive comparisons.
	 */
	int (*getIgnoreCase)(struct text *self);

	/**
	 * Set the methods for compare(), equals(), and hashcode()
	 * for case-sensistive or case-insensitive comparisons.
	 *
	 * @param self
	 *	This object.
	 *
	 * @param flag
	 *	True for case-insensitive comparisons.
	 */
	void (*setIgnoreCase)(struct text *self, int flag);

	/**
	 * <p>
	 * Find the first occurence of a substring.
	 * </p>
	 *
	 * @param self
	 *	This object.
	 *
	 * @param sub
	 *	The substring to find.
	 *
	 * @return
	 * 	Return -1 on no match, otherwise the offset of the substring.
	 */
	long (*findCase)(struct text *self, struct text *sub);
	long (*findCaseC)(struct text *self, const char *sub);
	long (*findIgnoreCase)(struct text *self, struct text *sub);
	long (*findIgnoreCaseC)(struct text *self, const char *sub);

	/**
	 * <p>
	 * Compare the trailing part of the string matches the given suffix.
	 * </p>
	 *
	 * @param self
	 *	This object.
	 *
	 * @param suffix
	 *	The string suffix to match.
	 *
	 * @return
	 * 	Return -1 on no match, otherwise the length of the matching suffix.
	 */
	long (*endsWithCase)(struct text *self, struct text *suffix);
	long (*endsWithCaseC)(struct text *self, const char *suffix);
	long (*endsWithIgnoreCase)(struct text *self, struct text *suffix);
	long (*endsWithIgnoreCaseC)(struct text *self, const char *suffix);

	/**
	 * <p>
	 * Compare the leading part of the string matches the given prefix.
	 * </p>
	 *
	 * @param self
	 *	This object.
	 *
	 * @param prefix
	 *	The string prefix to match.
	 *
	 * @return
	 * 	Return -1 on no match, otherwise the length of the matching prefix.
	 */
	long (*startsWithCase)(struct text *self, struct text *prefix);
	long (*startsWithCaseC)(struct text *self, const char *prefix);
	long (*startsWithIgnoreCase)(struct text *self, struct text *prefix);
	long (*startsWithIgnoreCaseC)(struct text *self, const char *prefix);

	/**
	 * <p>
	 * The given string contains a list of substrings separated by the
	 * specified delimiter characters. The substrings may contain quoted
	 * strings and/or contain backslash-escaped characters. The common
	 * backslash escape sequences are supported and return their ASCII
	 * values.
	 * </p>
	 *
	 * @param self
	 *	This object.
	 *
	 * @param delims
	 *	A set of delimiter characters.
	 *
	 * @param returnEmptyTokens
	 *	If false then a run of one or more delimeters is treated as a
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
	 * @return
	 *	A vector of Text objects.
	 */
	Vector (*split)(struct text *self, struct text *delims, int returnEmptyTokens);
	Vector (*splitC)(struct text *self, const char *delims, int returnEmptyTokens);

	void (*invertCase)(struct text *self, long index, long length);
	void (*lowerCase)(struct text *self, long index, long length);
	void (*upperCase)(struct text *self, long index, long length);
	void (*reverse)(struct text *self, long index, long length);

	struct text *(*substring)(struct text *self, long offset, long length);

	void (*resetTokens)(struct text *self);
	int (*hasMoreTokens)(struct text *self);

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
	 * @param self
	 *	This object, which may be a quoted string.
	 *
	 * @param delims
	 *	A set of delimiter characters.
	 *
	 * @param returnEmptyToken
	 *	If false then a run of one or more delimeters is treated as a
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
	 * @return
	 *	An allocated token.
	 *
	 * @see #TextBackslash(char)
	 */
	struct text *(*nextToken)(struct text *self, struct text *delims, int returnEmptyTokens);
	struct text *(*nextTokenC)(struct text *self, const char *delims, int returnEmptyTokens);

	/*
	 * Private
	 */
	long _length;
	/*@owned@*//*@notnull@*/ char *_string;
	/*@dependent@*//*@null@*/ char *_nextToken;
} *Text;

/*@-exportlocal@*/

/**
 * Initialise an object. Typically used for static Objects.
 *
 * @param data
 *	A pointer to an object to initialise.
 */
extern void TextInitEmptyString(/*@out@*/  Text self);

/**
 * Initialise an object. Typically used for static Objects.
 *
 * @param data
 *	A pointer to an object to initialise.
 *
 * @param cstring
 *	A C string pointer.
 */
extern void TextInitFromString(/*@out@*/ Text self, const char *cstring);

#if NOT_IMPLEMENTED
/**
 * Create a new and initialised object for an empty string.
 *
 * @return
 *	A new object representing an empty string; otherwise null on error.
 *
 * @notes
 *	Text->string() will return a pointer to a static empty C string.
 */
extern /*@only@*//*@null@*/ Text TextCreateEmptyString(void);
#endif

/**
 * Create a new and initialised Text object. The C string is duplicated
 * and always null terminated.
 *
 * @param cstring
 *	A C string pointer.
 *
 * @return
 *	A new object representing the C string; otherwise null on error.
 *
 * @see
 *	strdup()
 */
extern /*@only@*//*@null@*/ Text TextCreate(/*@unique@*/ const char *cstring);

/**
 * Create a new and initialised Text object. The first length octets of
 * the C string are duplicated and the string null terminated.
 *
 * @param cstring
 *	A C string pointer.
 *
 * @param length
 *	The length of the C sub-string to use.
 *
 * @return
 *	A new object representing the C string; otherwise null on error.
 */
extern /*@only@*//*@null@*/ Text TextCreateN(/*@unique@*/ const char *cstring, long length);

/**
 * <p>
 * Read a line of input until a newline (CRLF or LF) is read or the buffer
 * is filled. The newline is removed from the input and the line is always
 * null terminated.
 * </p>
 *
 * @param fd
 *	The file handle from which to read input.
 *
 * @param max
 *	The maximum line length to read, or -1 to ignore.
 *
 * @return
 *	A new object representing the line of input; otherwise null on error.
 */
extern /*@only@*//*@null@*/ Text TextCreateFromReadLine(int fd, long max);

/**
 * <p>
 * Read a line of input until a newline (CRLF or LF) is read or the buffer
 * is filled. The newline is removed from the input and the line is always
 * null terminated.
 * </p>
 *
 * @param fp
 *	The FILE * from which to read input.
 *
 * @param max
 *	The maximum line length to read, or -1 to ignore.
 *
 * @return
 *	A new object representing the line of input; otherwise null on error.
 */
extern /*@only@*//*@null@*/ Text TextCreateFromInputLine(FILE *fp, long max);

/*
 * Function oriented API.
 */
extern int TextAppend(Text self, /*@null@*/ Text other) /*@modifies self*/;
extern int TextAppendC(Text self, const char *other) /*@modifies self*/;

extern /*@only@*//*@null@*/ void *TextClone(void *self);
extern void TextDestroy(/*@only@*//*@null@*/ void *self);

extern long TextLength(Text self);
extern /*@observer@*//*@notnull@*/ char *TextString(Text self);

extern int TextCompareCase(void *self, /*@null@*/ void *other);
extern int TextCompareCaseN(void *self, /*@null@*/ void *other, long length);
extern int TextCompareIgnoreCase(void *self, /*@null@*/ void *other);
extern int TextCompareIgnoreCaseN(void *self, /*@null@*/ void *other, long length);

extern int TextCompareCaseC(Text self, const char *other);
extern int TextCompareCaseNC(Text self, const char *other, long length);
extern int TextCompareIgnoreCaseC(Text self, const char *other);
extern int TextCompareIgnoreCaseNC(Text self, const char *other, long length);

extern int TextEqualsCase(void *self, /*@null@*/ void *other);
extern int TextEqualsIgnoreCase(void *self, /*@null@*/ void *other);

extern long TextHashcodeCase(void *self);
extern long TextHashcodeIgnoreCase(void *self);

extern long TextEndsWithCase(Text self, Text suffix);
extern long TextEndsWithCaseC(Text self, const char *suffix);
extern long TextEndsWithIgnoreCase(Text self, Text suffix);
extern long TextEndsWithIgnoreCaseC(Text self, const char *suffix);

extern long TextStartsWithCase(Text self, Text prefix);
extern long TextStartsWithCaseC(Text self, const char *prefix);
extern long TextStartsWithIgnoreCase(Text self, Text prefix);
extern long TextStartsWithIgnoreCaseC(Text self, const char *prefix);

extern long TextFindCase(Text self, Text sub);
extern long TextFindCaseC(Text self, const char *sub);
extern long TextFindIgnoreCase(Text self, Text sub);
extern long TextFindIgnoreCaseC(Text self, const char *sub);

extern void TextInvertCase(Text self, long offset, long length);
extern void TextLowerCase(Text self, long offset, long length);
extern void TextReverseRegion(Text self, long offset, long length);
extern void TextUpperCase(Text self, long offset, long length);

extern int TextGetOctet(Text self, long index);
extern int TextGetIgnoreCase(Text self);
extern /*@only@*//*@null@*/ Text TextGetSubstring(Text self, long offset, long length);
extern int TextIsBlank(Text self);
extern int TextIsInt(Text self, int radix);
extern void TextSetIgnoreCase(Text self, int ignoreCase);
extern int TextSetOctet(Text self, long index, int ch);

extern void TextResetTokens(Text self) /*@modifies self@*/;
extern int TextHasMoreTokens(Text self);
extern /*@only@*//*@null@*/ Text TextNextToken(Text self, Text delims, int returnEmptyTokens);
extern /*@only@*//*@null@*/ Text TextNextTokenC(Text self, const char *delims, int returnEmptyTokens);
extern /*@only@*//*@null@*/ Vector TextSplitOn(Text self, Text delims, int returnEmptyTokens);
extern /*@only@*//*@null@*/ Vector TextSplitOnC(Text self, const char *delims, int returnEmptyTokens);

/*@=exportlocal@*/

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_type_Text_h__ */
