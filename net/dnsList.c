/**
 * dnsList.c
 *
 * Copyright 2008 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <stdio.h>
#include <stdlib.h>

#ifndef __MINGW32__
# if defined(HAVE_SYSLOG_H)
#  include <syslog.h>
# endif
#endif
#include <com/snert/lib/io/Log.h>

#include <com/snert/lib/sys/Time.h>
#include <com/snert/lib/mail/tlds.h>
#include <com/snert/lib/util/md5.h>
#include <com/snert/lib/util/Text.h>
#include <com/snert/lib/net/dnsList.h>

/***********************************************************************
 ***
 ***********************************************************************/

static int debug;
static FILE *log_file;
static DnsListLogResult log_what;

static const char usage_dns_list_log_file[] =
  "File name used to log DNS list lookup results separate from syslog.\n"
"# Intended for debugging only.\n"
"#"
;

Option optDnsListLogFile = { "dns-list-log-file", "", usage_dns_list_log_file };

static const char usage_dns_list_log_what[] =
  "What DNS list lookup results to log. 1 for successful lookups, 2 for\n"
"# unsuccessful lookups, 3 for both.\n"
"#"
;

Option optDnsListLogWhat = { "dns-list-log-what", "", usage_dns_list_log_what };

/***********************************************************************
 ***
 ***********************************************************************/

/**
 * @param level
 *	Set debug level. The higher the more verbose.  Zero is silent.
 */
void
dnsListSetDebug(int level)
{
	debug = level;
}

void
dnsListLogWhat(DnsListLogResult what)
{
	log_what = what & (DNS_LIST_LOG_HIT | DNS_LIST_LOG_MISS);
}

int
dnsListLogOpen(const char *filename, DnsListLogResult what)
{
	dnsListLogWhat(what);
	if ((log_file = fopen(filename, "a")) != NULL)
		setvbuf(log_file, NULL, _IOLBF, 0);

	return -(log_file == NULL);
}

void
dnsListLogClose(void)
{
	if (log_file != NULL)
		fclose(log_file);
}

void
dnsListLog(const char *id, const char *name, const char *list_name)
{
	char timestamp[40];

	if (log_file != NULL && name != NULL) {
		TimeStampAdd(timestamp, sizeof (timestamp));

		if ((log_what & DNS_LIST_LOG_MISS) && list_name == NULL)
			(void) fprintf(log_file, "%s %s %s \n", timestamp, id, name);
		else if ((log_what & DNS_LIST_LOG_HIT) && list_name != NULL)
			(void) fprintf(log_file, "%s %s %s %s\n", timestamp, id, name, list_name);
	}
}

void
dnsListLogSys(const char *id, const char *name, const char *list_name)
{
	char timestamp[40];

	if (name != NULL) {
		TimeStampAdd(timestamp, sizeof (timestamp));

		if ((log_what & DNS_LIST_LOG_MISS) && list_name == NULL)
			syslog(LOG_INFO, "%s %s %s \n", timestamp, id, name);
		else if ((log_what & DNS_LIST_LOG_HIT) && list_name != NULL)
			syslog(LOG_INFO, "%s %s %s %s\n", timestamp, id, name, list_name);
	}
}

/**
 * @param _dns_list
 *	A DnsList structure to cleanup.
 */
void
dnsListFree(void *_list)
{
	DnsList *list = _list;

	if (list != NULL) {
		VectorDestroy(list->suffixes);
		free(list->masks);
		free(list);
	}
}

/**
 * @param list_string
 *	A semi-colon separated list of DNS list suffixes to consult.
 *
 * 	Aggregate lists that return bit-vector are supported using
 * 	suffix/mask. Without a /mask, suffix is the same as
 *	suffix/0x00FFFFFE.
 *
 *	Aggregate lists that return a multi-home list of records are
 *	not yet supported, beyond simple membership.
 *
 * @return
 *	A pointer to a DnsList.
 */
