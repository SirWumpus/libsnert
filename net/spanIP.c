/*
 * spanIP.c
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
#include <com/snert/lib/util/Text.h>

/*
 * RFC 2821 section 4.1.3 IPv4 address literals
 *
 * @param ip
 *	A pointer to a C string that starts with an IPv4 address.
 *
 * @return
 *	The length of the IPv4 address string upto, but excluding, the
 *	first invalid character following it; otherwise zero (0) for a
 *	parse error.
 */
int
spanIPv4(const char *ip)
{
	int dots;
	long octet;
	const char *start, *stop;

	if (ip == NULL)
		return 0;

	dots = 0;

	for (start = ip; *ip != '\0'; ip = stop) {
		octet = strtol(ip, (char **) &stop, 10);

		/* Did we advance? */
		if (ip == stop)
			break;

		/* The octet must be between 0..255. */
		if (octet < 0 || 255 < octet)
			return 0;

		/* Count the dot separators. */
		if (*stop == '.') {
			++stop;
			++dots;
		}
	}

	/* An IPv4 address must have only three dot delimiters. */
	if (dots != 3)
		return 0;

	return ip - start;
}

/*
 * RFC 2821 section 4.1.3 IPv6 address literals
 *
 * Validate the characters and syntax.
 *
 *	IPv4-address-literal = Snum 3("." Snum)
 *	IPv6-address-literal = "IPv6:" IPv6-addr
 *	General-address-literal = Standardized-tag ":" 1*dcontent
 *	Standardized-tag = Ldh-str
 *		; MUST be specified in a standards-track RFC
 *		; and registered with IANA
 *
 *	Snum = 1*3DIGIT  ; representing a decimal integer
 *		; value in the range 0 through 255
 *
 *	Let-dig = ALPHA / DIGIT
 *	Ldh-str = *( ALPHA / DIGIT / "-" ) Let-dig
 *
 *	IPv6-addr = IPv6-full / IPv6-comp / IPv6v4-full / IPv6v4-comp
 *	IPv6-hex  = 1*4HEXDIG
 *	IPv6-full = IPv6-hex 7(":" IPv6-hex)
 *	IPv6-comp = [IPv6-hex *5(":" IPv6-hex)] "::" [IPv6-hex *5(":" IPv6-hex)]
 *		; The "::" represents at least 2 16-bit groups of zeros
 *		; No more than 6 groups in addition to the "::" may be
 *		; present
 *
 *	IPv6v4-full = IPv6-hex 5(":" IPv6-hex) ":" IPv4-address-literal
 *	IPv6v4-comp = [IPv6-hex *3(":" IPv6-hex)] "::"
 *		      [IPv6-hex *3(":" IPv6-hex) ":"] IPv4-address-literal
 *		; The "::" represents at least 2 16-bit groups of zeros
 *		; No more than 4 groups in addition to the "::" and
 *		; IPv4-address-literal may be present
 *
 * @param ip
 *	A pointer to a C string that starts with an IPv6 address.
 *
 * @return
 *	The length of the IPv6 address string upto, but excluding, the
 *	first invalid character following it; otherwise zero (0) for a
 *	parse error.
 */
int
spanIPv6(const char *ip)
{
	long length, word;
	int groups, compressed;
	const char *start, *stop;

	if (ip == NULL)
		return 0;

	for (start = ip, compressed = 0, groups = 0; ; ip = stop + 1) {
		word = strtol(ip, (char **) &stop, 16);
		if (word < 0 || 0xffff < word)
			return 0;

		if (ip < stop && *stop != '.')
			groups++;

		if (*stop != ':')
			break;

		if (stop[1] == ':') {
			if (compressed)
				return 0;
			compressed = 1;
		}
	}

	/* IPv6v4-full, IPv6v4-comp */
	if (*stop == '.') {
		if (compressed && 4 < groups)
			return 0;
		if (!compressed && 6 < groups)
			return 0;

		length = spanIPv4(ip);
		if (length <= 0)
			return 0;

		return ip - start + length;
	}

	/* IPv6-full */
	if (!compressed && groups == 8)
		return stop - start;

	/* IPv6-comp */
	if (compressed && groups <= 6)
		return stop - start;

	return 0;
}

/**
 * RFC 2821 section 4.1.3 IP address literals
 *
 * @param ip
 *	A pointer to a C string that starts with an IPv4 or IPv6 address.
 *
 * @return
 *	The length of the IPv6 address string upto, but excluding, the
 *	first invalid character following it; otherwise zero (0) for a
 *	parse error.
 */
int
spanIP(const char *ip)
{
	int span;

	if (ip == NULL)
		return 0;

	if (0 < TextInsensitiveStartsWith(ip, "IPv6:"))
		return spanIPv6(ip + sizeof ("IPv6:") - 1);

	if (0 < (span = spanIPv6(ip)))
		return span;

	return spanIPv4(ip);
}
