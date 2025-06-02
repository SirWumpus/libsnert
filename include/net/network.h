/*
 * network.h
 *
 * Network Support Routines
 *
 * Copyright 2004, 2014 by Anthony Howe. All rights reserved.
 */

#ifndef __com_snert_lib_io_network_h__
#define __com_snert_lib_io_network_h__	1

#ifdef __cplusplus
extern "C" {
#endif

#define IPV4_BIT_LENGTH			32
#define IPV6_BIT_LENGTH			128

#define IPV4_BYTE_SIZE			(IPV4_BIT_LENGTH/8)
#define IPV4_BYTE_LENGTH		IPV4_BYTE_SIZE		/* depricated */

/* Space for a full-size IPv4 string (4 octets of 3 decimal digits
 * separated by dots and terminating NULL byte).
 */
#define IPV4_STRING_SIZE		(IPV4_BIT_LENGTH/8*4)
#define IPV4_STRING_LENGTH		IPV4_STRING_SIZE	/* depricated */

#define IPV6_TAG			"IPv6:"
#define IPV6_TAG_LENGTH			5

#define IPV6_BYTE_SIZE			(IPV6_BIT_LENGTH/8)
#define IPV6_BYTE_LENGTH		IPV6_BYTE_SIZE		/* depricated */

/* Space for a full-size IPv6 string; 8 groups of 4 character hex
 * words (16-bits) separated by colons and terminating NULL byte.
 */
#define IPV6_STRING_SIZE		(IPV6_BIT_LENGTH/16*5)
#define IPV6_STRING_LENGTH		IPV6_STRING_SIZE	/* depricated */

#define IPV6_OFFSET_IPV4		(IPV6_BYTE_SIZE-IPV4_BYTE_SIZE)

/* Space for a full-size domain string, plus terminating NULL byte.
 */
#define DOMAIN_SIZE			256
#define DOMAIN_STRING_LENGTH		DOMAIN_SIZE        	/* depricated */

/* These macros intended to retrieve network numeric data types stored
 * at odd memory addresses, which can cause some bus errors on certain
 * CPU types if the pointer is cast to a particular type.
 */
#define NET_GET_SHORT(p)	(unsigned short) (                 \
				    (unsigned short)((unsigned char *) p)[0] << 8  \
				  | (unsigned short)((unsigned char *) p)[1]       \
				)

#define NET_GET_LONG(p)		(unsigned long) (                  \
				    (unsigned long)((unsigned char *) p)[0] << 24 \
				  | (unsigned long)((unsigned char *) p)[1] << 16 \
				  | (unsigned long)((unsigned char *) p)[2] << 8  \
				  | (unsigned long)((unsigned char *) p)[3]       \
				)

#define NET_SET_SHORT(p, n)	(						 \
				  (((unsigned char *) p)[0] = ((n) >> 8) & 0xFF),\
				  (((unsigned char *) p)[1] =  (n)       & 0xFF) \
				)

#define NET_SET_LONG(p, n)	(						 \
				  (((unsigned char *) p)[0] = ((n) >> 24) & 0xFF),\
				  (((unsigned char *) p)[1] = ((n) >> 16) & 0xFF),\
				  (((unsigned char *) p)[2] = ((n) >>  8) & 0xFF),\
				  (((unsigned char *) p)[3] =  (n)        & 0xFF) \
				)

/*
 * IP test flags for isReservedIPv4(), isReservedIPv6(), isReservedIP().
 */
