/*
 * ipinclient.c
 *
 * Copyright 2006 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 ***
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <com/snert/lib/net/network.h>
#include <com/snert/lib/util/Text.h>

/***********************************************************************
 ***
 ***********************************************************************/

#ifdef OLD
/*
 * This is a strstr() variant.
 */
static const char *
find_substr(const char *string, const char *pat)
{
	const char *p, *c, *d;

	for (c = string; *c != '\0'; c++) {
		for (d = c, p = pat; *p != '\0' && *d != '\0'; p++, d++) {
			/* Ignore punctuation or alpha in the string. */
			if (!isxdigit(*d) && isxdigit(*p)) {
				p--;
				continue;
			}

			/* Match puntuation position in the string and pattern. */
			if (ispunct(*d) && ispunct(*p))
				continue;

			if (*p != *d)
				break;
		}

		if (*p == '\0')
			return c;
	}

	return NULL;
}

#else
/*
 * This is a strstr() variant.
 */
static const char *
find_substr(const char *string, const char *pat)
{
	const char *p, *c, *d;

	for (c = string; (c = strchr(c, *pat)) != NULL; c++) {
		for (d = c+1, p = pat+1; *p != '\0' && *d != '\0'; p++, d++) {
			/* Only ignore punctuation in the string. Do NOT
			 * ignore non-hex alpha in the string. Consider
			 *
			 * 193.190.238.226	smtp-out.west-vlaanderen.be
			 *
			 * Bytes 2 & 1 (eebe) would incorrectly match the
			 * substring "eren.be", ignoring the 'r' and 'n' in
			 * the string.
			 */
			if (ispunct(*d) && isxdigit(*p)) {
				p--;
				continue;
			}

			/* Match puntuation position in the string and pattern. */
			if (ispunct(*d) && ispunct(*p))
				continue;

			if (*p != *d)
				break;
		}

		if (*p == '\0')
			return c;
	}

	return NULL;
}
#endif


static const char *
find_octets(const char *client_name, const char *fmtnum, const char *fmtshort, const char *fmtpair, unsigned char *ipv4)
{
	char buffer[20];
	const char *sub;
	unsigned short pair;

	/* Look for the number as a whole. */
	if (fmtnum != NULL) {
		snprintf(buffer, sizeof (buffer), fmtnum, networkGetLong(ipv4));
		if ((sub = find_substr(client_name, buffer)) != NULL)
			return sub;
	}

	/* Look for the IPv4 address by 16-bit words 12-34, 1-2-34, 12-3-4. */
	if (fmtshort != NULL) {
		/* Get the leading pair and make sure we have a number
		 * greater than 255. This avoid possible mismatches
		 * for something like 0.128.c.d, where the high order
		 * zero could cause a mismatch.
		 */
		pair = networkGetShort(ipv4);
		if (255 < pair) {
			snprintf(buffer, sizeof (buffer), fmtshort, pair);
			if ((sub = find_substr(client_name, buffer)) != NULL)
				return sub;
		}

		/* Get the tailing pair and make sure we have a number
		 * greater than 255. This avoid possible mismatches
		 * for something like a.b.0.128, where the high order
		 * zero could cause a mismatch.
		 */
		pair = networkGetShort(ipv4+2);
		if (255 < pair) {
			snprintf(buffer, sizeof (buffer), fmtshort, pair);
			if ((sub = find_substr(client_name, buffer)) != NULL)
				return sub;
		}
	}

	/* Look for the IP address by byte pairs 1-2, 2-3, 3-4. */
	snprintf(buffer, sizeof (buffer), fmtpair, ipv4[0], ipv4[1]);
	if ((sub = find_substr(client_name, buffer)) != NULL)
		return sub;

	snprintf(buffer, sizeof (buffer), fmtpair, ipv4[1], ipv4[2]);
	if ((sub = find_substr(client_name, buffer)) != NULL)
		return sub;

	snprintf(buffer, sizeof (buffer), fmtpair, ipv4[2], ipv4[3]);
	if ((sub = find_substr(client_name, buffer)) != NULL)
		return sub;

	/* Look for the IP address by byte pairs 4-3, 3-2, 2-1 */
	snprintf(buffer, sizeof (buffer), fmtpair, ipv4[3], ipv4[2]);
	if ((sub = find_substr(client_name, buffer)) != NULL)
		return sub;

	snprintf(buffer, sizeof (buffer), fmtpair, ipv4[2], ipv4[1]);
	if ((sub = find_substr(client_name, buffer)) != NULL)
		return sub;

	snprintf(buffer, sizeof (buffer), fmtpair, ipv4[1], ipv4[0]);
	if ((sub = find_substr(client_name, buffer)) != NULL)
		return sub;

	return NULL;
}