DnsList *
dnsListCreate(const char *string)
{
	long i;
	size_t length;
	DnsList *list;
	char *slash, *suffix, *rooted;

	if (string == NULL || *string == '\0')
		goto error0;

	if ((list = malloc(sizeof (*list))) == NULL)
		goto error0;

	if ((list->suffixes = TextSplit(string, " ,;", 0)) == NULL)
		goto error1;

	if ((list->masks = calloc(sizeof (*list->masks), VectorLength(list->suffixes))) == NULL)
		goto error1;

	for (i = 0; i < VectorLength(list->suffixes); i++) {
		if ((suffix = VectorGet(list->suffixes, i)) == NULL)
			continue;

		/* Assert that the DNS list suffixes are rooted, ie.
		 * terminated by a dot. This will prevent wildcard
		 * lookups when resolv.conf specifies a 'search domain.com'
		 * pragma and domain.com has a wildcard entry then
		 * any NXDOMAIN returns result in .domain.com being
		 * added to the end of the lookup and the wildcard being
		 * returned.
		 */
		length = strlen(suffix);
		if (0 < length && suffix[length-1] != '.' && (rooted = malloc(length+2)) != NULL) {
			(void) TextCopy(rooted, length+2, suffix);
			suffix = rooted;
			suffix[length  ] = '.';
			suffix[length+1] = '\0';
			VectorSet(list->suffixes, i, suffix);
		}

		if ((slash = strchr(suffix, '/')) == NULL) {
			list->masks[i] = (unsigned long) ~0L;
		} else {
			list->masks[i] = (unsigned long) strtol(slash+1, NULL, 0);
			*slash = '\0';
		}
	}

	return list;
error1:
	dnsListFree(list);
error0:
	return NULL;
}

const char *
dnsListIsNameListed(DnsList *dns_list, const char *name, PDQ_rr *list)
{
	long i;
	PDQ_A *rr;
	unsigned long bits;
	const char **suffixes;

	suffixes = (const char **) VectorBase(dns_list->suffixes);
	for (rr = (PDQ_A *) list; rr != NULL; rr = (PDQ_A *) rr->rr.next) {
		if (rr->rr.rcode != PDQ_RCODE_OK || rr->rr.type != PDQ_TYPE_A)
			continue;

		if (TextInsensitiveStartsWith(rr->rr.name.string.value, name) < 0)
			continue;

		for (i = 0; suffixes[i] != NULL; i++) {
			if (strstr(rr->rr.name.string.value, suffixes[i]) == NULL)
				continue;

			bits = NET_GET_LONG(rr->address.ip.value + rr->address.ip.offset);

			if ((bits & dns_list->masks[i]) != 0
			&& isReservedIPv4(rr->address.ip.value + rr->address.ip.offset, IS_IP_LOCAL|IS_IP_THIS_NET)) {
				if (0 < debug)
					syslog(LOG_DEBUG, "found %s %s", rr->rr.name.string.value, rr->address.string.value);

				return suffixes[i];
			}
		}
	}

	return NULL;
}

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
const char *
dnsListQueryName(DnsList *dns_list, PDQ *pdq, Vector names_seen, const char *name)
{
	int offset = 0;
	PDQ_rr *answers;
	const char *list_name = NULL;
	char buffer[DOMAIN_STRING_LENGTH];

	if (dns_list == NULL || name == NULL || *name == '\0')
		return NULL;

	if (0 < spanIP(name)) {
		(void) reverseIp(name, buffer, sizeof (buffer), 0);
		name = buffer;
	}

	if (names_seen != NULL) {
		const char **seen;

		/* Check cache of previously tested hosts/domains. */
		for (seen = (const char **) VectorBase(names_seen); *seen != NULL; seen++) {
			if (TextInsensitiveCompare(name, *seen) == 0) {
				if (1 < debug)
					syslog(LOG_INFO, "name=%s previously checked", name);
				return NULL;
			}
		}

		(void) VectorAdd(names_seen, strdup(name));
	}

	answers = pdqGetDnsList(
		pdq, PDQ_CLASS_IN, PDQ_TYPE_A, name+offset,
		(const char **) VectorBase(dns_list->suffixes), pdqWait
	);

	if (answers != NULL) {
		list_name = dnsListIsNameListed(dns_list, name+offset, answers);
		pdqFree(answers);
	}

	return list_name;
}

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
const char *
dnsListQuery(DnsList *dns_list, PDQ *pdq, Vector names_seen, int test_sub_domains, const char *name)
{
	int offset;
	const char *list_name = NULL;

	if (dns_list == NULL || name == NULL || *name == '\0')
		return NULL;

	/* Find start of TLD. */
	offset = indexValidTLD(name);

	if (offset < 0) {
		unsigned char ipv6[IPV6_BYTE_LENGTH];

		if (parseIPv6(name, ipv6) <= 0)
			return NULL;

		/* Is an IP address. */
		offset = 0;
	}

	/* Scan domain and subdomains from right-to-left. */
	do {
		offset = strlrcspn(name, offset-1, ".");

		if ((list_name = dnsListQueryName(dns_list, pdq, names_seen, name+offset)) != NULL) {
			if (0 < debug)
				syslog(LOG_DEBUG, "%s listed in %s", name+offset, list_name);

			return list_name;
		}
	} while (test_sub_domains && 0 < offset);

	return NULL;
}

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
 *	A host or domain name whos A/AAAA records are first found and
 *	then passed to dnsListQueryName.
 *
 * @return
 *	A C string pointer to a list name in which name is a member.
 *	Otherwise NULL if name was not found in a DNS list.
 */
