/**
 * pdq.h
 *
 * Parallel Domain Query
 *
 * RFC 1035 (DNS), 1886 (IPv6), 2821 (SMTP), 2874 (IPv6), 3596 (IPv6)
 *
 * Copyright 2002, 2008 by Anthony Howe. All rights reserved.
 */

#ifndef __com_snert_lib_net_pdq_h__
#define __com_snert_lib_net_pdq_h__	1

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************************
 ***
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <stdio.h>

#if defined(__WIN32__)
# if defined(__VISUALC__)
#  define _WINSOCK2API_
# endif

/* IPv6 support such as getaddrinfo, freeaddrinfo, getnameinfo
 * only available in Windows XP or later.
 */
# define WINVER		0x0501

# include <windows.h>
# include <winsock2.h>
# define ETIMEDOUT	WSAETIMEDOUT
#else
# include <sys/types.h>
# include <netinet/in.h>
#endif /* __WIN32__ */

#if HAVE_INTTYPES_H
# include <inttypes.h>
#else
# if HAVE_STDINT_H
# include <stdint.h>
# endif
#endif

#ifdef TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# ifdef HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#include <com/snert/lib/net/network.h>

typedef struct pdq PDQ;

typedef struct {
	struct {
		unsigned short offset;			/* 0 or IPV6_OFFSET_IPV4 */
		unsigned char value[IPV6_BYTE_LENGTH];	/* IPv6 or IPv4-in-IPv6 address */
	} ip;
	struct {
		unsigned short length;
		char value[IPV6_STRING_LENGTH];
	} string;
} PDQ_address;

typedef struct {
	struct {
		unsigned short length;
		char value[DOMAIN_STRING_LENGTH];
	} string;
} PDQ_name;

typedef struct {
	struct {
		unsigned short length;
		unsigned char *value;
	} data;
} PDQ_data;

typedef enum {
	PDQ_TYPE_UNKNOWN		= 0,
	PDQ_TYPE_A			= 1,	/* RFC 1035 */
	PDQ_TYPE_NS			= 2,	/* RFC 1035 */
	PDQ_TYPE_CNAME			= 5,	/* RFC 1035 */
	PDQ_TYPE_SOA			= 6,	/* RFC 1035 */
	PDQ_TYPE_NULL			= 10,	/* RFC 1035 */
	PDQ_TYPE_WKS			= 11,	/* RFC 1035, not supported */
	PDQ_TYPE_PTR			= 12,	/* RFC 1035 */
	PDQ_TYPE_HINFO			= 13,	/* RFC 1035 */
	PDQ_TYPE_MINFO			= 14,	/* RFC 1035 */
	PDQ_TYPE_MX			= 15,	/* RFC 1035 */
	PDQ_TYPE_TXT			= 16,	/* RFC 1035 */
	PDQ_TYPE_AAAA			= 28,	/* RFC 1886, 3596 */
	PDQ_TYPE_A6			= 38,	/* RFC 2874, not supported */
	PDQ_TYPE_DNAME			= 39,	/* RFC 2672 */
	PDQ_TYPE_ANY			= 255,	/* RFC 1035 all (behaves like ``any'') */
	PDQ_TYPE_5A			= 256,	/* special API type for pdqListFindName */
} PDQ_type;

typedef enum {
	PDQ_CLASS_IN			= 1,	/* RFC 1035 Internet */
	PDQ_CLASS_CS			= 2,	/* RFC 1035 CSNET */
	PDQ_CLASS_CH			= 3,	/* RFC 1035 CHAOS */
	PDQ_CLASS_HS			= 4,	/* RFC 1035 Hesiod */
	PDQ_CLASS_ANY			= 255,	/* RFC 1035 any */
} PDQ_class;

