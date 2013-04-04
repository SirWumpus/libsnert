/**
 * reverse.c
 *
 * String reversal functions.
 *
 * Copyright 2002, 2007 by Anthony Howe. All rights reserved.
 */

#include <com/snert/lib/version.h>
#include <stdlib.h>
#include <string.h>
#include <com/snert/lib/net/network.h>
#include <com/snert/lib/util/Text.h>

long
reverseSegmentOrder(const char *string, const char *delims, char *buffer, int size)
{
	char *x, *y, ch;
	long length, span;

	if (string == NULL || size <= 0)
		return 0;

	/* Copy string to the buffer. */
	if (size <= (length = TextCopy(buffer, size, (char *) string)))
		return length;

	/* Remove the trailing delimiter if present. */
	if (0 < length && strchr(delims, buffer[length-1]) != NULL)
		buffer[--length] = '\0';

	/* Reverse the entire string to reverse the segment order. */
	for (x = buffer, y = buffer + length; x < --y; x++) {
		ch = *y;
		*y = *x;
		*x = ch;
	}

	/* For each segement, reverse it to restore the substring order. */
	for ( ; 0 < (span = strcspn(buffer, delims)); buffer += span + (buffer[span] != '\0')) {
		for (x = buffer, y = buffer + span; x < --y; x++) {
			ch = *y;
			*y = *x;
			*x = ch;
		}
	}

	return length;
}

long
reverseByNibble(const char *group, char *buffer, int size)
{
	char *stop;
	unsigned short word;
	int i, nibble, length = 0;

	word = (int) strtol(group, &stop, 16);
	if (*stop == ':')
		length = reverseByNibble(stop+1, buffer, size);

	for (i = 0; i < 4; i++) {
		nibble = word & 0xf;
		word >>= 4;
		length += snprintf(buffer+length, size-length, "%x.", nibble);
	}

	return length;
}

long
reverseSegments(const char *source, const char *delims, char *buffer, int size, int arpa)
{
	long length;
	char ip[IPV6_STRING_SIZE];
	unsigned char ipv6[IPV6_BYTE_SIZE];

	if (TextInsensitiveCompareN(source, IPV6_TAG, IPV6_TAG_LENGTH) == 0)
		source += IPV6_TAG_LENGTH;

	if (strchr(source, ':') == NULL) {
		length = reverseSegmentOrder(source, delims, buffer, size);
		if (arpa)
			length += TextCopy(buffer+length, size-length, ".in-addr.arpa.");
	} else {
		/* Is it a compact IPv6 address? */
		if (strstr(source, "::") != NULL) {
			/* Convert to a binary IP address. */
			(void) parseIPv6(source, ipv6);

			/* Convert back to full IPv6 address string. */
			formatIP(ipv6, IPV6_BYTE_SIZE, 0, ip, sizeof (ip));
			source = ip;
		}

		length = reverseByNibble(source, buffer, size);

		/* Remove trailing dot from last nibble. */
		if (buffer[length-1] == '.')
			buffer[--length] = '\0';

		if (arpa)
			length += TextCopy(buffer+length, size-length, ".ip6.arpa.");
	}

	return length;
}


long
reverseIp(const char *source, char *buffer, int size, int arpa)
{
	return reverseSegments(source, ".", buffer, size, arpa);
}

