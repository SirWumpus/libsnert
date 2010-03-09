/*
 * dnsList.h
 *
 * Copyright 2008, 2010 by Anthony Howe. All rights reserved.
 */

#ifndef __com_snert_lib_net_dnsList_h__
#define __com_snert_lib_net_dnsList_h__	1

#ifdef __cplusplus
extern "C" {
#endif

/**
 * dnsList API Function Relationship
 * ---------------------------------
 *
 * 			pdqGetDnsList:
 * 			  string+suffix process loop
 * 			^
 * 			|
 *
 * 			dnsLisQueryString
 * 			  check/maintain names_seen
 * 			  do single name lookup
 * 			  process result
 * 			^
 * 			|
 * +--------------------+-----------------------+
 * ^			^			^
 * |			|			|
 *
 * dnsListQueryIP	dnsListQueryName	dnsListQueryMD5
 *   name 5A lookup	  assert host/domain	  generate MD5 hash
 *   process IP lists,    lookup, bar IP	  single lookup of hash
 *   reversing IP	^			^
 * ^			|			|
 * |			|			|
 * |
 * +----------- dnsListQueryDomain		dnsListQueryMail
 * ^		  TLD/registry process		  localpart exclusions
 * |		  sub-domain right-to-left	  domains permitted
 * |			^
 * |			|
 * |			|
 * |
 * +----------- dnsListQueryNs
 * 		  left-to-right search for
 * 		  NS hosts and IPs
 */

/***********************************************************************
 ***
 ***********************************************************************/

#include <com/snert/lib/net/pdq.h>
#include <com/snert/lib/net/network.h>
#include <com/snert/lib/type/Vector.h>

typedef struct dns_list_code {
	struct dns_list_code *next;
	unsigned char code[IPV6_BYTE_LENGTH];
	char *action;
} DnsListCode;

typedef struct dns_list_suffix {
	char *suffix;
	Vector codes;
	unsigned long mask;
} DnsListSuffix;

typedef struct {
	Vector suffixes;
	unsigned long *masks;
	const char *query_server;
} DnsList;

typedef enum {
	DNS_LIST_LOG_HIT	= 1,
	DNS_LIST_LOG_MISS	= 2,
} DnsListLogResult;

/**
 * @param level
 *	Set debug level. The higher the more verbose.  Zero is silent.
 */
extern void dnsListSetDebug(int level);

/**
 * @param flag
 *	When set true, wait for all the replies from DNS lists; otherwise
 *	wait for only the first reply (default);
 */
extern void dnsListSetWaitAll(int flag);

/**
 *
 */
extern int dnsListLogOpen(const char *filename, DnsListLogResult what);
extern void dnsListLogWhat(DnsListLogResult what);
extern void dnsListLogSys(const char *token, const char *name, const char *list_name);
extern void dnsListLog(const char *token, const char *name, const char *list_name);
extern void dnsListLogClose(void);

extern const char *dnsListIsNameListed(DnsList *dns_list, const char *name, PDQ_rr *list);

/**
 * @param _dns_list
 *	A DnsList structure to cleanup.
 */
extern void dnsListFree(void *_dns_list);

/**
 * @param list_string
 *	A semi-colon separated list of DNS list suffixes to consult.
 *
 * 	Aggregate lists that return bit-vector are supported using
 * 	suffix/mask. Without a /mask, suffix is the same as
 *	suffix/0x00FFFFFE. surbl.org and uribl.com use bit-vector
 *	A record.
 *
 *	Aggregate lists that return a multi-home list of records are
 *	not yet supported, beyond simple membership. spamhaus.org uses
 *	multi-homed A records.
 *
 * @return
 *	A pointer to a DnsList.
 */
extern DnsList *dnsListCreate(const char *list_string);

/**
 * @param dns_list
 *	A pointer to a DnsList.
 *
 * @param pdq
 *	A pointer to PDQ structure to use for the query.
 *
 * @param names_seen
 *	A pointer to vector of previously queried names. If name
 *	is present in this vector, then the query is skipped and
 *	NULL immiediately returned. The query name will be added
 *	to this vector.	Specify NULL to skip this check.
 *
 * @param name
 *	An arbitrary string to query in one or more DNS lists.
 *
 * @return
 *	A C string pointer to a list name in which name is a member.
 *	Otherwise NULL if name was not found in a DNS list.
 */
extern const char *dnsListQueryString(DnsList *dns_list, PDQ *pdq, Vector names_seen, const char *name);

/**
 * @param dns_list
 *	A pointer to a DnsList.
 *
 * @param pdq
 *	A pointer to PDQ structure to use for the query.
 *
 * @param names_seen
 *	A pointer to vector of previously queried names. If name
 *	is present in this vector, then the query is skipped and
 *	NULL immiediately returned. The query name will be added
 *	to this vector.	Specify NULL to skip this check.
 *
 * @param name
 *	A host or domain name to query in one or more DNS lists.
 *	An IP address will result in NULL being returned.
 *
 * @return
 *	A C string pointer to a list name in which name is a member.
 *	Otherwise NULL if name was not found in a DNS list.
 */
extern const char *dnsListQueryName(DnsList *dns_list, PDQ *pdq, Vector names_seen, const char *name);

/**
 * @param dns_list
 *	A pointer to a DnsList.
 *
 * @param pdq
 *	A pointer to PDQ structure to use for the query.
 *
 * @param names_seen
 *	A pointer to vector of previously looked up names. If name
 *	is present in this vector, then the query is skipped and
 *	NULL immiediately returned. The query name will be added
 *	to this vector.	Specify NULL to skip this check.
 *
 * @param name
 *	An IP, host name, or domain name. In the case of a host or
 *	domain name, their A/AAAA records are first found and the
 *	resulting list of IP addresses checked.
 *
 * @return
 *	A C string pointer to a list name in which name is a member.
 *	Otherwise NULL if name was not found in a DNS list.
 */
extern const char *dnsListQueryIP(DnsList *dns_list, PDQ *pdq, Vector names_seen, const char *name);

/**
 * @param dns_list
 *	A pointer to a DnsList.
 *
 * @param pdq
 *	A pointer to PDQ structure to use for the query.
 *
 * @param names_seen
 *	A pointer to vector of previously looked up names. If name
 *	is present in this vector, then the query is skipped and
 *	NULL immiediately returned. The query name will be added
 *	to this vector.	Specify NULL to skip this check.
 *
 * @param test_sub_domains
 *	If true, then test sub-domains from right to left. That is
 *	the domain starting with the label immediately preceding the
 *	top-level-domain is passed to dnsListQueryName. If NULL is
 *	returned, then repeat with the next preceding label, until a
 *	a list name is return or the entire name has been queried.
 *
 *	Otherwise when false, the domain starting with the label
 *	immediately preceding the top-level-domain is passed to
 *	dnsListQueryName.
 *
 * @param name
 *	A host, domain, or IP to query in one or more DNS lists.
 *
 * @return
 *	A C string pointer to a list name in which name is a member.
 *	Otherwise NULL if name was not found in a DNS list.
 */
extern const char *dnsListQueryDomain(DnsList *dns_list, PDQ *pdq, Vector names_seen, int test_sub_domains, const char *name);

/**
 * @param ns_bl
 *	A pointer to a DnsList.
 *
 * @param ns_ip_bl
 *	A pointer to a DnsList.
 *
 * @param pdq
 *	A pointer to PDQ structure to use for the query.
 *
 * @param names_seen
 *	A pointer to vector of previously looked up names. If name
 *	is present in this vector, then the query is skipped and
 *	NULL immiediately returned. The query name will be added
 *	to this vector.	Specify NULL to skip this check.
 *
 * @param name
 *	A host or domain name who's NS records are first found and
 *	then passed to dnsListQueryDomain.
 *
 * @return
 *	A C string pointer to a list name in which name is a member.
 *	Otherwise NULL if name was not found in a DNS list.
 */
extern const char *dnsListQueryNs(DnsList *ns_bl, DnsList *ns_ip_bl, PDQ *pdq, Vector names_seen, const char *name);

/**
 * @param dns_list
 *	A pointer to a DnsList.
 *
 * @param pdq
 *	A pointer to PDQ structure to use for the query.
 *
 * @param already_seen
 *	A pointer to vector of previously looked up mails. If mail
 *	is present in this vector, then the query is skipped and
 *	NULL immiediately returned. The query mail will be added
 *	to this vector.	Specify NULL to skip this check.
 *
 * @param mail
 *	A C string is hashed then passed to dnsListQueryString.
 *
 * @return
 *	A C string pointer to a list name in which name is a member.
 *	Otherwise NULL if name was not found in a DNS list.
 */
extern const char *dnsListQueryMD5(DnsList *dns_list, PDQ *pdq, Vector already_seen, const char *string);

/**
 * @param dns_list
 *	A pointer to a DnsList.
 *
 * @param pdq
 *	A pointer to PDQ structure to use for the query.
 *
 * @param limited_domains
 *	A list of domain glob-like patterns for which to test against dns_list,
 *	typically free mail services. This reduces the load on public black lists.
 *	Specify NULL to test all domains.
 *
 * @param names_seen
 *	A pointer to vector of previously looked up mails. If mail
 *	is present in this vector, then the query is skipped and
 *	NULL immiediately returned. The query mail will be added
 *	to this vector.	Specify NULL to skip this check.
 *
 * @param string
 *	A mail address is hashed then passed to dnsListQueryMD5.
 *
 * @return
 *	A C string pointer to a list name in which name is a member.
 *	Otherwise NULL if name was not found in a DNS list.
 */
extern const char *dnsListQueryMail(DnsList *dns_list, PDQ *pdq, Vector domains, Vector mails_seen, const char *mail);

/***********************************************************************
 *** dnsList Application Options
 ***********************************************************************/

#include <com/snert/lib/util/option.h>

extern Option optDnsListLogFile;
extern Option optDnsListLogWhat;

#define DNS_LIST_OPTIONS_TABLE \
	&optDnsListLogFile, \
	&optDnsListLogWhat

#define DNS_LIST_OPTIONS_SETTING(debug) \
	dnsListSetDebug(debug); \
	dnsListLogOpen(optDnsListLogFile.string, optDnsListLogWhat.value);

/***********************************************************************
 ***
 ***********************************************************************/

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_net_dnsList_h__ */
