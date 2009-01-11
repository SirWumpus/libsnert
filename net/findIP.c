/*
 * findIP.c
 *
 * Copyright 2008 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <com/snert/lib/net/network.h>

/**
 * Find the first occurence of an IPv4 address in a string.
 *
 * @param offsetp
 *	A pointer to an int in which to passback the offset of
 *	the IP address. -1 if not found. offsetp may be NULL.
 *
 * @param spanp
 *	A pointer to an int in which to passback the span of
 *	the IP address. 0 if not found. spanp may be NULL.
 *
 * @return
 *	A pointer to the first occurence of an IP address or
 *	NULL if not found.
 */
const char *
findIPv4(const char *string, int *offsetp, int *spanp)
{
	int span;
	char *delim, *ip;

	/* Search the string looking for a delimiter
	 * found in an IP address...
	 */
	for (delim = (char *) string-1; (delim = strchr(delim+1, '.')) != NULL; ) {
		ip = delim;
		if (string < ip && isdigit(ip[-1])) {
			ip--;
			if (string < ip && isdigit(ip[-1])) {
				ip--;
				if (string < ip && isdigit(ip[-1])) {
					ip--;
				}
			}
		}

		/* When we find the leading part of a possible
		 * IP address, then check if what follows makes
		 * a complete address.
		 */
		if (ip < delim && 0 < (span = spanIPv4(ip))) {
			if (offsetp != NULL)
				*offsetp = ip - string;
			if (spanp != NULL)
				*spanp = span;
			return ip;
		}
	}

	if (offsetp != NULL)
		*offsetp = -1;
	if (spanp != NULL)
		*spanp = 0;

	return NULL;
}

/**
 * Find the first occurence of an IPv6 address in a string.
 *
 * @param offsetp
 *	A pointer to an int in which to passback the offset of
 *	the IP address. -1 if not found. offsetp may be NULL.
 *
 * @param spanp
 *	A pointer to an int in which to passback the span of
 *	the IP address. 0 if not found. spanp may be NULL.
 *
 * @return
 *	A pointer to the first occurence of an IP address or
 *	NULL if not found.
 */
const char *
findIPv6(const char *string, int *offsetp, int *spanp)
{
	int span;
	const char *delim, *ip;

	/* Search the string looking for a delimiter
	 * found in an IP address...
	 */
	for (delim = string-1; (delim = strchr(delim+1, ':')) != NULL; ) {
		ip = delim;
		if (string < ip && isxdigit(ip[-1])) {
			ip--;
			if (string < ip && isxdigit(ip[-1])) {
				ip--;
				if (string < ip && isxdigit(ip[-1])) {
					ip--;
					if (string < ip && isxdigit(ip[-1])) {
						ip--;
					}
				}
			}
		}

		/* When we find the leading part of a possible
		 * IP address, then check if what follows makes
		 * a complete address.
		 */
		if (ip < delim && 0 < (span = spanIPv6(ip))) {
			if (offsetp != NULL)
				*offsetp = ip - string;
			if (spanp != NULL)
				*spanp = span;
			return ip;
		}
	}

	if (offsetp != NULL)
		*offsetp = -1;
	if (spanp != NULL)
		*spanp = 0;

	return NULL;
}

/**
 * Find the first occurence of an IPv6 or IPv4 address in a string.
 *
 * @param string
 *	A C string to search.
 *
 * @param offsetp
 *	A pointer to an int in which to passback the offset of
 *	the IP address. -1 if not found. offsetp may be NULL.
 *
 * @param spanp
 *	A pointer to an int in which to passback the span of
 *	the IP address. 0 if not found. spanp may be NULL.
 *
 * @return
 *	A pointer to the first occurence of an IP address or
 *	NULL if not found.
 */
const char *
findIP(const char *string, int *offsetp, int *spanp)
{
	const char *s6, *s4;
	int offset6, offset4, span6, span4;

	s6 = findIPv6(string, &offset6, &span6);
	s4 = findIPv4(string, &offset4, &span4);

	if (offset6 < 0 || (0 <= offset4 && offset4 < offset6)) {
		if (offsetp != NULL)
			*offsetp = offset4;
		if (spanp != NULL)
			*spanp = span4;

		return s4;
	}

	if (offsetp != NULL)
		*offsetp = offset6;
	if (spanp != NULL)
		*spanp = span6;

	return s6;
}

#ifdef TEST

#include <stdio.h>
#include <com/snert/lib/util/Text.h>

int
main(int argc, char **argv)
{
	int offset, span;
	char ip[IPV6_TAG_LENGTH+IPV6_STRING_LENGTH], *string;

	if (argc <= 1) {
		fprintf(stderr, "usage: findIP string ...\n");
		return 1;
	}

	ip[0] = '\0';
	for (argv++ ; *argv != NULL; argv++) {
		for (string = *argv; findIP(string, &offset, &span) != NULL; string += offset+span) {
			if (sizeof (ip) <= span) {
				printf("span error [%s]\n", string+offset);
				break;
			}
			(void) TextCopy(ip, span+1, string+offset);
			printf("%s\n", ip);
		}
	}

	return ip[0] == '\0';
}

#endif
