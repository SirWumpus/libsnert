/**
 * Dns.h
 *
 * Copyright 2002, 2006 by Anthony Howe. All rights reserved.
 */

#ifndef __com_snert_lib_io_Dns_h__
#define __com_snert_lib_io_Dns_h__	1

#ifndef __com_snert_lib_type_Vector_h__
# include <com/snert/lib/type/Vector.h>
#endif

#include <stdio.h>

/*
 * Provide IPPROTO_* definitions.
 */
#if defined(__WIN32__)
# if defined(__VISUALC__)
#  define _WINSOCK2API_
# endif
# include <windows.h>
# include <winsock2.h>
# define ETIMEDOUT	WSAETIMEDOUT
#else
# include <sys/types.h>
# include <netinet/in.h>
#endif /* __WIN32__ */

#ifdef __cplusplus
extern "C" {
#endif

#ifndef IPV4_BIT_LENGTH
#define IPV4_BIT_LENGTH			32
#endif

#ifndef IPV4_BYTE_LENGTH
#define IPV4_BYTE_LENGTH		(IPV4_BIT_LENGTH/8)
#endif

#ifndef IPV4_STRING_LENGTH
/* Space for a full-size IPv4 string (4 octets of 3 decimal digits
 * separated by dots and terminating NULL byte).
 */
#define IPV4_STRING_LENGTH		(IPV4_BIT_LENGTH/8*4)
#endif

#ifndef IPV6_BIT_LENGTH
#define IPV6_BIT_LENGTH			128
#endif

#ifndef IPV6_BYTE_LENGTH
#define IPV6_BYTE_LENGTH		(IPV6_BIT_LENGTH/8)
#endif

#ifndef IPV6_STRING_LENGTH
/* Space for a full-size IPv6 string (8 groups of 4 character hex
 * words separated by colons and terminating NULL byte).
 */
#define IPV6_STRING_LENGTH		(IPV6_BIT_LENGTH/16*5)
#endif

/**
 * An opaque Dns object.
 */
typedef void *Dns;

/**
 * A DNS resource record object.
 */
typedef struct {
	char *name;

	/* For all records except SOA and TXT, this is a C string. A TXT
	 * may contain binary data and requires decoding. A SOA will point
	 * to a structure. For A and AAAA records, this is the textual
	 * representation of the address; the IPv6 network binary address
	 * is saved in ``address'' below.
	 */
	void *value;			/* string, DnsSOA, binary string */

	unsigned long ttl;
	unsigned short type;
	unsigned short preference;	/* DNS_TYPE_MX only, 0 otherwise */

	/* This field is NULL or an IPv6 address in network byte order.
	 * This is always set for A and AAAA records and MAY be set for
	 * CNAME, MX, NS, and SOA records and will represent the resolved
	 * IP address.
	 */
	unsigned char *address;		/* NULL or IPV6_BYTE_LENGTH buffer */
	int address_length;		/* If address not NULL, then either
					 * IPV4_BYTE_LENGTH or IPV6_BYTE_LENGTH.
					 */
	char *address_string;
} DnsEntry;

/**
 * A DNS resource record object subsection for an SOA.
 */
typedef struct {
	char *mname;
	char *rname;
	unsigned long serial;
	  signed long refresh;
	  signed long retry;
	  signed long expire;
	unsigned long minimum;
} DnsSOA;

typedef struct {
	unsigned int length;
	unsigned char *data;
} DnsTXT;

extern const char DnsErrorNameLength[];
extern const char DnsErrorLabelLength[];
extern const char DnsErrorSocket[];
extern const char DnsErrorRead[];
extern const char DnsErrorWrite[];
extern const char DnsErrorNoAnswer[];
extern const char DnsErrorIdMismatch[];
extern const char DnsErrorFormat[];
extern const char DnsErrorServer[];
extern const char DnsErrorNotFound[];
extern const char DnsErrorNotImplemented[];
extern const char DnsErrorRefused[];
extern const char DnsErrorUnknown[];
extern const char DnsErrorCircular[];
extern const char DnsErrorInternal[];
extern const char DnsErrorMemory[];
extern const char DnsErrorNullArgument[];
extern const char DnsErrorIpParse[];
extern const char DnsErrorUnsupportedType[];
extern const char DnsErrorUndefined[];

/**
 * @param level
 *	Set debug level. The higher the more verbose.  Zero is silent.
 */
extern void DnsSetDebug(int level);

/**
 * (Re)Load the resolv.conf file. Currently only nameserver lines
 * are recognised. This function should be called before the first
 * DnsOpen() call.
 *
 * @param
 *	The resolv.conf file path to parse. If NULL, then use the system
 *	default, typically /etc/resolv.conf.
 *
 * @return
 *	Zero (0) on success otherwise -1 one error.
 */
extern int DnsInit(char *resolv_conf_path);

/**
 * We're finished with the Dns subsystem.
 */
extern void DnsFini(void);

/**
 * @param servers
 *	A NULL terminate array of C string pointers. Each C string
 * 	is the IP address of a DNS server to consult in order of
 *	preference. This argument can be NULL, in which case the
 *	list of DNS servers from /etc/resolv.conf are consulted.
 */
extern void DnsSetNameServers(char **servers);

/**
 * @return
 *	A Dns object returned by DnsOpen().
 */
extern Dns DnsOpen(void);

/**
 * @param dns
 *	A Dns object returned by DnsOpen().
 *
 * @param n
 *	Maximum number of times to cycle through the DNS server
 *	list before giving up on a search. Default is 4.
 */
extern void DnsSetRounds(Dns dns, int n);

/**
 * @param dns
 *	A Dns object returned by DnsOpen().
 *
 * @param ms
 *	The initial timeout in milliseconds to wait for a response
 *	from a DNS server. An exponential backup algorithm is used.
 *	The default is 5000 ms.
 *
 * Using the defaults and assuming a single DNS server, then the
 * exponential backoff algorithm will result in a max. delay of
 * 75 seconds (5+10+20+40) to find an answer before giving up.
 *
 * With more than one server, the timeout each round is divided
 * by the number of servers available. So for example:
 *
 *	1 server : 5+     10+    20+    40+      = 75 seconds
 *	2 servers: 5+5+   5+5+   10+10+ 20+20    = 80 seconds
 *	3 servers: 5+5+5+ 5+5+5+ 6+6+6+ 13+13+13 = 87 seconds
 */
extern void DnsSetTimeout(Dns dns, long ms);

/**
 * @param dns
 *	A Dns object returned by DnsOpen().
 *
 * @param first
 *	One of DNS_TYPE_A or DNS_TYPE_AAAA.
 *
 * @param second
 *	One of DNS_TYPE_A, DNS_TYPE_AAAA, or zero (0).
 */
extern void DnsSetAddressOrder(Dns dns, int first, int second);

/**
 * @param dns
 *	A Dns object returned by DnsOpen() to close.
 */
extern void DnsClose(Dns dns);

/**
 * @param dns
 *	A Dns object returned by DnsOpen().
 *
 * @return
 *	A DNS_RCODE_ value. The value DNS_RCODE_ERRNO is an internal
 *	error conditions not returned from a DNS server.
 */
extern int DnsGetReturnCode(Dns dns);

#define DNS_RCODE_ERRNO			(-1)
#define DNS_RCODE_OK			0
#define DNS_RCODE_FORMAT		1
#define DNS_RCODE_SERVER		2
#define DNS_RCODE_UNDEFINED		3
#define DNS_RCODE_NOT_IMPLEMENTED	4
#define DNS_RCODE_REFUSED		5

/**
 * @param dns
 *	A Dns object returned by DnsOpen().
 *
 * @return
 *	A C string error message for the corresponding DNS_RCODE_ value.
 */
extern const char *DnsGetError(Dns dns);

/**
 * @param dns
 *	A Dns object returned by DnsOpen().
 *
 * @param type
 *	The DNS resource record type to search for. See DNS_TYPE_ constants.
 *	If type is DNS_TYPE_PTR and the name argument is not already in .arpa
 *	form, then the IPv4 or IPv6 address will be reversed into its .arpa
 *	form.
 *
 * @param recurse
 *	If true, DnsGet() will attempt to resolve the answers given by
 *	the initial query to an IP address and discard the supplemental
 *	information once complete. If false, then all the records from
 *	the first query are returned.
 *
 * @param name
 *	A domain or host name to search for. In the case of a PTR
 * 	search, its the IP address in human readable form.
 *
 * @return
 *	A Vector of DnsEntry pointers on success, otherwise a NULL
 *	pointer on error. The Vector may be empty. When finished
 *	with the Vector, pass it to VectorDestroy() to clean up.
 */
extern Vector DnsGet(Dns dns, int type, int recurse, const char *name);

#define DNS_TYPE_A			1	/* RFC 1035 */
#define DNS_TYPE_NS			2	/* RFC 1035 */
#define DNS_TYPE_CNAME			5	/* RFC 1035 */
#define DNS_TYPE_SOA			6	/* RFC 1035 */
#define DNS_TYPE_WKS			11	/* RFC 1035, not supported */
#define DNS_TYPE_PTR			12	/* RFC 1035 */
#define DNS_TYPE_HINFO			13	/* RFC 1035, not supported */
#define DNS_TYPE_MINFO			14	/* RFC 1035, not supported */
#define DNS_TYPE_MX			15	/* RFC 1035 */
#define DNS_TYPE_TXT			16	/* RFC 1035 */
#define DNS_TYPE_AAAA			28	/* RFC 1886, 3596 */
#define DNS_TYPE_A6			38	/* RFC 2874, not supported */
#define DNS_TYPE_ALL			255	/* RFC 1035, not supported */

/**
 * A DnsGet() wrapper that handles the calls to DnsOpen(), DnsGet(),
 * and DnsClose() using the default settings. Since DnsGet2() is a
 * simple wrapper for several complex function calls, error reporting
 * is not as precise as it could be; essentially the DNS_RCODE_ values
 * are mapped onto errno values.
 *
 *	DNS_RCODE_OK 			0
 *	DNS_RCODE_FORMAT		EINVAL
 *	DNS_RCODE_SERVER		EFAULT
 *	DNS_RCODE_UNDEFINED		ENOENT
 *	DNS_RCODE_NOT_IMPLEMENTED	EINVAL
 *	DNS_RCODE_REFUSED		EPERM
 *
 * @see DnsGet()
 */
extern int DnsGet2(int type, int recurse, const char *name, Vector *results, const char **error);

/**
 * @param entry
 *	A pointer to a previous allocated DnsEntry to be cloned.
 */
extern DnsEntry *DnsEntryClone(DnsEntry *entry);

/**
 * @param entry
 *	A pointer to a previous allocated DnsEntry to be destroyed.
 */
extern void DnsEntryDestroy(void *entry);

/**
 * @param fp
 *	An output file stream pointer.
 *
 * @param entry
 *	A DNS resource record to dump. The output will be one line.
 */
extern void DnsEntryDump(FILE *fp, DnsEntry *entry);

/**
 * @param typeName
 *	Type name to map to a DNS_TYPE_* code. The name is case insensitive.
 *
 * @return
 *	A DNS type code based on RFC 1035, 1886, 3596, and 2874 defined
 *	types.
 */
extern int DnsTypeCode(const char *typeName);

/**
 * @param typeCode
 *	A DNS type code based on RFC 1035, 1886, 3596, and 2874 defined
 *	types.
 *
 * @return
 *	A constant C string
 */
extern const char *DnsTypeName(int typeCode);

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
extern long reverseSegmentOrder(const char *string, const char *delims, char *buffer, int size);

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
extern long reverseByNibble(const char *string, char *buffer, int size);

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
extern long reverseSegments(const char *source, const char *delims, char *buffer, int size, int arpa);

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
extern long reverseIp(const char *source, char *buffer, int size, int arpa);

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_io_Dns_h__ */