const char *
dnsListQueryIP(DnsList *dns_list, PDQ *pdq, Vector names_seen, const char *name)
{
	PDQ_rr *rr, *list;
	const char *list_name = NULL;

	if (dns_list == NULL || name == NULL || *name == '\0')
		return NULL;

	list = pdqGet5A(pdq, PDQ_CLASS_IN, name);

	for (rr = list; rr != NULL; rr = rr->next) {
		if (rr->rcode != PDQ_RCODE_OK || (rr->type != PDQ_TYPE_A && rr->type != PDQ_TYPE_AAAA))
			continue;

		/* Some domains specify a 127.0.0.0/8 address for
		 * an A recorded, like "anything.so". The whole
		 * TLD .so for Somalia, is a wild card record that
		 * maps to 127.0.0.2, which typically is a DNSBL
		 * test record that always fails.
		 */
		if (isReservedIPv6(((PDQ_AAAA *) rr)->address.ip.value, IS_IP_LOOPBACK|IS_IP_LOCALHOST))
			continue;

		list_name = dnsListQueryName(dns_list, pdq, names_seen, ((PDQ_AAAA *) rr)->address.string.value);
		if (list_name != NULL) {
			if (0 < debug)
				syslog(LOG_DEBUG, "%s [%s] listed in %s", name, ((PDQ_AAAA *) rr)->address.string.value, list_name);
			break;
		}
	}

	pdqFree(list);

	return list_name;
}

