/*
 * dnsList.h
 *
 * Copyright 2008 by Anthony Howe. All rights reserved.
 */

#ifndef __com_snert_lib_net_dnsList_h__
#define __com_snert_lib_net_dnsList_h__	1

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************************
 ***
 ***********************************************************************/

#include <com/snert/lib/net/pdq.h>
#include <com/snert/lib/type/Vector.h>

typedef struct {
	Vector suffixes;
	unsigned long *masks;
} DnsList;

/**
 * @param level
 *	Set debug level. The higher the more verbose.  Zero is silent.
 */
extern void dnsListSetDebug(int level);

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
 *	A host or domain name to query in one or more DNS lists.
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
 *	A host or domain name to query in one or more DNS lists.
 *
 * @return
 *	A C string pointer to a list name in which name is a member.
 *	Otherwise NULL if name was not found in a DNS list.
 */
extern const char *dnsListQuery(DnsList *dns_list, PDQ *pdq, Vector names_seen, int test_sub_domains, const char *name);

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
 *	A host or domain name whos NS records are first found and
 *	then passed to dnsListQuery.
 *
 * @return
 *	A C string pointer to a list name in which name is a member.
 *	Otherwise NULL if name was not found in a DNS list.
 */
extern const char *dnsListQueryNs(DnsList *dns_list, PDQ *pdq, Vector names_seen, const char *name);

/***********************************************************************
 ***
 ***********************************************************************/

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_net_dnsList_h__ */
