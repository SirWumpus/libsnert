/*
 * isReservedIPv4.c
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

/**
 * @param ipv4
 *	An IPv4 address in network byte order.
 *
 * @param mask
 *	A bit mask of which special IP addresses to test for.
 *
 * @return
 *	True if the IP address string matches a reserved IP address.
 *	See RFC 3330, 3513, 3849, 4048
 */
int
isReservedIPv4(unsigned char ipv4[IPV4_BYTE_LENGTH], long mask)
{
	unsigned long ip;

	if (ipv4 == NULL) {
		errno = EFAULT;
		return 0;
	}

	ip = NET_GET_LONG(ipv4);

	/* RFC 3330 */
	return
	    ((mask & IS_IP_LOCALHOST)  &&  ip               == 0x7f000001) /* 127.0.0.1/32    localhost      */
	||  ((mask & IS_IP_THIS_HOST)  &&  ip               == 0x00000000) /* 0.0.0.0/32      "this" host    */
	||  ((mask & IS_IP_TEST_NET)   && (ip & 0xffffff00) == 0xc0000200) /* 192.0.2.0/24    test network   */
	||  ((mask & IS_IP_LINK_LOCAL) && (ip & 0xffff0000) == 0xa9fe0000) /* 169.254.0.0/16  link local (private use) */
	||  ((mask & IS_IP_PRIVATE_C)  && (ip & 0xffff0000) == 0xc0a80000) /* 192.168.0.0/16  private use    */
	||  ((mask & IS_IP_BENCHMARK)  && (ip & 0xfffe0000) == 0xc6120000) /* 198.18.0.0/15   RFC 2544       */
	||  ((mask & IS_IP_PRIVATE_B)  && (ip & 0xfff00000) == 0xac100000) /* 172.16.0.0/12   private use    */
	||  ((mask & IS_IP_LOOPBACK)   && (ip & 0xff000000) == 0x7f000000  /* 127.0.0.0/8     loopback       */ && ip != 0x7f000001)
	||  ((mask & IS_IP_PRIVATE_A)  && (ip & 0xff000000) == 0x0a000000) /* 10.0.0.0/8      private use    */
	||  ((mask & IS_IP_THIS_NET)   && (ip & 0xff000000) == 0x00000000) /* 0.0.0.0/8       "this" network */
	||  ((mask & IS_IP_MULTICAST)  && (ip & 0xf0000000) == 0xe0000000) /* 224.0.0.0/4     RFC 3171       */
	;
}