#ifndef NS_VERSION1
static const char *
dnsListQueryNs0(DnsList *dns_list, PDQ *pdq, Vector names_seen, int recurse, const char *name)
{
	PDQ_rr *rr, *ns_list;
	const char *list_name = NULL;

	if (dns_list == NULL || name == NULL || *name == '\0')
		return NULL;

	if ((ns_list = pdqGet(pdq, PDQ_CLASS_IN, PDQ_TYPE_NS, name, NULL)) != NULL) {
		for (rr = ns_list; rr != NULL; rr = rr->next) {
			if (0 < recurse && rr->rcode == PDQ_RCODE_OK && rr->type == PDQ_TYPE_SOA) {
				list_name = dnsListQueryNs0(dns_list, pdq, names_seen, recurse-1, rr->name.string.value);
				break;
			} else if (rr->rcode == PDQ_RCODE_OK && rr->type == PDQ_TYPE_NS) {
				recurse = 0;
				if ((list_name = dnsListQuery(dns_list, pdq, names_seen, 1, ((PDQ_PTR *) rr)->host.string.value)) != NULL)
					break;
			}
		}

		pdqFree(ns_list);
	}

	return list_name;
}
#endif

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
const char *
dnsListQueryNs(DnsList *dns_list, PDQ *pdq, Vector names_seen, const char *name)
{
#ifdef NS_VERSION1
	const char *list_name = NULL;
	PDQ_rr *rr, *ns_list, *soa_list;

	/* Find SOA domain handling the name in question. This
 	 * handles CNAME. Consider www.snert.com CNAME mx.snert.net.
 	 * Once you have the SOA domain, then lookup the NS of SOA
 	 * domain and then check those against an NS BL.
	 */
	if ((soa_list = pdqGet(pdq, PDQ_CLASS_IN, PDQ_TYPE_SOA, name, NULL)) != NULL) {
		for (rr = soa_list; rr != NULL; rr = rr->next) {
			if (rr->rcode == PDQ_RCODE_OK && rr->type == PDQ_TYPE_SOA) {
				if ((ns_list = pdqGet(pdq, PDQ_CLASS_IN, PDQ_TYPE_NS, rr->name.string.value, NULL)) != NULL) {
					for (rr = ns_list; rr != NULL; rr = rr->next) {
						if (rr->rcode == PDQ_RCODE_OK && rr->type == PDQ_TYPE_NS) {
							if ((list_name = dnsListQuery(dns_list, pdq, names_seen, 1, ((PDQ_PTR *) rr)->host.string.value)) != NULL)
								break;
						}
					}

					pdqFree(ns_list);
				}
				break;
			}
		}

		pdqFree(soa_list);
	}

	return list_name;

#else
/* Version 1 did an initial SOA lookup wnrl37.cheesereason.com
 * assuming the DNS server would return an SOA for the queried
 * host or parent domain. This does not always appear to be the
 * case. Example:
 *
 * 	dig soa wnrl37.cheesereason.com		SERVFAIL
 * 	dig soa cheesereason.com		OK (SOA result)
 * 	dig ns wnrl37.cheesereason.com    	OK (SOA result)
 * 	dig ns cheesereason.com    		OK (NS list)
 *
 * However if you do a NS lookup and get an SOA result, then
 * recurse once using the SOA domain. This still works fine for
 * CNAME records, like www.snert.com.
 */
	return dnsListQueryNs0(dns_list, pdq, names_seen, 1, name);
#endif
}

static void
digestToString(unsigned char digest[16], char digest_string[33])
{
	int i;
	static const char hex_digit[] = "0123456789abcdef";

	for (i = 0; i < 16; i++) {
		digest_string[i << 1] = hex_digit[(digest[i] >> 4) & 0x0F];
		digest_string[(i << 1) + 1] = hex_digit[digest[i] & 0x0F];
	}
	digest_string[32] = '\0';
}

static const char *mail_ignore_table[] = {
 	"abuse@*",
 	"contact@*",
 	"helpdesk@*",
 	"info@*",
 	"kontakt@*",
 	"sales@*",
 	"support@*",
	"*master@*",
	"bounce*@*",
	"request*@*",
	NULL
};

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
 * @param mail
 *	A mail address is hashed then passed to dnsListQueryName.
 *
 * @return
 *	A C string pointer to a list name in which name is a member.
 *	Otherwise NULL if name was not found in a DNS list.
 */
const char *
dnsListQueryMail(DnsList *dns_list, PDQ *pdq, Vector limited_domains, Vector mails_seen, const char *mail)
{
	md5_state_t md5;
	char digest_string[33];
	unsigned char digest[16];
	const char *list_name = NULL, **table, *domain;

	if (dns_list == NULL || mail == NULL || *mail == '\0')
		return NULL;

	for (table = mail_ignore_table; *table != NULL; table++) {
		if (0 <= TextFind(mail, *table, -1, 1))
			return NULL;
	}

	if (limited_domains != NULL) {
		if ((domain = strchr(mail, '@')) == NULL)
			return NULL;
		domain++;

		for (table = (const char **) VectorBase(limited_domains); *table != NULL; table++) {
			if (0 <= TextFind(domain, *table, -1, 1))
				break;
		}

		if (*table == NULL)
			return NULL;
	}

	md5_init(&md5);
	md5_append(&md5, (md5_byte_t *) mail, strlen(mail));
	md5_finish(&md5, (md5_byte_t *) digest);
	digestToString(digest, digest_string);

	list_name = dnsListQueryName(dns_list, pdq, mails_seen, digest_string);
	if (list_name != NULL && 0 < debug)
		syslog(LOG_DEBUG, "<%s> listed in %s", mail, list_name);

	return list_name;
}

/***********************************************************************
 *** END
 ***********************************************************************/
