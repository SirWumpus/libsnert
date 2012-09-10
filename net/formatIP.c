/*
 * formatIP.c
 *
 * RFC 4291 section 2.2
 *
 * Copyright 2002, 2008 by Anthony Howe. All rights reserved.
 */


/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <com/snert/lib/util/Text.h>
#include <com/snert/lib/io/socket2.h>
#include <com/snert/lib/net/network.h>

/**
 * @param ip
 *	An IP address in network byte order.
 *
 * @param ip_length
 *	The length of the IP address, which is either IPV4_BYTE_LENGTH (4)
 *	or IPV6_BYTE_LENGTH (16).
 *
 * @param compact
 *	If true and the ip argument is an IPv6 address, then the compact
 *	IPv6 address form will be written into buffer. Otherwise the full
 *	IP address is written to buffer.
 *
 * @param buffer
 *	The buffer for the IP address string. The buffer is always null
 *	terminated.
 *
 * @param size
 *	The size of the buffer, which should be at least IPV6_STRING_LENGTH.
 *
 * @return
 *	The length of the formatted address, excluding the terminating null
 *	byte if the buffer were of infinite size. If the return value is
 *	greater than or equal to the buffer size, then the contents of the
 *	buffer are truncated.
 */
long
formatIP(unsigned char *ip, int ip_length, int compact, char *buffer, long size)
{
	int i, z;
	long length;
	unsigned word;
	const char *word_fmt;

	if (ip == NULL || buffer == NULL) {
		errno = EFAULT;
		return 0;
	}

	if (ip_length == IPV4_BYTE_LENGTH)
		return snprintf(buffer, size, "%d.%d.%d.%d", ip[0],ip[1],ip[2],ip[3]);

	if (ip_length != IPV6_BYTE_LENGTH)
		return 0;

	length = 0;
	word_fmt = compact == 2 ? "%04x" : "%x";

	z = 0;
	for (i = 0; i < IPV6_BYTE_LENGTH; i += 2) {
		word = NET_GET_SHORT(&ip[i]);

		/* Count leading zero words. */
		if (word == 0 && (i >> 1) == z)
			z++;

		if (compact == 1 && word == 0) {
			compact = 0;
			length += snprintf(buffer+length, size-length, 0 < i ? ":" : "::");

			/* 1:2:3:4:5:6:7:0 compacting trailing zeros 1:2:3:4:5:6:7:: */
			if (i == IPV6_BYTE_LENGTH-2)
				break;

			for (i += 2; i < IPV6_BYTE_LENGTH; i += 2) {
				word = NET_GET_SHORT(&ip[i]);
				if (word != 0)
					break;

				/* Continue countining leading zero words. */
				if ((i >> 1) == z)
					z++;
			}
		}

		/* IPv4-compatibile-IPv6 == 0:0:0:0:0:0:123.45.67.89 */
		if (z == 6 && i == 12)
			break;

		length += snprintf(buffer+length, size-length, word_fmt, word);
		if (i < IPV6_BYTE_LENGTH-2 && length+1 < size)
			buffer[length++] = ':';

		/* IPv4-mapped-IPv6  == 0:0:0:0:0:ffff:123.45.67.89 */
		if (z == 5 && i == 10 && word == 0xFFFF)
			break;
	}
	if (length < size)
		buffer[length] = '\0';

	/* IPv4-compatibile-IPv6 or IPv4-mapped-IPv6 */
	if ((z == 6 && i == 12) || (z == 5 && i == 10))
		length += snprintf(buffer+length, size-length, "%d.%d.%d.%d", ip[12],ip[13],ip[14],ip[15]);

	return length;
}

/**
 * @param addr
 *	A struct sockaddr pointer.
 *
 * @param flags
 *	The flags argument is formed by OR'ing the following values:
 *	SOCKET_ADDRESS_WITH_PORT, SOCKET_ADDRESS_WITH_BRACKETS,
 *	SOCKET_ADDRESS_AS_FULL, SOCKET_ADDRESS_AS_IPV4.
 *
 * @param buffer
 *	A buffer to hold the C string.
 *
 * @param size
 *	The size of the buffer. Should be at least SOCKET_ADDRESS_STRING_SIZE.
 *	It may need to be longer when using unix domain sockets.
 *
 * @retrun
 *	The length of the formatted address, excluding the terminating null
 *	byte if the buffer were of infinite size. If the return value is
 *	greater than or equal to the buffer size, then the contents of the
 *	buffer are truncated.
 */
