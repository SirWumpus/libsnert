/* -*- mode: c; c-file-style: "k&r" -*-

  strnatcmp.c -- Perform 'natural order' comparisons of strings in C.
  Copyright (C) 2000, 2004 by Martin Pool <mbp sourcefrog net>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.

  Originally from http://sourcefrog.net/projects/natsort/

	2008-2-11 Anthony C Howe <achowe at snert dot com>:

  	Fixes for ANSI C portability. The ctype functions take an "int",
  	NOT an "unisgned int" as claimed by Eric Sosman based on a comment
  	in the code (since removed). See ANSI C 89 section 4.3:

  	"In all cases the argument is an ''int'', the value of which shall be
  	representable as an ''unsigned char'' or shall equal the value of the
  	macros EOF. If the argument has any other value, the behaviour of is
  	undefined."
*/

#include <ctype.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

static int
compare_right(const unsigned char *a, const unsigned char *b)
{
     int bias = 0;

     /* The longest run of digits wins.  That aside, the greatest
	value wins, but we can't know that it will until we've scanned
	both numbers to know that they have the same magnitude, so we
	remember it in BIAS. */
     for (;; a++, b++) {
	  if (!isdigit(*a)  &&  !isdigit(*b))
	       return bias;
	  else if (!isdigit(*a))
	       return -1;
	  else if (!isdigit(*b))
	       return +1;
	  else if (*a < *b) {
	       if (!bias)
		    bias = -1;
	  } else if (*a > *b) {
	       if (!bias)
		    bias = +1;
	  } else if (!*a  &&  !*b)
	       return bias;
     }

     return 0;
}


static int
compare_left(const unsigned char *a, const unsigned char *b)
{
     /* Compare two left-aligned numbers: the first to have a
        different value wins. */
     for (;; a++, b++) {
	  if (!isdigit(*a)  &&  !isdigit(*b))
	       return 0;
	  else if (!isdigit(*a))
	       return -1;
	  else if (!isdigit(*b))
	       return +1;
	  else if (*a < *b)
	       return -1;
	  else if (*a > *b)
	       return +1;
     }

     return 0;
}


int
strnatcmp0(const unsigned char *a, const unsigned char *b, int fold_case)
{
     int ai, bi;
     unsigned char  ca, cb;
     int fractional, result;

     assert(a && b);
     ai = bi = 0;
     while (1) {
	  ca = a[ai]; cb = b[bi];

	  /* skip over leading spaces or zeros */
	  while (isspace(ca))
	       ca = a[++ai];

	  while (isspace(cb))
	       cb = b[++bi];

	  /* process run of digits */
	  if (isdigit(ca)  &&  isdigit(cb)) {
	       fractional = (ca == '0' || cb == '0');

	       if (fractional) {
		    if ((result = compare_left(a+ai, b+bi)) != 0)
			 return result;
	       } else {
		    if ((result = compare_right(a+ai, b+bi)) != 0)
			 return result;
	       }
	  }

	  if (!ca && !cb) {
	       /* The strings compare the same.  Perhaps the caller
                  will want to call strcmp to break the tie. */
	       return 0;
	  }

	  if (fold_case) {
	       ca = toupper(ca);
	       cb = toupper(cb);
	  }

	  if (ca < cb)
	       return -1;
	  else if (ca > cb)
	       return +1;

	  ++ai; ++bi;
     }
}



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
int strnatcmp(const char *s1, const char *s2) {
     return strnatcmp0((const unsigned char *) s1, (const unsigned char *) s2, 0);
}


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
int strnatcasecmp(const char *s1, const char *s2) {
     return strnatcmp0((const unsigned char *) s1, (const unsigned char *) s2, 1);
}