int
isIPv4InName(const char *client_name, unsigned char *ipv4, const char **black, const char **white)
{
	int octet;
	char buffer[10];
	const char *sub, **pat;

	if (white != NULL) {
		for (pat = white; *pat != NULL; pat++) {
			if (TextMatch(client_name, *pat, -1, 1))
				return 0;
		}
	}

	if (black != NULL) {
		for (pat = black; *pat != NULL; pat++) {
			if (TextMatch(client_name, *pat, -1, 1))
				return 13;
		}
	}

	/* Look the IP address in the client_name in assorted
	 * number bases and formats.
	 */
	if (find_octets(client_name, "%08x", "%hx", "%02x%02x", ipv4) != NULL)
		return 1;
	if (find_octets(client_name, "%u", "%hu", "%03u%03u", ipv4) != NULL)
		return 2;
	if (find_octets(client_name, "%o", "%ho", "%03o%03o", ipv4) != NULL)
		return 3;

	if (find_octets(client_name, NULL, NULL, "%02x-%02x", ipv4) != NULL)
		return 10;

	/* Previous this used to be "%d%d" which could be problematic.
	 * It would be extremely rare to find two decimal numbers without
	 * leading zeros concatenated together. So have opted to match
	 * only with separating punctuation.
	 */
	if (find_octets(client_name, NULL, NULL, "%u-%u", ipv4) != NULL)
		return 11;

	if (find_octets(client_name, NULL, NULL, "%03o-%03o", ipv4) != NULL)
		return 12;

	/* Host name starts with the least significant octet. */
	if (isxdigit(client_name[0]) && isxdigit(client_name[1]) && !isxdigit(client_name[2])) {
		sscanf(client_name, "%02x", &octet);
		if (octet == ipv4[3])
			return 5;
	}

	if (isdigit(*client_name)) {
		sscanf(client_name, "%03d", &octet);
		if (octet == ipv4[3])
			return 6;
	}

	/* Least significant octet zero padded and bracketed by
	 * punctuation /[-_.]\d{3}[-_.]/.
	 */
	snprintf(buffer, sizeof (buffer), "-%03d.", ipv4[3]);
	if (find_substr(client_name, buffer) != NULL)
		return 7;

	/* Least significant octet and bracketed by punctuation
	 * /[-_.]\d{1,3}[-_.]/.
	 */
	snprintf(buffer, sizeof (buffer), "-%d.", ipv4[3]);
	if (find_substr(client_name, buffer) != NULL)
		return 8;

	/* Look for something like 241net98.net.zeork.com.pl [194.117.241.98].
	 * Take care NOT to match ns1.ipandmore.de [213.252.1.1].
	 */
	snprintf(buffer, sizeof (buffer), "%d", ipv4[2]);
	if ((sub = find_substr(client_name, buffer)) != NULL) {
		snprintf(buffer, sizeof (buffer), "%d", ipv4[3]);
		if (find_substr(sub+1, buffer) != NULL)
			return 9;
	}

	return 0;
}

int
isIPv4InClientName(const char *client_name, unsigned char *ipv4)
{
	return isIPv4InName(client_name, ipv4, NULL, NULL);
}

#ifdef TEST

#include <com/snert/lib/util/Text.h>

int
main(int argc, char **argv)
{
	int rc;
	unsigned char ipv6[IPV6_BYTE_SIZE];
	char client_addr[IPV6_STRING_SIZE+2], client_name[DOMAIN_SIZE];

	printf("Enter IP and host name pairs:\n");
	while (!feof(stdin)) {
		if (fscanf(stdin, "%41s %255s", client_addr, client_name) != 2)
			break;

		TextLower(client_name, -1);
		parseIPv6(client_addr, ipv6);

		rc = isIPv4InClientName(client_name, ipv6+IPV6_OFFSET_IPV4);
		printf("%s\t%d\t%s\t%s\t\n", rc ? "DYNAMIC\t" : "\t", rc, client_addr, client_name);
	}

	return 0;
}

#endif