typedef enum {
	PDQ_RCODE_OK			= 0,	/* RFC 1035 */
	PDQ_RCODE_FORMAT		= 1,	/* RFC 1035 */
	PDQ_RCODE_SERVER		= 2,	/* RFC 1035 */
	PDQ_RCODE_UNDEFINED		= 3,	/* RFC 1035 */
	PDQ_RCODE_NOT_IMPLEMENTED	= 4,	/* RFC 1035 */
	PDQ_RCODE_REFUSED		= 5,	/* RFC 1035 */
	PDQ_RCODE_ERRNO			= 16,	/* local error */
	PDQ_RCODE_TIMEDOUT		= 17,	/* timeout error */
	PDQ_RCODE_ANY			= 255,	/* any rcode, see pdqListFind */
} PDQ_rcode;

#define PDQ_CNAME_TOO_DEEP		((PDQ_rr *) 1)
#define PDQ_CNAME_IS_CIRCULAR		((PDQ_rr *) 2)
#define PDQ_RR_IS_VALID(rr)		((rr) != NULL && (rr) != PDQ_CNAME_TOO_DEEP && (rr) != PDQ_CNAME_IS_CIRCULAR)
#define PDQ_RR_IS_NOT_VALID(rr)		((rr) == NULL || (rr) == PDQ_CNAME_TOO_DEEP || (rr) == PDQ_CNAME_IS_CIRCULAR)

/*
 * Common RR elements. Prefixes the start of each RR record.
 */
typedef struct pdq_rr {
	struct pdq_rr *next;
	PDQ_rcode rcode;
	time_t created;			/* When this record was created. */
	PDQ_name name;			/* Domain, host, reversed-IP or request. */
	uint16_t class;			/* RFC 1035, PDQ_CLASS_ value */
	uint16_t type;			/* RFC 1035, PDQ_TYPE_ value */
	uint32_t ttl;			/* Original TTL received. */
} PDQ_rr;

#define PDQ_LIST_WALK(rr, list)		for ((rr) = (list); (rr) != NULL; (rr) = (rr)->next)

typedef struct {
	PDQ_rr rr;
	PDQ_address address;
} PDQ_A, PDQ_AAAA;

typedef struct {
	PDQ_rr rr;
	PDQ_name host;
} PDQ_CNAME, PDQ_NS, PDQ_PTR, PDQ_DNAME;

typedef struct {
	PDQ_rr rr;
	PDQ_name host;
	uint16_t preference;
} PDQ_MX;

typedef struct {
	PDQ_rr rr;
	struct {
		unsigned long length;
		unsigned char *value;
	} text;
} PDQ_TXT, PDQ_NULL;

typedef struct {
	PDQ_rr rr;
	PDQ_name mname;
	PDQ_name rname;
	uint32_t serial;
	 int32_t refresh;
	 int32_t retry;
	 int32_t expire;
	uint32_t minimum;
} PDQ_SOA;

typedef struct {
	PDQ_rr rr;
	PDQ_name cpu;
	PDQ_name os;
} PDQ_HINFO;

typedef struct {
	PDQ_rr rr;
	PDQ_name rmailbx;
	PDQ_name emailbx;
} PDQ_MINFO;

#ifndef PDQ_TIMEOUT_START
#define PDQ_TIMEOUT_START		3
#endif

#ifndef PDQ_TIMEOUT_MAX
/* The overall time that pdqWait() and pdqWaitAll() are allowed.
 * With an initial timeout of 3 seconds, doubling every interation,
 * limited to 4 iterations, they will take 45 seconds to timeout.
 */
#define PDQ_TIMEOUT_MAX			(PDQ_TIMEOUT_START+(PDQ_TIMEOUT_START*2)+(PDQ_TIMEOUT_START*4)+(PDQ_TIMEOUT_START*8))
#endif

/***********************************************************************
 *** "Class" Methods
 ***********************************************************************/

/**
 * @param level
 *	Set debug level. The higher the more verbose.  Zero is silent.
 */
extern void pdqSetDebug(int level);

/*
 * @param flag
 *	Set true to query NS servers, per pdqQuery, in round robin order
 *	according to the order defined in resolv.conf. Set false (default)
 *	to query all the NS servers at the same time.
 */
extern void pdqSetRoundRobin(int flag);

