/*
 * htmlEntity.c
 *
 * Copyright 2010 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#define _REENTRANT	1

#include <com/snert/lib/version.h>

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <com/snert/lib/util/html.h>

/***********************************************************************
 ***
 ***********************************************************************/

#define HTML_ENTITY_SHY		0xAD

struct mapping {
	const char *entity;
	size_t length;
	int ch;
};

static struct mapping entities[] = {
	{ "&lt;",	sizeof ("&lt;")-1,	'<'	},
	{ "&gt;",	sizeof ("&gt;")-1,	'>'	},
	{ "&amp;",	sizeof ("&amp;")-1,	'&'	},
	{ "&quot;",	sizeof ("&quot;")-1,	'"'	},
	{ "&apos;",	sizeof ("&apos;")-1,	'\''	},

	/* ISO-8859-1 character set */
	{ "&euro;",	sizeof ("&euro;")-1,	0x80	},
	{ "&nbsp;",	sizeof ("&nbsp;")-1,	0xA0	},
	{ "&iexcl;",	sizeof ("&iexcl;")-1,	0xA1	},
	{ "&cent;",	sizeof ("&cent;")-1,	0xA2	},
	{ "&pound;",	sizeof ("&pound;")-1,	0xA3	},
	{ "&curren;",	sizeof ("&curren;")-1,	0xA4	},
	{ "&yen;",	sizeof ("&yen;")-1,	0xA5	},
	{ "&brvbar;",	sizeof ("&brvbar;")-1,	0xA6	},
	{ "&sect;",	sizeof ("&sect;")-1,	0xA7	},
	{ "&uml;",	sizeof ("&uml;")-1,	0xA8	},
	{ "&copy;",	sizeof ("&copy;")-1,	0xA9	},
	{ "&ordf;",	sizeof ("&ordf;")-1,	0xAA	},
	{ "&laquo;",	sizeof ("&laquo;")-1,	0xAB	},
	{ "&not;",	sizeof ("&not;")-1,	0xAC	},
	{ "&shy;",	sizeof ("&shy;")-1,	0xAD 	},
	{ "&reg;",	sizeof ("&reg;")-1,	0xAE	},
	{ "&macr;",	sizeof ("&macr;")-1,	0xAF	},
	{ "&deg;",	sizeof ("&deg;")-1,	0xB0	},
	{ "&plusmn;",	sizeof ("&plusmn;")-1,	0xB1	},
	{ "&sup2;",	sizeof ("&sup2;")-1,	0xB2	},
	{ "&sup3;",	sizeof ("&sup3;")-1,	0xB3	},
	{ "&acute;",	sizeof ("&acute;")-1,	0xB4	},
	{ "&micro;",	sizeof ("&micro;")-1,	0xB5	},
	{ "&para;",	sizeof ("&para;")-1,	0xB6	},
	{ "&middot;",	sizeof ("&middot;")-1,	0xB7	},
	{ "&cedil;",	sizeof ("&cedil;")-1,	0xB8	},
	{ "&sup1;",	sizeof ("&sup1;")-1,	0xB9	},
	{ "&ordm;",	sizeof ("&ordm;")-1,	0xBA	},
	{ "&raquo;",	sizeof ("&raquo;")-1,	0xBB	},
	{ "&frac14;",	sizeof ("&frac14;")-1,	0xBC	},
	{ "&frac12;",	sizeof ("&frac12;")-1,	0xBD	},
	{ "&frac34;",	sizeof ("&frac34;")-1,	0xBE	},
	{ "&iquest;",	sizeof ("&iquest;")-1,	0xBF	},
	{ "&Agrave;",	sizeof ("&Agrave;")-1,	0xC0	},
	{ "&Aacute;",	sizeof ("&Aacute;")-1,	0xC1	},
	{ "&Acirc;",	sizeof ("&Acirc;")-1,	0xC2	},
	{ "&Atilde;",	sizeof ("&Atilde;")-1,	0xC3	},
	{ "&Auml;",	sizeof ("&Auml;")-1,	0xC4	},
	{ "&Aring;",	sizeof ("&Aring;")-1,	0xC5	},
	{ "&AElig;",	sizeof ("&AElig;")-1,	0xC6	},
	{ "&Ccedil;",	sizeof ("&Ccedil;")-1,	0xC7	},
	{ "&Egrave;",	sizeof ("&Egrave;")-1,	0xC8	},
	{ "&Eacute;",	sizeof ("&Eacute;")-1,	0xC9	},
	{ "&Ecirc;",	sizeof ("&Ecirc;")-1,	0xCA	},
	{ "&Euml;",	sizeof ("&Euml;")-1,	0xCB	},
	{ "&Igrave;",	sizeof ("&Igrave;")-1,	0xCC	},
	{ "&Iacute;",	sizeof ("&Iacute;")-1,	0xCD	},
	{ "&Icirc;",	sizeof ("&Icirc;")-1,	0xCE	},
	{ "&Iuml;",	sizeof ("&Iuml;")-1,	0xCF	},
	{ "&eth;",	sizeof ("&eth;")-1,	0xD0	},
	{ "&Ntilde;",	sizeof ("&Ntilde;")-1,	0xD1	},
	{ "&Ograve;",	sizeof ("&Ograve;")-1,	0xD2	},
	{ "&Oacute;",	sizeof ("&Oacute;")-1,	0xD3	},
	{ "&Ocirc;",	sizeof ("&Ocirc;")-1,	0xD4	},
	{ "&Otilde;",	sizeof ("&Otilde;")-1,	0xD5	},
	{ "&Ouml;",	sizeof ("&Ouml;")-1,	0xD6	},
	{ "&times;",	sizeof ("&times;")-1,	0xD7	},
	{ "&Oslash;",	sizeof ("&Oslash;")-1,	0xD8	},
	{ "&Ugrave;",	sizeof ("&Ugrave;")-1,	0xD9	},
	{ "&Uacute;",	sizeof ("&Uacute;")-1,	0xDA	},
	{ "&Ucirc;",	sizeof ("&Ucirc;")-1,	0xDB	},
	{ "&Uuml;",	sizeof ("&Uuml;")-1,	0xDC	},
	{ "&Yacute;",	sizeof ("&Yacute;")-1,	0xDD	},
	{ "&THORN;",	sizeof ("&THORN;")-1,	0xDE	},
	{ "&szlig;",	sizeof ("&szlig;")-1,	0xDF	},
	{ "&agrave;",	sizeof ("&agrave;")-1,	0xE0	},
	{ "&aacute;",	sizeof ("&aacute;")-1,	0xE1	},
	{ "&acirc;",	sizeof ("&acirc;")-1,	0xE2	},
	{ "&atilde;",	sizeof ("&atilde;")-1,	0xE3	},
	{ "&auml;",	sizeof ("&auml;")-1,	0xE4	},
	{ "&aring;",	sizeof ("&aring;")-1,	0xE5	},
	{ "&aelig;",	sizeof ("&aelig;")-1,	0xE6	},
	{ "&ccedil;",	sizeof ("&ccedil;")-1,	0xE7	},
	{ "&egrave;",	sizeof ("&egrave;")-1,	0xE8	},
	{ "&eacute;",	sizeof ("&eacute;")-1,	0xE9	},
	{ "&ecirc;",	sizeof ("&ecirc;")-1,	0xEA	},
	{ "&euml;",	sizeof ("&euml;")-1,	0xEB	},
	{ "&igrave;",	sizeof ("&igrave;")-1,	0xEC	},
	{ "&iacute;",	sizeof ("&iacute;")-1,	0xED	},
	{ "&icirc;",	sizeof ("&icirc;")-1,	0xEE	},
	{ "&iuml;",	sizeof ("&iuml;")-1,	0xEF	},
	{ "&eth;",	sizeof ("&eth;")-1,	0xF0	},
	{ "&ntilde;",	sizeof ("&ntilde;")-1,	0xF1	},
	{ "&ograve;",	sizeof ("&ograve;")-1,	0xF2	},
	{ "&oacute;",	sizeof ("&oacute;")-1,	0xF3	},
	{ "&ocirc;",	sizeof ("&ocirc;")-1,	0xF4	},
	{ "&otilde;",	sizeof ("&otilde;")-1,	0xF5	},
	{ "&ouml;",	sizeof ("&ouml;")-1,	0xF6	},
	{ "&divide;",	sizeof ("&divide;")-1,	0xF7	},
	{ "&oslash;",	sizeof ("&oslash;")-1,	0xF8	},
	{ "&ugrave;",	sizeof ("&ugrave;")-1,	0xF9	},
	{ "&uacute;",	sizeof ("&uacute;")-1,	0xFA	},
	{ "&ucirc;",	sizeof ("&ucirc;")-1,	0xFB	},
	{ "&uuml;",	sizeof ("&uuml;")-1,	0xFC	},
	{ "&yacute;",	sizeof ("&yacute;")-1,	0xFD	},
	{ "&thorn;",	sizeof ("&thorn;")-1,	0xFE	},
	{ "&yuml;",	sizeof ("&yuml;")-1,	0xFF	},
	{ NULL, 	0,			0 	}
};

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
size_t
htmlEntityDecode(const char *source, size_t length, char *buffer, size_t size)
{
	long code;
	char *stop;
	size_t buflen;
	const char *s;
	struct mapping *entry;

	if (size == 0)
		return 0;

	for (buflen = 0, s = source; s - source < length && *s != '\0' && buflen < size; buflen++) {
		if (*s == '&') {
			if (s[1] == '#') {
				if (s[2] == 'x')
					code = strtol(s+3, &stop, 16);
				else
					code = strtol(s+2, &stop, 10);

				if (0 <= code && code < 256 && *stop == ';') {
					if (code == HTML_ENTITY_SHY)
						/* Discard soft-hyphen &shy; */
						buflen--;
					else {
						/* Write the decoded hex byte. */
						*buffer++ = (char) code;
					}
					s += stop - s + 1;
				} else {
					/* Unknown numeric. Copy as is. */
					*buffer++ = *s++;
				}
				continue;
			}

			for (entry = entities; entry->entity != NULL; entry++) {
				if (strncmp(s, entry->entity, entry->length) == 0) {
					if (entry->ch == HTML_ENTITY_SHY)
						/* Discard soft-hyphen &shy; */
						buflen--;
					else {
						/* Replace entity name by byte. */
						*buffer++ = entry->ch;
					}
					s += entry->length;
					break;
				}
			}

			if (entry->entity == NULL) {
				/* No entity by that name. Copy as is. */
				*buffer++ = *s++;
			}
		} else {
			/* Copy as is. */
			*buffer++ = *s++;
		}
	}

	if (buflen < size)
		*buffer = '\0';

	return buflen;
}

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
size_t
htmlEntityEncode(const char *source, size_t length, char *buffer, size_t size)
{
	size_t buflen = 0;
	const char *s = source;
	static const char hex_digit[] = "0123456789ABCDEF";

	if (1 < size) {
		--size;

		while (s - source < length && *s != '\0' && buflen < size) {
			if (*s < 0) {
				if (size <= buflen + 6)
					break;

				*buffer++ = '&';
				*buffer++ = '#';
				*buffer++ = 'x';
				*buffer++ = hex_digit[(*(unsigned char *)s >> 4) & 0x0F];
				*buffer++ = hex_digit[*(unsigned char *)s & 0x0F];
				*buffer++ = ';';
				buflen += 6;
			} else {
				*buffer++ = *s;
				buflen++;
			}
		}

		*buffer = '\0';
	}

	/* If the buffer is too small to encode the string, then
	 * compute the length of the buffer required to encode the
	 * full string.
	 */
	for ( ; 0 < length--; s++) {
		if (*s < 0)
			buflen += 6;
		else
			buflen++;
	}

	return buflen;
}

