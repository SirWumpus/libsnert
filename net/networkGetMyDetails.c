/*
 * networkGetMyDetails.c
 *
 * Network Support Routines
 *
 * Copyright 2004, 2008 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <stdlib.h>
#include <string.h>

#ifndef __MINGW32__
# if defined(HAVE_NETDB_H)
#  include <netdb.h>
# endif
# if defined(HAVE_SYSLOG_H)
#  include <syslog.h>
# endif
#endif
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include <com/snert/lib/net/network.h>
#include <com/snert/lib/io/Log.h>
#include <com/snert/lib/io/socket2.h>
#include <com/snert/lib/util/Text.h>

/**
 * @param host
 *	A pointer to a buffer to fill with the FQDN for this host.
 */
void
networkGetMyName(char host[DOMAIN_SIZE])
{
	struct hostent *my_name;

	if (gethostname(host, DOMAIN_SIZE) < 0) {
#ifndef NDEBUG
		UPDATE_ERRNO;
		syslog(LOG_ERR, "networkGetMyName: gethostname error: %d", errno);
#endif
		TextCopy(host, DOMAIN_SIZE, "localhost.localhost");
	}

	if ((my_name = gethostbyname(host)) != NULL)
		TextCopy(host, DOMAIN_SIZE, my_name->h_name);
}

/**
 * @param host
 *	The name of this host.
 *
 * @param ip
 *	A pointer to a buffer to fill with the IP address for this host.
 */
void
networkGetHostIp(char *host, char ip[IPV6_STRING_SIZE])
{
	struct hostent *my_ip;

	if ((my_ip = gethostbyname(host)) == NULL) {
#ifndef NDEBUG
		UPDATE_ERRNO;
		syslog(LOG_ERR, "networkGetHostIp: gethostbyname error: %d", errno);
#endif
     		TextCopy(ip, IPV6_STRING_SIZE, "0.0.0.0");
     	} else {
		(void) formatIP(
			(unsigned char *) my_ip->h_addr_list[0],
			my_ip->h_addrtype == AF_INET ? IPV4_BYTE_SIZE : IPV6_BYTE_SIZE,
			1, ip, IPV6_STRING_SIZE
		);
	}
}

/**
 * @param opt_name
 *	A pointer to a buffer containing a FQDN for this host. If empty,
 *	then the host name will be automatically determined. This name
 *	will be used to determine this host's IP address.
 *
 * @param opt_ip
 *	A pointer to a buffer containing an IP address for this host.
 *	If empty, then the IP address will be determined automatically
 *	from the host name.
 */
void
networkGetMyDetails(char host[DOMAIN_SIZE], char ip[IPV6_STRING_SIZE])
{
	if (*host == '\0')
		networkGetMyName(host);

	if (*ip == '\0')
		networkGetHostIp(host, ip);
}

