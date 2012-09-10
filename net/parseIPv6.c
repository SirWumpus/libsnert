/*
 * parseIPv6.c
 *
 * Copyright 2002, 2006 by Anthony Howe. All rights reserved.
 */


/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <com/snert/lib/util/Text.h>
#include <com/snert/lib/net/network.h>

/**
 * RFC 2821 section 4.1.3 Address Literals
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
 *	An IPv4, IPv6, or an IP-as-domain literal string.
 *
 * @param ipv6
 *	A buffer to save the parsed IP address in IPv6 network byte order.
 *
 * @return
 *	The length of the IP address string parsed; zero if nothing parsed.
 */
int
parseIPv6(const char *ip, unsigned char ipv6[IPV6_BYTE_LENGTH])
{
	long a, b, c, d, word;
	const char *start, *stop;
	int groups, compressed, offset, mark;

	start = ip;

	if (ip == NULL || ipv6 == NULL) {
		errno = EFAULT;
		return 0;
	}

	if (*ip == '[')
		ip++;

	if (TextInsensitiveCompareN(ip, IPV6_TAG, IPV6_TAG_LENGTH) == 0)
		ip += IPV6_TAG_LENGTH;

	mark = offset = 0;
	for (compressed = 0, groups = 0; groups < 8; ip = stop + 1) {
		word = strtol(ip, (char **) &stop, 16);
		if (word < 0 || 0xffff < word)
			return 0;

		if (ip < stop && *stop != '.') {
			ipv6[offset++] = (word & 0xff00) >> 8;
			ipv6[offset++] = (word & 0x00ff);
			groups++;
		}

		if (*stop != ':')
			break;

		if (stop[1] == ':') {
			if (compressed)
				return 0;
			compressed = 1;
			mark = offset;
		}
	}

	/* Shift the tail of the IPv6 address to right end of the buffer. */
	memmove(ipv6+(IPV6_BYTE_LENGTH-(*stop == '.')*4-offset+mark), ipv6+mark, offset-mark);

	/* Fill in the compressed zeros. */
	memset(ipv6+mark, 0, IPV6_BYTE_LENGTH-(*stop == '.')*4 - offset);

	/* IPv6v4-full, IPv6v4-comp */
	if (*stop == '.') {
		if (compressed && 4 < groups)
			return 0;
		if (!compressed && 6 < groups)
			return 0;

		a = strtol(ip, (char **) &ip, 10);
		if (a < 0 || 255 < a || *ip != '.')
			return 0;
		b = strtol(ip+1, (char **) &ip, 10);
		if (b < 0 || 255 < b || *ip != '.')
			return 0;
		c = strtol(ip+1, (char **) &ip, 10);
		if (c < 0 || 255 < c || *ip != '.')
			return 0;
		d = strtol(ip+1, (char **) &ip, 10);
		if (d < 0 || 255 < d)
			return 0;

		ipv6[12] = (unsigned char) a;
		ipv6[13] = (unsigned char) b;
		ipv6[14] = (unsigned char) c;
		ipv6[15] = (unsigned char) d;

		return ip - start + (*ip == ']');
	}

	/* IPv6-full */
	if (!compressed && groups == 8)
		return stop - start + (*stop == ']');

	/* IPv6-comp */
	if (compressed && groups <= 7)
		return stop - start + (*stop == ']');

	return 0;
}