/**
 * (Re)Load the resolv.conf file. Currently only nameserver lines
 * are recognised.
 *
 * @return
 *	Zero (0) on success otherwise -1 one error.
 */
extern int pdqInit(void);

/**
 * Terminate the DNS subsystem.
 */
extern void pdqFini(void);

/**
 * @param seconds
 *	Set the default maximum timeout value for any lookup.
 *	This is the timeout initially assigned to a PDQ instance
 *	by pdqOpen(). Affects pdqFetch(), pdqGet().
 */
extern void pdqMaxTimeout(unsigned seconds);

/**
 * @param seconds
 *	Set the default initial timeout value for any lookup.
 */
extern void pdqInitialTimeout(unsigned seconds);

/**
 * @param className
 *	Class name to map to a PDQ_CLASS_* code. The name is case insensitive.
 *
 * @return
 *	A DNS class code based on RFC 1035 defined classes.
 */
extern PDQ_class pdqClassCode(const char *className);

/**
 * @param classCode
 *	A DNS class code based on RFC 1035 defined classes.
 *
 * @return
 *	A constant C string
 */
extern const char *pdqClassName(PDQ_class classCode);

/**
 * @param typeName
 *	Type name to map to a PDQ_TYPE_* code. The name is case insensitive.
 *
 * @return
 *	A DNS type code based on RFC 1035, 1886, 3596, and 2874 defined
 *	types.
 */
extern PDQ_type pdqTypeCode(const char *typeName);

/**
 * @param typeCode
 *	A DNS PDQ_TYPE_ code based on RFC 1035, 1886, 3596, and 2874 defined
 *	types.
 *
 * @return
 *	A constant C string
 */
extern const char *pdqTypeName(PDQ_type typeCode);

/**
 * @param rcode
 *
 * @return
 *	A pointer to a C string name for the rcode.
 */
extern const char *pdqRcodeName(PDQ_rcode rcode);

/**
 * @param record
 *	A single DNS A/AAAA resource record.
 *
 * @return
 *	A C string for the IP address. Otherwise an error string in the event
 *	record is: NULL, PDQ_CNAME_TOO_DEEP, PDQ_CNAME_IS_CIRCULAR, or the
 *	record is not PDQ_RCODE_OK.
 *
 * @see
 *	pdqFindListName
 */
extern const char *pdqGetAddress(PDQ_rr *record);

/**
 * @param type
 *	A DNS PDQ_TYPE_ code based on RFC 1035, 1886, 3596, and 2874 defined
 *	types.
 *
 * @return
 *	Size of the type's structure.
 */
extern size_t pdqSizeOfType(PDQ_type type);

/***********************************************************************
 *** Instance Methods
 ***********************************************************************/

/**
 * @return
 *	A PDQ structure for handling one or more DNS queries.
 */
extern PDQ *pdqOpen(void);

/**
 * @param pdq
 *	A PDQ structure to cleanup.
 */
extern void pdqClose(PDQ *pdq);

/**
 * @param seconds
 * 	Set the maximum timeout for lookups. This will override
 *	the timeout assigned by pdqMaxTimeout() when pdqOpen()
 *	created this PDQ instance.
 */
extern void pdqSetTimeout(PDQ *pdq, unsigned seconds);

/**
 * @param pdq
 *	A PDQ structure pointer for handling queries.
 *
 * @param class
 *	A PDQ_CLASS_ code of the DNS record class to find.
 *
 * @param type
 *	A PDQ_TYPE_ code of the DNS record type to find.
 *
 * @param name
 *	A domain, host, or reversed-IP name to find.
 *
 * @param ns
 *	The name or IP address of a specific DNS name server to
 *	query. NULL if the system configued name servers should
 *	be used.
 *
 * @return
 *	Zero on successful posting of the request. Otherwise
 *	-1 on error in posting the request.
 */
extern int pdqQuery(PDQ *pdq, PDQ_class class, PDQ_type type, const char *name, const char *ns);

