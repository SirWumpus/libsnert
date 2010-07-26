/*
 * network.c
 *
 * Network Support Routines
 *
 * Copyright 2004, 2008 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>
#include <string.h>
#include <com/snert/lib/net/network.h>

/**
 * @param net
 *	An IPv6 network address.
 *
 * @param cidr
 *	A Classless Inter-Domain Routing (CIDR) prefix value.
 *
 * @param ipv6
 *	A specific IPv6 address.
 *
 * @return
 *	True if network/cidr contains the given IPv6 address.
 */
int
networkContainsIp(unsigned char net[IPV6_BYTE_LENGTH], unsigned long cidr, unsigned char ipv6[IPV6_BYTE_LENGTH])
{
	int i, prefix, partial;
	unsigned char mask[IPV6_BYTE_LENGTH];

	if (IPV6_BIT_LENGTH < cidr)
		return 0;

	prefix = cidr / 8;
	partial = cidr % 8;
	memset(mask, 0, sizeof (mask));
	memset(mask, 0xff, prefix);
	if (partial != 0)
		mask[prefix] = (unsigned char) ((signed char) 0x80 >> (partial-1));

	for (i = 0; i < sizeof (mask); i++) {
		if ((ipv6[i] & mask[i]) != (net[i] & mask[i]))
			return 0;
	}

	return 1;
}

unsigned short
networkGetShort(unsigned char *p)
{
	return NET_GET_SHORT(p);
}

unsigned long
networkGetLong(unsigned char *p)
{
	return NET_GET_LONG(p);
}

size_t
networkSetShort(unsigned char *p, unsigned short n)
{
	NET_SET_SHORT(p, n);
	return 2;
}

size_t
networkSetLong(unsigned char *p, unsigned long n)
{
	NET_SET_LONG(p, n);
	return 4;
}
