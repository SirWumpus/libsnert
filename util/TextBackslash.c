/*
 * TextBackslash.c
 *
 * Copyright 2001, 2005 by Anthony Howe. All rights reserved.
 */

#include <com/snert/lib/util/Text.h>

/***********************************************************************
 *** 
 ***********************************************************************/

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
int
TextBackslash(char ch)
{
	switch (ch) {
	case 'a': return '\007';
	case 'b': return '\010';
	case 'e': return '\033';
	case 'f': return '\014';
	case 'n': return '\012';
	case 'r': return '\015';
	case 's': return '\040';
	case 't': return '\011';
	case 'v': return '\013';
	}

	return ch;
}