/**
 * Remove all outstanding queries from the list of requests.
 *
 * @param pdq
 *	A PDQ structure pointer for handling queries.
 */
extern void pdqQueryRemoveAll(PDQ *pdq);

/**
 * @param pdq
 *	A PDQ structure pointer for handling queries.
 *
 * @param ms
 *	Timeout in milliseconds to wait for an answer.
 *
 * @return
 *	A PDQ_rr pointer to the head of records list or NULL if
 *	no result found. It is the caller's responsibility to
 *	pdqFree() this list when done.
 */
extern PDQ_rr *pdqPoll(PDQ *pdq, unsigned ms);

/**
 * @param pdq
 *	A PDQ structure pointer for handling queries.
 *
 * @return
 *	A PDQ_rr pointer to the head of records list or NULL if
 *	no result found. It is the caller's responsibility to
 *	pdqFree() this list when done.
 */
extern PDQ_rr *pdqWait(PDQ *pdq);

/**
 * @param pdq
 *	A PDQ structure pointer for handling queries.
 *
 * @return
 *	A PDQ_rr pointer to the head of records list or NULL if
 *	no result found. It is the caller's responsibility to
 *	pdqFree() this list when done.
 */
extern PDQ_rr *pdqWaitAll(PDQ *pdq);

/***********************************************************************
 *** Convience Functions (pdqGet*, pdqFetch*)
 ***********************************************************************/

/**
 * @param pdq
 *	A PDQ structure pointer for handling queries.
 *
 * @param class
 *	A PDQ_CLASS_ code of the DNS record class to find.
 *
 * @param type
 *	A PDQ_TYPE_ code of the DNS record type to find.
 *
 * @param name
 *	A domain, host, or reversed-IP name to find.
 *
 * @param ns
 *	The name or IP address of a specific DNS name server to
 *	query. NULL if the system configued name servers should
 *	be used.
 *
 * @return
 *	A PDQ_rr pointer to the head of records list or NULL if
 *	no result found, or error occured and errno was set. It
 *	is the caller's responsibility to pdqListFree() this list
 *	when done.
 *
 * @note
 *	This is a convience function that combines the necessary
 *	steps to perform a single lookup with an open PDQ session
 *	using pdqQuery(), pdqWaitAll().
 *
 *	For MX, NS, and SOA lookups, it will also perform the lookups
 *	for the A and/or AAAA records, and handle the "implicit MX
 *	0 rule" from RFC 2821. .
 *
 *	Depending on the application, it might be nessary to call
 *	pdqQueryRemoveAll() before calling this function in order to
 *	discard any incomplete queries previously queued by earlier
 *	calls to pdqQuery().
 */
extern PDQ_rr *pdqGet(PDQ *pdq, PDQ_class class, PDQ_type type, const char *name, const char *ns);

/**
 * @param class
 *	A PDQ_CLASS_ code of the DNS record class to find.
 *
 * @param type
 *	A PDQ_TYPE_ code of the DNS record type to find.
 *
 * @param name
 *	A domain, host, or reversed-IP name to find.
 *
 * @param ns
 *	The name or IP address of a specific DNS name server to
 *	query. NULL if the system configued name servers should
 *	be used.
 *
 * @return
 *	A PDQ_rr pointer to the head of records list or NULL if
 *	no result found, or error occured and errno was set. It
 *	is the caller's responsibility to pdqListFree() this list
 *	when done.
 *
 * @note
 *	This is a convience function that combines the necessary
 *	steps to perform a single lookup using pdqOpen(), pdqGet(),
 *	and pdqClose().
 *
 *	For MX, NS, and SOA lookups, it will also perform the lookups
 *	for the A and/or AAAA records, and handle the "implicit MX
 *	0 rule" from RFC 2821.
 */
extern PDQ_rr *pdqFetch(PDQ_class class, PDQ_type type, const char *name, const char *ns);