long
socketAddressFormatIp(const struct sockaddr *sa, int flags, char *buffer, size_t size)
{
	int delim = ' ';
	long length = 0;
	unsigned port = 0;

	if (sa == NULL) {
		errno = EFAULT;
		return 0;
	}

	if (flags & SOCKET_ADDRESS_WITH_BRACKETS)
		length += snprintf(buffer, size, "[");

	switch (sa->sa_family) {
	case AF_INET:
		length += formatIP((unsigned char *) &((struct sockaddr_in *) sa)->sin_addr, IPV4_BYTE_LENGTH, 0, buffer+length, size-length);
		port = ntohs(((struct sockaddr_in *) sa)->sin_port);
		delim = ':';
		break;

#ifdef HAVE_STRUCT_SOCKADDR_IN6
	case AF_INET6:
{
		int offset, ip_length;

		offset = 0;
		ip_length = IPV6_BYTE_LENGTH;

		if ((flags & SOCKET_ADDRESS_AS_IPV4)
		&& isReservedIPv6((unsigned char *) &((struct sockaddr_in6 *) sa)->sin6_addr, IS_IP_V4)) {
			offset = IPV6_OFFSET_IPV4;
			ip_length = IPV4_BYTE_LENGTH;
		}

		length += formatIP(
			(unsigned char *) &((struct sockaddr_in6 *) sa)->sin6_addr + offset,
			ip_length, (flags & SOCKET_ADDRESS_AS_FULL) == 0,
			buffer+length, size-length
		);
		/* see RFC 2732 about expressing IPv6 addresses in URLs. */
		delim = (flags & SOCKET_ADDRESS_WITH_BRACKETS) ? ':' : ',';
		port = ntohs(((struct sockaddr_in6 *) sa)->sin6_port);
		break;
}
#endif
#ifdef HAVE_STRUCT_SOCKADDR_UN
	case AF_UNIX:
		return TextCopy(buffer, size, ((struct sockaddr_un *) sa)->sun_path);
#endif
	default:
		errno = EPFNOSUPPORT;
		return 0;
	}

	if (flags & SOCKET_ADDRESS_WITH_BRACKETS)
		length += snprintf(buffer+length, size-length, "]");

	if (flags & SOCKET_ADDRESS_WITH_PORT)
		length += snprintf(buffer+length, size-length, "%c%u", delim, port);

	return length;
}

#ifdef TEST
#include <stdio.h>

char *test_list[] = {
	"TEST",

	/* This host */
	"::",
	"::0",

	/* Local host */
	"::1",

	/* Link local */
	"fe80::",
	"fe80::1",
	"fe80::230:18ff:fef8:707d",

	/* IPv4-compatibile-IPv6 */
	"::123.45.67.89",

	/* IPv4-mapped-IPv6 */
	"::FFFF:123.45.67.89",
	"::beef:123.45.67.89",

	"2001::123.45.67.89",
	"2001::1",
	"2001::1234:0",

	/* Test net */
	"2001:db8::",

	/* Last word is a zero. */
	"1:2:3:4:5:6:7::",
	"1:2:3:4:5:6:7:0",

	/* Check for no buffer overflow. */
	"1234:5678:9ABC:DEF0:1234:5678:9ABC:DEF0",

	NULL
};

int
main(int argc, char **argv)
{
	int argi, length;
	char string[IPV6_STRING_LENGTH];
	unsigned char ipv6[IPV6_BYTE_LENGTH];

	if (argc == 1) {
		argv = test_list;
		argc = sizeof (test_list) / sizeof (*test_list) - 1;
	}

	for (argi = 1; argi < argc; argi++) {
		length = parseIPv6(argv[argi], ipv6);
		if (length == 0)
			printf("%s does not parse\n", argv[argi]);
		else {
			printf("%s\t", argv[argi]);

			length = formatIP(ipv6, sizeof (ipv6), 0, string, sizeof (string));
			if (sizeof (string) <= length) {
				printf("buffer overflow!\n");
				return 1;
			}
			printf("full=%s\t", string);

			length = formatIP(ipv6, sizeof (ipv6), 1, string, sizeof (string));
			if (sizeof (string) <= length) {
				printf("buffer overflow!\n");
				return 1;
			}
			printf("compact=%s\n", string);
		}
	}

	return 0;
}

#endif