typedef enum {
	IS_IP_BENCHMARK		= 0x00000001,	/* 198.18.0.0/15   	RFC 2544 */
	IS_IP_LINK_LOCAL	= 0x00000002,	/* 169.254.0.0/16, FF80::/10	RFC 3927, 4291 */
	IS_IP_LOCALHOST		= 0x00000004,	/* 127.0.0.1/32, ::1/128	localhost      */
	IS_IP_LOOPBACK		= 0x00000008,	/* 127.0.0.0/8     loopback, excluding 127.0.0.1 */
	IS_IP_MULTICAST		= 0x00000010,	/* 224.0.0.0/4     	RFC 3171 */
	IS_IP_PRIVATE_A		= 0x00000020,	/* 10.0.0.0/8      	RFC 5735 */
	IS_IP_PRIVATE_B		= 0x00000040,	/* 172.16.0.0/12   	RFC 5735 */
	IS_IP_PRIVATE_C		= 0x00000080,	/* 192.168.0.0/16  	RFC 5735 */
	IS_IP_CLASS_E		= 0x00000100,  	/* 240.0.0.0/4          RFC 1112 */
	IS_IP_PROTOCOL		= 0x00000200, 	/* 192.0.0.0/24         RFC 5736 */
	IS_IP_TEST_NET_1	= 0x00000400,	/* 192.0.2.0/24		RFC 5737 */
	IS_IP_THIS_HOST		= 0x00000800,	/* 0.0.0.0/32, ::0/128	"this" host */
	IS_IP_THIS_NET		= 0x00001000,	/* 0.0.0.0/8		"this" network */
	IS_IP_V4_COMPATIBLE	= 0x00002000,	/* 0000::/96		RFC 4291 section 4 */
	IS_IP_V4_MAPPED		= 0x00004000,  	/* IPv4-mapped IPv6 	RFC 4291 */
	IS_IP_V6		= 0x00008000,

	IS_IP_V6_RESERVED	= 0x00010000,	/* 0000::/8		RFC 4291 */
	IS_IP_BROADCAST		= 0x00020000,	/* 255.255.255.255/32	RFC 5735 */
	IS_IP_6TO4_ANYCAST	= 0x00040000,	/* 192.88.99.0/24	RFC 3068 */
	IS_IP_TEST_NET_2	= 0x00080000,	/* 198.51.100.0/24	RFC 5737 */
	IS_IP_TEST_NET_3	= 0x00100000,	/* 203.0.113.0/24	RFC 5737 */
	IS_IP_SHARED		= 0x00200000,	/* 100.64.0.0/10	RFC 6598 */
} is_ip_t;

#define IS_IP_V4		(IS_IP_V4_COMPATIBLE | IS_IP_V4_MAPPED)
#define IS_IP_ANY		(~0 & ~IS_IP_V4 & ~IS_IP_V6)

#define IS_IP_TEST_NET		(IS_IP_BENCHMARK | IS_IP_TEST_NET_1 | IS_IP_TEST_NET_2 | IS_IP_TEST_NET_3)
#define IS_IP_LOCAL		(IS_IP_THIS_HOST | IS_IP_LOCALHOST | IS_IP_LOOPBACK)
#define IS_IP_LAN		(IS_IP_PRIVATE_A | IS_IP_PRIVATE_B | IS_IP_PRIVATE_C | IS_IP_LINK_LOCAL )
#define IS_IP_ISP		(IS_IP_SHARED | IS_IP_LAN)
#define IS_IP_RESTRICTED	(IS_IP_LOCAL | IS_IP_TEST_NET | IS_IP_LINK_LOCAL | IS_IP_MULTICAST | IS_IP_CLASS_E | IS_IP_BROADCAST)

/**
 * @param ip
 *	An IP address in network byte order.
 *
 * @param ip_length
 *	The length of the IP address, which is either IPV4_BYTE_SIZE (4)
 *	or IPV6_BYTE_SIZE (16).
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
extern long formatIP(unsigned char *ip, int ip_length, int compact, char *buffer, long size);

/**
 * @param client_name
 *	The client name, typically found through DNS PTR lookup.
 *
 * @param ipv4
 *	The client's address in an IPv4 address buffer in network byte order.
 *
 * @return
 *	Return true if the client name contains a pattern of IPv4 octets
 *	corresponding to the client's connecting IP.
 */
extern int isIPv4InClientName(const char *client_name, unsigned char *ipv4);