/**
 * @param pdq
 *	A PDQ structure pointer for handling queries.
 *
 * @param class
 *	A PDQ_CLASS_ code of the DNS record class to find.
 *
 * @param type
 *	A PDQ_TYPE_ code of the DNS record type to find.
 *
 * @param prefix_name
 *	A domain, host, or IP name to find.
 *
 * @param suffix_list
 *	A list of DNS black/white lists.
 *
 * @param wait_fn
 *	Specify pdqWait or pdqWaitAll.
 *
 * @return
 *	A PDQ_rr pointer to the head of records list or NULL if
 *	no result found. It is the caller's responsibility to
 *	pdqFree() this list when done.
 *
 * @note
 *	This is a convience function that combines the necessary
 *	steps to perform an asynchronus lookup of DNS based lists
 *	with an open PDQ session.
 *
 *	Depending on the application, it might be nessary to call
 *	pdqQueryRemoveAll() before calling this function in order to
 *	discard any incomplete queries previously queued by earlier
 *	calls to pdqQuery().
 */
extern PDQ_rr *pdqGetDnsList(PDQ *pdq, PDQ_class class, PDQ_type type, const char *prefix_name, const char **suffix_list, PDQ_rr *(*wait_fn)(PDQ *));

/**
 * @param class
 *	A PDQ_CLASS_ code of the DNS record class to find.
 *
 * @param type
 *	A PDQ_TYPE_ code of the DNS record type to find.
 *
 * @param prefix_name
 *	A domain, host, or IP name to find.
 *
 * @param suffix_list
 *	A list of DNS black/white lists.
 *
 * @param wait_fn
 *	Specify pdqWait or pdqWaitAll.
 *
 * @return
 *	A PDQ_rr pointer to the head of records list or NULL if
 *	no result found. It is the caller's responsibility to
 *	pdqFree() this list when done.
 *
 * @note
 *	This is a convience function that combines the necessary
 *	steps to perform an asynchronus lookup of DNS based lists
 *	using pdqOpen(), pdqGetDnsList(), and pdqClose().
 */
extern PDQ_rr *pdqFetchDnsList(PDQ_class class, PDQ_type type, const char *prefix_name, const char **suffix_list, PDQ_rr *(*wait_fn)(PDQ *));

/**
 * @param pdq
 *	A PDQ structure pointer for handling queries.
 *
 * @param class
 *	A PDQ_CLASS_ code of the DNS record class to find.
 *
 * @param name
 *	A host name to find.
 *
 * @return
 *	A PDQ_rr pointer to the head of A and/or AAAA records list
 *	or NULL if no result found. It is the caller's responsibility
 *	to pdqFree() this list when done.
 *
 * @note
 *	This is a convience function.
 */
extern PDQ_rr *pdqGet5A(PDQ *pdq, PDQ_class class, const char *name);

/**
 * @param class
 *	A PDQ_CLASS_ code of the DNS record class to find.
 *
 * @param name
 *	A host name to find.
 *
 * @return
 *	A PDQ_rr pointer to the head of A and/or AAAA records list
 *	or NULL if no result found. It is the caller's responsibility
 *	to pdqFree() this list when done.
 *
 * @note
 *	This is a convience function.
 */
extern PDQ_rr *pdqFetch5A(PDQ_class class, const char *name);

/**
 * @param pdq
 *	A PDQ structure pointer for handling queries.
 *
 * @param class
 *	A PDQ_CLASS_ code of the DNS record class to find.
 *
 * @param name
 *	A domain name for which to find MX records and associated
 *	A/AAAA records.
 *
 * @param is_ip_mask
 *	A bit vector of IS_IP_ flags. See isReservedIP*() family of
 *	functions in com/snert/lib/net/network.h.
 *
 * @return
 *	A PDQ_rr pointer to the head of MX and A/AAAA records list
 *	or NULL if no result found. A/AAAA records that match the
 *	is_ip_mask are removed, after which any MX without an A/AAAA
 *	record is also removed. It is the caller's responsibility
 *	to pdqFree() this list when done.
 *
 * @note
 *	This is a convience function.
 */
extern PDQ_rr *pdqGetMX(PDQ *pdq, PDQ_class class, const char *name, long is_ip_mask);

