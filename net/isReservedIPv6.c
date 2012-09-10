/*
 * isReservedIPv6.c
 *
 * Copyright 2002, 2006 by Anthony Howe. All rights reserved.
 */


/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <errno.h>
#include <stdlib.h>

#include <com/snert/lib/net/network.h>

#define IPV6_OFFSET_IPV4	(IPV6_BYTE_LENGTH-IPV4_BYTE_LENGTH)

/**
 * @param ip
 *	An IPv6 address in network byte order.
 *
 * @param mask
 *	A bit mask of which special IP addresses to test for.
 *
 * @return
 *	True if the IP address string matches a reserved IP address.
 *	See RFC 3330, 3513, 3849, 4048, 4291
 */
int
isReservedIPv6(unsigned char ipv6[IPV6_BYTE_LENGTH], is_ip_t mask)
{
	int zeros;

	if (ipv6 == NULL) {
		errno = EFAULT;
		return 0;
	}

	/* Count leading zero octets. */
	for (zeros = 0; zeros < IPV6_BYTE_LENGTH; zeros++)
		if (ipv6[zeros] != 0)
			break;

	/* RFC 3513, 3330, 4291 */
	if ((mask & IS_IP_THIS_HOST) && zeros == IPV6_BYTE_LENGTH)
		return 1;

	/* IPv4 0.0.0.0/8 */
	if ((mask & IS_IP_THIS_NET) && zeros >= IPV6_BYTE_LENGTH - 3)
		return 1;

	/* RFC 3513, 4291 ::1/128 */
	if ((mask & IS_IP_LOCALHOST) && zeros == 15 && ipv6[15] == 0x01)
		return 1;

	/* IPv4-compatible IPv6 address and IPv4-mapped IPv6 address */
	if (zeros == IPV6_OFFSET_IPV4 || (zeros == 10 && ipv6[10] == 0xff && ipv6[11] == 0xff)) {
		/* RFC 4291 deprecates IPv4-Compatible IPv6 address, now reserved space. */
		if ((mask & IS_IP_V4_COMPATIBLE) && zeros == IPV6_OFFSET_IPV4)
			return 1;

		if ((mask & IS_IP_V4_MAPPED))
			return 1;

		return isReservedIPv4(ipv6 + IPV6_OFFSET_IPV4, mask);
	}

	/* RFC 4291 0000::/8 */
	if ((mask & IS_IP_V6_RESERVED) && zeros >= 2)
		return 1;

	if ((mask & IS_IP_V6))
		return 1;

	/* RFC 3849 IPv6 Address Prefix Reserved for Documentation */
	if ((mask & IS_IP_TEST_NET)   && NET_GET_LONG(ipv6) == 0x20010DB8)
		return 1;

	/* RFC 3513, 4291 FE80::/10 */
	if ((mask & IS_IP_LINK_LOCAL) && ipv6[0] == 0xFE && (ipv6[1] & 0xC0) == 0x80)
		return 1;

	/* RFC 3513, 4291 FF00::/8 */
	if ((mask & IS_IP_MULTICAST)  && ipv6[0] == 0xFF)
		return 1;

	return 0;
}

