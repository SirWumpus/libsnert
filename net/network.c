/*
 * network.c
 *
 * Network Support Routines
 *
 * Copyright 2004, 2012 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>
#include <stdlib.h>
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
networkContainsIPv6(unsigned char net[IPV6_BYTE_SIZE], unsigned long cidr, unsigned char ipv6[IPV6_BYTE_SIZE])
{
	int i, prefix, partial;
	unsigned char mask[IPV6_BYTE_SIZE];

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

/**
 * @param net
 *	An IPv6 or IPv4 network address and CIDR string.
 *
 * @param ip
 *	An IPv6 or IPv4 address string.
 *
 * @return
 *	True if network/cidr contains the given IP address.
 */
int
networkContainsIP(const char *net_cidr, const char *address)
{
	const char *slash;
	unsigned long cidr;
	unsigned char network[IPV6_BYTE_SIZE], ip[IPV6_BYTE_SIZE];

	if (net_cidr == NULL || (slash = strchr(net_cidr, '/')) == NULL)
		return 0;

	slash++;
	cidr = strtol(slash, NULL, 10);

	if (parseIPv6(net_cidr, network) <= 0)
		return 0;
	if (parseIPv6(address, ip) <= 0)
		return 0;

	/* Detect difference between IPv4 and IPv6 CIDR. */
	if (cidr <= IPV4_BIT_LENGTH && strchr(net_cidr, ':') == NULL)
		cidr = IPV6_BIT_LENGTH - IPV4_BIT_LENGTH + cidr;

	return networkContainsIPv6(network, cidr, ip);
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

#ifdef TEST
#include <stdio.h>
#include <com/snert/lib/util/getopt.h>
#include <com/snert/lib/sys/sysexits.h>

static const char usage[] =
"usage: netcontainsip [-p] cidr ip\n"
;

int
main(int argc, char **argv)
{
	int rc, ch, print = 0;

	while ((ch = getopt(argc, argv, "p")) != -1) {
		switch (ch) {
		case 'p':
			print = 1;
			break;
		default:
			optind = argc;
			break;
		}
	}

	if (argc <= optind+1) {
		fprintf(stderr, usage);
		return EX_USAGE;
	}

	rc = networkContainsIP(argv[optind], argv[optind+1]);

	if (print)
		printf("%s %s %s\n", rc ? "Yes" : "No", argv[optind], argv[optind+1]);

	return ! rc;
}
#endif
