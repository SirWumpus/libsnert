/*
 *** DEPRICATED ***
 *
 * uriIsDomainBL.c
 *
 * RFC 2821, 2396 Support Routines
 *
 * Copyright 2002, 2006 by Anthony Howe. All rights reserved.
 */

#ifndef DNSBL
#define DNSBL				".multi.surbl.org"
#endif

#define _REENTRANT	1

#include <com/snert/lib/version.h>

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#endif

#if defined(HAVE_SYSLOG_H) && ! defined(__MINGW32__)
# include <syslog.h>
#endif

#include <com/snert/lib/io/socket2.h>
#include <com/snert/lib/io/Dns.h>
#include <com/snert/lib/io/Log.h>
#include <com/snert/lib/net/network.h>
#include <com/snert/lib/mail/tlds.h>
#include <com/snert/lib/mail/parsePath.h>
#include <com/snert/lib/util/b64.h>
#include <com/snert/lib/util/uri.h>
#include <com/snert/lib/util/Text.h>

extern int uriDebug;

/**
 * @param host
 *	A pointer to a C string containing a FQDN or IP address.
 *
 * @param dnsbl_suffix
 *	A pointer to a C string of a DNSBL suffix to be appended to
 *	the query lookup.
 *
 * @param mask
 *	A 32-bit unsigned bit mask of used for aggregate lists. Set
 *	the bits that you are interested in. Specify ~0L for all.
 *	Note that bits 1 and 25..32 are ignored due to the way
 *	most blacklists are implemented in DNS.
 *
 * @param lookup_subdomains
 *	If true and host is not an IP address, then first check the
 *	registered domain and then subsequent sub-domains working
 *	from right to left.
 *
 * @return
 *	True if the host is black listed.
 *
 * @see
 *	http://www.surbl.org/implementation.html
 */
int
uriIsDomainBL(const char *host, const char *dnsbl_suffix, unsigned long mask, int lookup_subdomains)
{
	int offset;
	size_t length;
	Vector answer;
	DnsEntry *entry;
	char buffer[256];
	unsigned long bits;

	if (host == NULL || dnsbl_suffix == NULL) {
		errno = EINVAL;
		return 0;
	}

	/* Ignore bits 1 and 25..32, since they match 127.0.0.1. */
	mask &= 0x00fffffe;

	/* If host is an IP address, then we have to reverse it first. */
	if (0 < spanIP((unsigned char *)host)) {
		if ((length = reverseIp(host, buffer, sizeof (buffer), 0)) == 0) {
			errno = EINVAL;
			return 0;
		}

		/* Its an IP address, there are no subdomains, daaa! */
		lookup_subdomains = offset = 0;

		/* Jump into the do-while loop. */
		goto add_suffix;
	}

	/* Find start of TLD. */
	offset = indexValidTLD(host);

	/* An unknown TLD domain is not tested and is not
	 * blacklisted, because there should be no means
	 * by which to reach a domain with an unknown TLD.
	 */
	if (offset < 0)
		return 0;

	/* Backup one level to obtain the registered domain. */
	do {
		offset = strlrcspn(host, offset-1, ".");

		if (sizeof (buffer) <= (length = TextCopy(buffer, sizeof (buffer), (char *) host + offset))) {
			errno = EFAULT;
			break;
		}

		/* When the host name ends in a trailing dot, remove it. */
		if (0 < length && buffer[length-1] == '.')
			length--;
add_suffix:
		/* But make sure we have a leading dot on the DNSBL suffix. */
		if (*dnsbl_suffix != '.')
			buffer[length++] = '.';

		if (sizeof (buffer) <= TextCopy(buffer+length, sizeof (buffer)-length, (char *) dnsbl_suffix)) {
			errno = EINVAL;
			break;
		}

		if (1 < uriDebug)
			syslog(LOG_DEBUG, "lookup %s", buffer);

		if (DnsGet2(DNS_TYPE_A, 1, buffer, &answer, NULL) == 0
		&& (entry = VectorGet(answer, 0)) != NULL && entry->address != NULL) {
			bits = NET_GET_LONG(entry->address + IPV6_OFFSET_IPV4);
			if ((bits & mask) != 0) {
				if (0 < uriDebug)
					syslog(LOG_DEBUG, "found domain %s %s", buffer, entry->address_string);

				VectorDestroy(answer);
				return 1;
			}
		}

		VectorDestroy(answer);

		/* Check sub-domains from right to left. */
	} while (lookup_subdomains && 0 < offset);

	return 0;
}

/**
 * @param host
 *	A pointer to a C string containing a FQDN or IP address. Lookup
 *	the host's A or AAAA record and check each of its IPs against
 *	the DNSBL.
 *
 * @param dnsbl_suffix
 *	A pointer to a C string of DNSBL suffix to be appended to
 *	the query lookup.
 *
 * @param mask
 *	A 32-bit unsigned bit mask of used for aggregate lists. Set
 *	the bits that you are interested in. Specify ~0L for all.
 *	Note that bits 1 and 25..32 are ignored due to the way
 *	most blacklists are implemented in DNS.
 *
 * @param dummy
 *	Not used. Present for function signature compatibility with
 *	uriIsDomainBL().
 *
 * @return
 *	True if the host is black listed.
 *
 * @see
 *	http://www.spamhaus.org/sbl/howtouse.html
 */
int
uriIsHostBL(const char *host, const char *dnsbl_suffix, unsigned long mask, int dummy)
{
	long length;
	int i, found;
	Vector answer;
	DnsEntry *entry;
	char buffer[256];
	unsigned long bits;
	Vector addr_list = NULL;

	if (host == NULL || dnsbl_suffix == NULL) {
		errno = EINVAL;
		return 0;
	}

	/* Ignore bits 1 and 25..32, since they match 127.0.0.1. */
	mask &= 0x00fffffe;

	if (DnsGet2(DNS_TYPE_A, 1, host, &addr_list, NULL) != 0)
		return 0;

	found = 0;

	for (i = 0; (host = VectorGet(addr_list, i)) != NULL; i++) {
		/* Some domains specify a 127.0.0.0/8 address for
		 * an A recorded, like "anything.so". The whole
		 * TLD .so for Somalia, is a wild card record that
		 * maps to 127.0.0.2, which typically is a DNSBL
		 * test record that always fails.
		 */
		if (isReservedIPv6(((DnsEntry *) host)->address, IS_IP_LOOPBACK))
			continue;

		host = ((DnsEntry *) host)->value;

		if ((length = reverseIp(host, buffer, sizeof (buffer), 0)) == 0) {
			errno = EINVAL;
			break;
		}

		/* But make sure we have a leading dot on the DNSBL suffix. */
		if (*dnsbl_suffix != '.')
			buffer[length++] = '.';

		if (sizeof (buffer) <= TextCopy(buffer+length, sizeof (buffer)-length, (char *) dnsbl_suffix)) {
			errno = EINVAL;
			break;
		}

		if (1 < uriDebug)
			syslog(LOG_DEBUG, "lookup %s", buffer);

		if (DnsGet2(DNS_TYPE_A, 1, buffer, &answer, NULL) == 0
		&& (entry = VectorGet(answer, 0)) != NULL && entry->address != NULL) {
			bits = NET_GET_LONG(entry->address + IPV6_OFFSET_IPV4);
			if ((bits & mask) != 0) {
				if (0 < uriDebug)
					syslog(LOG_DEBUG, "found host %s %s", buffer, entry->address_string);

				VectorDestroy(answer);
				return 1;
			}
		}

		VectorDestroy(answer);
	}

	VectorDestroy(addr_list);

	return found;
}