/**
 * @param client_name
 *	The client name, typically found through DNS PTR lookup.
 *
 * @param ipv4
 *	The client's address in an IPv4 address buffer in network byte order.
 *
 * @param black
 *	A NULL terminated array of C string pointers of TextMatch() patterns
 *	to return true when matched. Specify NULL to ignore.
 *
 * @param white
 *	A NULL terminated array of C string pointers of TextMatch() patterns
 *	to return false when matched. Specify NULL to ignore.
 *
 * @return
 *	Return true if the client name contains a pattern of IPv4 octets
 *	corresponding to the client's connecting IP.
 */
extern int isIPv4InName(const char *client_name, unsigned char *ipv4, const char **black, const char **white);

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
extern int isReservedIPv4(unsigned char ipv4[IPV4_BYTE_SIZE], is_ip_t flags);

/**
 * @param ip
 *	An IPv6 address in network byte order.
 *
 * @param mask
 *	A bit mask of which special IP addresses to test for.
 *
 * @return
 *	True if the IP address string matches a reserved IP address.
 *	See RFC 3330, 3513, 3849, 4048
 */
extern int isReservedIPv6(unsigned char ipv6[IPV6_BYTE_SIZE], is_ip_t flags);

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
extern int isReservedIP(const char *ip, is_ip_t flags);

/**
 * @param path
 *	An email address or domain name string.
 *
 * @param flags
 *	Flag bit mask of IS_TLD_* flags for domains to restrict.
 *
 * @return
 *	True if the 1st or 2nd level domain matches a reserved domain.
 */
extern int isReservedTLD(const char *path, unsigned long flags);

#define IS_TLD_TEST			0x00000001
#define IS_TLD_EXAMPLE			0x00000002
#define IS_TLD_INVALID			0x00000004
#define IS_TLD_LOCALHOST		0x00000008
#define IS_TLD_LOCALDOMAIN		0x00000010
#define IS_TLD_LOCAL			0x00000020
#define IS_TLD_LAN			0x00000040
#define IS_TLD_HOME			0x00000080
#define IS_TLD_ARPA			0x00000100
#define IS_TLD_ONION			0x00000200
#define IS_TLD_ANY_LOCAL		(IS_TLD_LOCALHOST|IS_TLD_LOCALDOMAIN|IS_TLD_LOCAL|IS_TLD_LAN|IS_TLD_HOME)
#define IS_TLD_ANY_RESERVED		(~0)

/**
 * @param path
 *	An email address or domain name string.
 *
 * @return
 *	True if the domain portion matches the RFC 2606 reserved domains.
 */
extern int isRFC2606(const char *path);

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
extern int networkContainsIPv6(unsigned char net[IPV6_BYTE_SIZE], unsigned long cidr, unsigned char ipv6[IPV6_BYTE_SIZE]);
#define networkContainsIp	networkContainsIPv6

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
extern int networkContainsIP(const char *net_cidr, const char *address);

/**
 * @param host
 *	A pointer to a buffer to fill with the FQDN for this host.
 */
extern void networkGetMyName(char host[DOMAIN_SIZE]);

/**
 * @param host
 *	The name of this host.
 *
 * @param ip
 *	A pointer to a buffer to fill with the IP address for this host.
 */
extern void networkGetHostIp(char *host, char ip[IPV6_STRING_SIZE]);

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
extern void networkGetMyDetails(char host[DOMAIN_SIZE], char ip[IPV6_STRING_SIZE]);

extern unsigned short networkGetShort(unsigned char *p);
extern unsigned long networkGetLong(unsigned char *p);
extern size_t networkSetShort(unsigned char *p, unsigned short n);
extern size_t networkSetLong(unsigned char *p, unsigned long n);

/**
 * @param ip
 *	An IPv4, IPv6, or an IP-as-domain literal string.
 *
 * @param ipv6
 *	A buffer to save the parsed IP address in IPv6 network byte order.
 *
 * @return
 *	The length of the IP address string parsed; zero if nothing parsed.
 */
extern int parseIPv6(const char *ip, unsigned char ipv6[IPV6_BYTE_SIZE]);