/**
 * @param class
 *	A PDQ_CLASS_ code of the DNS record class to find.
 *
 * @param name
 *	A domain name for which to find MX records and associated
 *	A/AAAA records.
 *
 * @param is_ip_mask
 *	A bit vector of IS_IP_ flags. See isReservedIP*() family of
 *	functions in com/snert/lib/net/network.h.
 *
 * @return
 *	A PDQ_rr pointer to the head of MX and A/AAAA records list
 *	or NULL if no result found. A/AAAA records that match the
 *	is_ip_mask are removed, after which any MX without an A/AAAA
 *	record is also removed. It is the caller's responsibility
 *	to pdqFree() this list when done.
 *
 * @note
 *	This is a convience function.
 */
extern PDQ_rr *pdqFetchMX(PDQ_class class, const char *name, long is_ip_mask);

/***********************************************************************
 *** Record & List Support
 ***********************************************************************/

/**
 * @param type
 *	A DNS PDQ_TYPE_ code based on RFC 1035, 1886, 3596, and 2874 defined
 *	types.
 *
 * @return
 *	A pointer to a PDQ_rr record of the given type.
 */
extern PDQ_rr *pdqCreate(PDQ_type type);

/**
 * @param record
 *	A pointer to a previous allocated PDQ_rr structure to be
 *	duplicated.
 *
 * @return
 *	A copy of the given PDQ_rr structure or NULL on error. It is
 *	the caller's responsibility to pdqFree() this record when done.
 *	Note that pointer returned is a single record. To duplicate an
 *	entire PDQ_rr list, use pdqListClone().
 */
extern PDQ_rr *pdqDup(PDQ_rr *record);

/**
 * @param _list
 *	Release memory associated with a PDQ_rr pointer previouly
 *	obtained from pdqCreate(), pdqDup(), pdqFetch(), pdqGet(),
 *	or pdqListClone().
 */
extern void pdqListFree(void *_list);
#define pdqFree	pdqListFree

/**
 * @param a
 *	A pointer to a PDQ_rr list.
 *
 * @param b
 *	A pointer to a PDQ_rr list.
 *
 * @return
 *	A pointer to a PDQ_rr list where list ``b'' has been linked
 *	to the end of list ``a''.
 */
extern PDQ_rr *pdqListAppend(PDQ_rr *a, PDQ_rr *b);

/**
 * @param record
 *	A pointer to a previous allocated PDQ_rr list to be cloned.
 *
 * @return
 *	A clone of the given PDQ_rr list or NULL on error. It is the
 *	caller's responsibility to pdqFree() this record list when done.
 */
extern PDQ_rr *pdqListClone(PDQ_rr *record);

/**
 * @param fp
 *	An output file stream pointer.
 *
 * @param list
 *	A list of DNS resource records to dump.
 *	The output will be one line per record.
 *
 * @see
 *	pdqDump, pdqListLog, pdqLog
 */
extern void pdqListDump(FILE *fp, PDQ_rr *list);

/**
 * @param fp
 *	An output file stream pointer.
 *
 * @param record
 *	A single DNS resource record to dump.
 *	The output will be one line per record.
 *
 * @see
 *	pdqListDump, pdqListLog, pdqLog
 */
extern void pdqDump(FILE *fp, PDQ_rr *record);

/**
 * @param list
 *	A pointer to a PDQ_rr list.
 *
 * @param class
 *	A PDQ_CLASS_ code of the DNS record class to find.
 *
 * @param type
 *	A PDQ_TYPE_ code of the DNS record type to find.
 *
 * @param rcode
 *	A PDQ_RCODE_ code of the DNS record return code to find.
 *
 * @param name
 *	A record name to find. NULL for any. CNAME redirection
 *	is NOT followed.
 *
 * @return
 *	A pointer to the first PDQ_rr record found, or NULL if not found.
 */
extern PDQ_rr *pdqListFind(PDQ_rr *list, PDQ_class class, PDQ_type type, PDQ_rcode rcode, const char *name);

