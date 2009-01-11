/*
 * MailSpan.c
 *
 * Assorted span functions for validation.
 *
 * Copyright 2002, 2006 by Anthony Howe.  All rights reserved.
 */

#include <ctype.h>
#include <string.h>
#include <stdlib.h>

#include <com/snert/lib/version.h>
#include <com/snert/lib/util/Text.h>
#include <com/snert/lib/mail/MailSpan.h>

/*
 * RFC 2821 section 4.1.3 Address Literals
 *
 * Validate the characters and syntax.
 *
 *	IPv4-address-literal = Snum 3("." Snum)
 *
 *	Snum = 1*3DIGIT         ; representing a decimal integer
 *				; value in the range 0 through 255
 *
 * @param ip
 *	Start of an IPv4 address.
 *
 * @return
 *	The length of the address string upto, but excluding, the
 *	first invalid character. Zero is return if the required
 *	number of dots in the address was not found.
 */
long
MailSpanIPv4(const char *ip)
{
	int dots;
	long octet;
	const char *start, *stop;

	dots = 0;
	for (start = ip; *ip != '\0'; ip = stop) {
		octet = strtol(ip, (char **) &stop, 10);

		/* Must be an octet between 0..255. */
		if (ip == stop)
			break;

		if (octet < 0 || 255 < octet)
			return ip - start;

		/* Count the dot separators. */
		if (*stop == '.') {
			++stop;
			++dots;
		}
	}

	if (dots != 3)
		return 0;

	return ip - start;
}

/*
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
 */
long
MailSpanIPv6(const char *ip)
{
	long length, word;
	int groups, compressed;
	const char *start, *stop;

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

		length = MailSpanIPv4(ip);
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

/*
 * @param s
 *	Start of an IPv4 or IPv6 address literal.
 *
 * @return
 *	The length of the domain upto, but excluding, the first
 *	invalid character. Zero is return if minimum number of
 *	dots were not found or if an address-as-domain literal
 *	was not properly parsed.
 */
long
MailSpanAddressLiteral(const char *s)
{
	const char *t = s;

	if (*t++ != '[')
		return 0;

	if (isdigit(*t))
		t += MailSpanIPv4(t);
	else if (strncmp(t, "ipv6:", 5) == 0 || strncmp(t, "IPv6:", 5) == 0)
		t += MailSpanIPv6(t + 5) + 5;
	else
		return 0;

	if (*t++ != ']')
		return 0;

	return t - s;
}

/*
 * RFC 2821 section 4.1.2 Command Argument Syntax
 *
 * Validate the characters and syntax.
 *
 *     Mailbox = Local-part "@" Domain
 *     Domain = (sub-domain 1*("." sub-domain)) / address-literal
 *     sub-domain = Let-dig [Ldh-str]
 *     Let-dig = ALPHA / DIGIT
 *     Ldh-str = *( ALPHA / DIGIT / "-" ) Let-dig
 *
 * @param s
 *	Start of a domain name.
 *
 * @param minimumDots
 *	Number of minimum dots required to find in the domain.
 *
 * @return
 *	The length of the domain upto, but excluding, the first
 *	invalid character. Zero is return if minimum number of
 *	dots were not found or if an address-as-domain literal
 *	was not properly parsed.
 */
long
MailSpanDomainName(const char *s, int minimumDots)
{
	int dots;
	const char *t;

	if (*s == '[')
		return MailSpanAddressLiteral(s);

	dots = 0;

	for (t = s; *t != '\0'; t++) {
		/* First charcter of a domain segment must be alpha-numeric. */
		if (!isalnum(*t))
			break;

		/* Rest of domain segment can be alpha-numeric or hypen.
		 * RFC 2782 allows underscore as a domain name character.
		 */
		for (t++; *t != '\0'; t++) {
			if (!isalnum(*t) && *t != '-' && *t != '_')
				break;
		}

		/* Something other than the domain segment delimiter? */
		if (*t != '.')
			break;

		/* Last character of domain segment must be alpha-numeric. */
		if (t[-1] == '-') {
			t--;
			break;
		}

		dots++;
	}

	/* 0	simple machine name		local@machine
	 * 1	a domain name			local@domain.tld
	 * 2	fully qualified domain name	local@machine.domain.tld
	 */
	if (dots < minimumDots)
		return 0;

	return t - s;
}

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
long
MailSpanLocalPart(const char *s)
{
	const char *t;
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

long
MailSpanMailbox(const char *s)
{
	long localLength, domainLength;

	localLength = MailSpanLocalPart(s);
	if (s[localLength] != '@')
		return 0;

	domainLength = MailSpanDomainName(s + localLength + 1, 1);
	if (domainLength <= 0)
		return 0;

	return localLength + 1 + domainLength;
}

/*
 * RFC 2821 section 4.1.2 Command Argument Syntax
 *
 * Validate the characters and syntax.
 *
 *	Path = "<" [ A-d-l ":" ] Mailbox ">"
 *	A-d-l = At-domain *( "," A-d-l )
 *		; Note that this form, the so-called "source route",
 *		; MUST BE accepted, SHOULD NOT be generated, and SHOULD be
 *		; ignored.
 *	At-domain = "@" domain
 *	Domain = (sub-domain 1*("." sub-domain)) / address-literal
 *	sub-domain = Let-dig [Ldh-str]
 *	Let-dig = ALPHA / DIGIT
 *	Ldh-str = *( ALPHA / DIGIT / "-" ) Let-dig
 */
long
MailSpanAtDomainList(const char *s)
{
	long length;
	const char *start;

	for (start = s; *s == '@'; s += length) {
		length = MailSpanDomainName(s + 1, 1) + 1;
		if (length == 1)
			return 0;
		if (s[length] == ',')
			length++;
	}

	return s - start;
}

/*
 * RFC 2821 section 4.1.2 Command Argument Syntax
 *
 * Validate the characters and syntax.
 *
 *	Path = "<" [ A-d-l ":" ] Mailbox ">"
 *	A-d-l = At-domain *( "," A-d-l )
 *		; Note that this form, the so-called "source route",
 *		; MUST BE accepted, SHOULD NOT be generated, and SHOULD be
 *		; ignored.
 *	At-domain = "@" domain
 *	Domain = (sub-domain 1*("." sub-domain)) / address-literal
 *	sub-domain = Let-dig [Ldh-str]
 *	Let-dig = ALPHA / DIGIT
 *	Ldh-str = *( ALPHA / DIGIT / "-" ) Let-dig
 */
long
MailSpanPath(const char *s)
{
	long length = 0;

	if (*s == '@') {
		length = MailSpanAtDomainList(s);
		if (s[length] != ':')
			return 0;
		length++;
	}

	return length + MailSpanMailbox(s + length);
}