/**
 * @param string
 *	The segmented string to reverse.
 *
 * @param delims
 *	A string of segment delimiters characters.
 *
 * @param buffer
 *	The buffer for the reversed segmented string. The buffer is
 *	always null terminated.
 *
 * @param size
 *	The size of the buffer, which should be greater than or equal
 *	to strlen(string)+1.
 *
 * @return
 *	The number of characters that were copied into the buffer,
 *	excluding the terminating null byte. If the return value is
 *	greater than or equal to the buffer size, then the contents
 *	of buffer are truncated.
 */
extern size_t reverseSegmentOrder(const char *string, const char *delims, char *buffer, size_t size);

/**
 * @param string
 *	A full IPv6 address string to reverse.
 *
 * @param buffer
 *	The buffer for the reversed segmented string. The buffer is
 *	always null terminated.
 *
 * @param size
 *	The size of the buffer, which should be greater than or equal
 *	to strlen(string)+1.
 *
 * @return
 *	The number of characters that were copied into the buffer,
 *	excluding the terminating null byte. If the return value is
 *	greater than or equal to the buffer size, then the contents
 *	of buffer are truncated.
 */
extern size_t reverseByNibble(const char *string, char *buffer, size_t size);

/**
 * @param string
 *	A IPv4 or IPv6 address string to reverse. Can also be a domain.
 *
 * @param delims
 *	A set of segment delimiters.
 *
 * @param buffer
 *	The buffer for the reversed segmented string. The buffer is
 *	always null terminated.
 *
 * @param size
 *	The size of the buffer, which should be greater than or equal
 *	to strlen(string)+1.
 *
 * @param arpa
 *	True is the string ".in-addr.arpa." or ".ip6.arpa." should be
 *	appended.
 *
 * @return
 *	The number of characters that were copied into the buffer,
 *	excluding the terminating null byte. If the return value is
 *	greater than or equal to the buffer size, then the contents
 *	of buffer are truncated.
 */
extern size_t reverseSegments(const char *source, const char *delims, char *buffer, size_t size, int arpa);

/**
 * @param string
 *	A IPv4 or IPv6 address string to reverse. Can also be a domain.
 *
 * @param buffer
 *	The buffer for the reversed segmented string. The buffer is
 *	always null terminated.
 *
 * @param size
 *	The size of the buffer, which should be greater than or equal
 *	to strlen(string)+1.
 *
 * @param arpa
 *	True is the string ".in-addr.arpa." or ".ip6.arpa." should be
 *	appended.
 *
 * @return
 *	The number of characters that were copied into the buffer,
 *	excluding the terminating null byte. If the return value is
 *	greater than or equal to the buffer size, then the contents
 *	of buffer are truncated.
 */
extern size_t reverseIp(const char *source, char *buffer, size_t size, int arpa);

/**
 * Find the first occurence of an IPv6 or IPv4 address in a string.
 *
 * @param string
 *	A C string to search.
 *
 * @param offsetp
 *	A pointer to an int in which to passback the offset of
 *	the IP address. -1 if not found. offsetp may be NULL.
 *
 * @param spanp
 *	A pointer to an int in which to passback the span of
 *	the IP address. 0 if not found. spanp may be NULL.
 *
 * @return
 *	A pointer to the first occurence of an IP address or
 *	NULL if not found.
 */
extern const char *findIP(const char *string, int *offsetp, int *spanp);

/**
 * Find the first occurence of an IPv4 address in a string.
 *
 * @param offsetp
 *	A pointer to an int in which to passback the offset of
 *	the IP address. -1 if not found. offsetp may be NULL.
 *
 * @param spanp
 *	A pointer to an int in which to passback the span of
 *	the IP address. 0 if not found. spanp may be NULL.
 *
 * @return
 *	A pointer to the first occurence of an IP address or
 *	NULL if not found.
 */
extern const char *findIPv4(const char *string, int *offsetp, int *spanp);