/**
 * @param list
 *	A pointer to a PDQ_rr list.
 *
 * @param class
 *	A PDQ_CLASS_ code of the DNS record class to find.
 *
 * @param type
 *	A PDQ_TYPE_ code of the DNS record type to find. Specify the
 *	special type PDQ_TYPE_5A to find either A or AAAA records.
 *
 * @param name
 *	A record name to find. CNAME redirection is followed.
 *
 * @return
 *	NULL if not found, PDQ_CNAME_TOO_DEEP, or PDQ_CNAME_IS_CIRCULAR.
 *	Otherwise a pointer to PDQ_rr A or AAAA record.
 */
extern PDQ_rr *pdqListFindName(PDQ_rr *list, PDQ_class class, PDQ_type type, const char *name);

/**
 * @param list
 *	A pointer to a PDQ_rr list.
 *
 * @param class
 *	A PDQ_CLASS_ code of the DNS record class to find.
 *
 * @param type
 *	A PDQ_TYPE_ code of the DNS record type to find.
 *
 * @param host
 *	A record's host name value to find.
 *
 * @return
 *	A pointer to the first PDQ_rr record found or NULL if not found.
 *	Only records of type PDQ_CNAME, PDQ_MX, PDQ_NS, PDQ_PTR, and PDQ_SOA
 *	are returned.
 */
extern PDQ_rr *pdqListFindHost(PDQ_rr *list, PDQ_class class, PDQ_type type, const char *host);

/**
 * @param list
 *	A pointer to a PDQ_rr list.
 *
 * @param class
 *	A PDQ_CLASS_ code of the DNS record class to find.
 *
 * @param type
 *	A PDQ_TYPE_A,  PDQ_TYPE_AAAA, or PDQ_TYPE_5A code of the DNS
 *	record type to find. PDQ_TYPE_5A looks for both A and AAAA
 *	records.
 *
 * @param ip
 *	A C string of the record's IP address value to find.
 *
 * @return
 *	A pointer to the first PDQ_rr record found or NULL if not found.
 *	Only records of type PDQ_A or PDQ_AAAA are returned.
 */
extern PDQ_rr *pdqListFindAddress(PDQ_rr *list, PDQ_class class, PDQ_type type, const char *ip);

/**
 * @param list
 *	A pointer to a PDQ_rr list.
 *
 * @param class
 *	A PDQ_CLASS_ code of the DNS record class to find.
 *
 * @param type
 *	A PDQ_TYPE_A,  PDQ_TYPE_AAAA, or PDQ_TYPE_5A code of the DNS
 *	record type to find. PDQ_TYPE_5A looks for bother A and AAAA
 *	records.
 *
 * @param ipv6
 *	A record's IP address value to find.
 *
 * @return
 *	A pointer to the first PDQ_rr record found or NULL if not found.
 *	Only records of type PDQ_A or PDQ_AAAA are returned.
 */
extern PDQ_rr *pdqListFindIP(PDQ_rr *list, PDQ_class class, PDQ_type type, const unsigned char ipv6[IPV6_BYTE_LENGTH]);

/**
 * @param record
 *	A pointer to a PDQ_rr list.
 *
 * @param index
 *	The index of the record to fetch.
 *
 * @return
 *	A pointer to the Nth record or NULL if the Nth record does not
 *	exist.
 */
extern PDQ_rr *pdqListGet(PDQ_rr *record, unsigned index);

/**
 * @param list
 *	A pointer to a PDQ_rr list.
 *
 * @return
 *	A pointer to the last record in the list.
 */
extern PDQ_rr *pdqListLast(PDQ_rr *list);

/**
 * @param record
 *	A pointer to a PDQ_rr list.
 *
 * @return
 *	Number of entries in the list.
 */
extern unsigned pdqListLength(PDQ_rr *record);

/**
 * @param list
 *	A list of DNS resource records to dump via syslog.
 *	The output will be one log line per record.
 *
 * @see
 *	pdqLog, pdqListDump, pdqDump
 */
