/*
 * spanLocalPart.c
 *
 * Copyright 2010 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <com/snert/lib/net/network.h>
#include <com/snert/lib/util/Text.h>

/*
 * RFC 2821 section 4.1.2 Local-part and RFC 2822 section 3.2.4 Atom
 *
 * Validate only the characters.
 *
 *    Local-part = Dot-string / Quoted-string
 *    Dot-string = Atom *("." Atom)
 *    Atom = 1*atext
 *    Quoted-string = DQUOTE *qcontent DQUOTE
 *
 * @param s
 *	Start of the local-part of a mailbox.
 *
 * @return
 *	The length of the local-part upto, but excluding, the first
 *	invalid character.
 */
int
spanLocalPart(const unsigned char *s)
{
	const unsigned char *t;
	/*@-type@*/
	char x[2] = { 0, 0 };
	/*@-type@*/

	if (*s == '"') {
		/* Quoted-string = DQUOTE *qcontent DQUOTE */
		for (t = s+1; *t != '\0' && *t != '"'; t++) {
			switch (*t) {
			case '\\':
				if (t[1] != '\0')
					t++;
				break;
			case '\t':
			case '\r':
			case '\n':
			case '#':
				return t - s;
			}
		}

		if (*t == '"')
			t++;

		return t - s;
	}

	/* Dot-string = Atom *("." Atom) */
	for (t = s; *t != '\0'; t++) {
		if (isalnum(*t))
			continue;

		if (*t == '\\' && t[1] != '\0') {
			t++;
			continue;
		}

		/* atext does not include '.', but we do here to
		 * simplify scanning dot-atom.
		 */
		*x = *t;
		if (strspn(x, "!#$%&'*+-/=?^_`{|}~.") != 1)
			break;
	}

	return t - s;
}

