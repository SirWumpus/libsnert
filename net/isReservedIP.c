/*
 * isReservedIP.c
 *
 * Copyright 2002, 2006 by Anthony Howe. All rights reserved.
 */


/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <errno.h>

#include <com/snert/lib/net/network.h>

/**
 * A convenience function to parse and test an IP address string.
 *
 * @param ip
 *	An IP address or IP-as-domain literal string.
 *
 * @param mask
 *	A bit mask of which special IP addresses to test for.
 *
 * @return
 *	True if the IP address string matches a reserved IP address.
 *	See RFC 3330, 3513, 3849, 4048
 */
int
isReservedIP(const char *ip, long mask)
{
	unsigned char ipv6[IPV6_BYTE_LENGTH];

	if (parseIPv6(ip, ipv6) == 0)
		return 0;

	return isReservedIPv6(ipv6, mask);
}