extern void pdqListLog(PDQ_rr *list);

/**
 * @param record
 *	A singleDNS resource record to dump via syslog.
 *	The output will be one log line per record.
 *
 * @see
 *	pdqListLog, pdqListDump, pdqDump
 */
extern void pdqLog(PDQ_rr *record);

/**
 * @param a_record
 *	A pointer to a PDQ_rr list containing MX, NS, SOA, A, or AAAA records.
 *
 * @param is_ip_mask
 *      A bit vector of IS_IP_ flags. See isReservedIP*() family of
 *      functions in com/snert/lib/net/network.h.
 *
 * @return
 *	The updated head of the list or NULL if the list is empty. This list
 *	will only contain valid MX/NS/SOA records with matching A/AAAA which
 *	themselves have an IP address "at this time" (ie. PDQ_RCODE_SERVER
 *	results are discarded).
 */
extern PDQ_rr *pdqListPrune(PDQ_rr *a_record, long is_ip_mask);

/**
 * @param list
 *	A pointer to a PDQ_rr list containing A or AAAA records.
 *
 * @param is_ip_mask
 *	A bit vector of IS_IP_ flags. See isReservedIP*() family of
 *	functions in com/snert/lib/net/network.h.
 *
 * @param must_have_ip
 *	When true, keep only A/AAAA records that return PDQ_RCODE_OK
 *	ie. they have an IP address "at this point in time". Otherwise
 *	keep A/AAAA records that returned PDQ_RCODE_OK or PDQ_RCODE_SERVER
 *	(temporary DNS failure).
 *
 * @return
 *	The updated head of the list or NULL if the list is empty.
 *	The list will only contain A/AAAA records that successfully
 *	returned an IP address. Other record types remain untouched.
 */
extern PDQ_rr *pdqListPrune5A(PDQ_rr *list, long is_ip_mask, int must_have_ip);

/**
 * @param list
 *	A pointer to a PDQ_rr list containing MX, NS, SOA, A, or AAAA records.
 *
 * @return
 *	The updated head of the list or NULL if the list is empty. This list
 *	will only contain valid MX/NS/SOA records with matching A/AAAA records.
 */
extern PDQ_rr *pdqListPruneMatch(PDQ_rr *list);

/**
 * @param list
 *	A pointer to a PDQ_rr list.
 *
 * @param record
 *	A pointer to a record in the PDQ_rr list to be removed.
 *
 * @return
 *	A pointer to the new head of the PDQ_rr list.
 */
extern PDQ_rr *pdqListRemove(PDQ_rr *list, PDQ_rr *record);

/**
 * @param list
 *	A pointer to a PDQ_rr list to reverse.
 *
 * @return
 *	A pointer to the new head of the PDQ_rr list.
 */
extern PDQ_rr *pdqListReverse(PDQ_rr *list);

/**
 * @param list
 *	A pointer to a PDQ_rr pointer in which to pass back a
 *	list of records. It is the caller's responsibility to
 *	pdqFree() this list when done.
 *
 * @return
 *	True if the list contains a CNAME loop.
 */
extern int pdqIsCircular(PDQ_rr *list);

/**
 * @param record
 *	A pointer to a previous allocated PDQ_rr record.
 *
 * @return
 *	Size of the type's structure.
 */
extern size_t pdqSizeOf(PDQ_rr *record);

/***********************************************************************
 *** PDQ Application Options
 ***********************************************************************/

#include <com/snert/lib/util/option.h>

extern Option optDnsMaxTimeout;
extern Option optDnsRoundRobin;

#define PDQ_OPTIONS_TABLE \
	&optDnsMaxTimeout, \
	&optDnsRoundRobin

#define PDQ_OPTIONS_SETTING(debug) \
	pdqSetDebug(debug); \
	pdqMaxTimeout(optDnsMaxTimeout.value); \
	pdqSetRoundRobin(optDnsRoundRobin.value)

/***********************************************************************
 ***
 ***********************************************************************/

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_net_pdq_h__ */