/**
 * Find the first occurence of an IPv6 address in a string.
 *
 * @param offsetp
 *	A pointer to an int in which to passback the offset of
 *	the IP address. -1 if not found. offsetp may be NULL.
 *
 * @param spanp
 *	A pointer to an int in which to passback the span of
 *	the IP address. 0 if not found. spanp may be NULL.
 *
 * @return
 *	A pointer to the first occurence of an IP address or
 *	NULL if not found.
 */
extern const char *findIPv6(const char *string, int *offsetp, int *spanp);

/***********************************************************************
 *** Span Family
 ***********************************************************************/

/**
 * RFC 2821 section 4.1.3 IP address literals
 *
 * @param ip
 *	A pointer to a C string that starts with an IPv4 or IPv6 address.
 *
 * @return
 *	The length of the IPv6 address string upto, but excluding, the
 *	first invalid character following it; otherwise zero (0) for a
 *	parse error.
 */
extern int spanIP(const unsigned char *ip);

/**
 * RFC 2821 section 4.1.3 IPv4 address literals
 *
 * @param ip
 *	A pointer to a C string that starts with an IPv4 address.
 *
 * @return
 *	The length of the IPv4 address string upto, but excluding, the
 *	first invalid character following it; otherwise zero (0) for a
 *	parse error.
 */
extern int spanIPv4(const unsigned char *ip);

/**
 * RFC 2821 section 4.1.3 IPv6 address literals
 *
 * @param ip
 *	A pointer to a C string that starts with an IPv6 address.
 *
 * @return
 *	The length of the IPv6 address string upto, but excluding, the
 *	first invalid character following it; otherwise zero (0) for a
 *	parse error.
 */
extern int spanIPv6(const unsigned char *ip);

/**
 * @param host
 *	Start of a host name, IP-domain-literal, or IP address..
 *
 * @param minDots
 *	The minimum number of dots separators expected in the host name.
 *
 * @return
 *	The length of the host name upto, but excluding, the first
 *	invalid character.
 */
extern int spanHost(const unsigned char *host, int minDots);

/**
 * RFC 2821 domain syntax excluding address-literal.
 *
 * Note that RFC 1035 section 2.3.1 indicates that domain labels
 * should begin with an alpha character and end with an alpha-
 * numeric character. However, all numeric domains do exist, such
 * as 123.com, so are permitted.
 *
 * @param domain
 *	Start of a domain name.
 *
 * @param minDots
 *	The minimum number of dots separators expected in the domain.
 *
 * @return
 *	The length of the domain upto, but excluding, the first
 *	invalid character.
 */
extern int spanDomain(const unsigned char *domain, int minDots);

/**
 * RFC 2821 section 4.1.2 Local-part and RFC 2822 section 3.2.4 Atom
 *
 * Validate only the characters.
 *
 *    Local-part = Dot-string / Quoted-string
 *    Dot-string = Atom *("." Atom)
 *    Atom = 1*atext
 *    Quoted-string = DQUOTE *qcontent DQUOTE
 *
 * @param s
 *	Start of the local-part of a mailbox.
 *
 * @return
 *	The length of the local-part upto, but excluding, the first
 *	invalid character.
 */
extern int spanLocalPart(const unsigned char *s);

/*
 * RFC 2821 section 4.1.2 Command Argument Syntax
 *
 * Validate the characters and syntax.
 *
 *	Path = "<" [ A-d-l ":" ] Mailbox ">"
 *	A-d-l = At-domain *( "," A-d-l )
 *		; Note that this form, the so-called "source route",
 *		; MUST BE accepted, SHOULD NOT be generated, and SHOULD be
 *		; ignored.
 *	At-domain = "@" domain
 *	Domain = (sub-domain 1*("." sub-domain)) / address-literal
 *	sub-domain = Let-dig [Ldh-str]
 *	Let-dig = ALPHA / DIGIT
 *	Ldh-str = *( ALPHA / DIGIT / "-" ) Let-dig
 */
extern long spanSourceRoute(const unsigned char *s);

/***********************************************************************
 ***
 ***********************************************************************/

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_sys_pid_h__ */
