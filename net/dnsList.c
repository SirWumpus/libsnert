/**
 * dnsList.c
 *
 * Copyright 2008, 2012 by Anthony Howe. All rights reserved.
 */

#define NS_VERSION3

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
static PDQ_rr *(*wait_fn)(PDQ *) = pdqWait;

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
dnsListSetWaitAll(int flag)
{
	wait_fn = flag ? pdqWaitAll : pdqWait;
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
	char timestamp[TIME_STAMP_MIN_SIZE];

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
	char timestamp[TIME_STAMP_MIN_SIZE];

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

#ifdef STRUCTURED_FIELDS
/*
$option="$dns_list1;$dns_list2;..."

$dns_list is either

	$suffix/$mask    (currently available)

or
	$suffix:$code1/$action,$code2/$action,...

	/$action is optional and assumes /REJECT is default


eg1

uri-bl="multi.surbl.org:127.0.0.2/REJECT,127.0.0.4/CONTENT; black.uribl.com"


eg2

uri-bl="multi.surbl.org:127.0.0.2/REJECT,127.0.0.4/CONTENT;
black.uribl.com/0xffff08"
*/
{
	int span;
	DnsListCode *code;
	DnsListSuffix *suffix;
	char **array, *suffix_end, *slash;


	if ((array = TextSplit(string, ";", 0)) == NULL)
		goto error1;

	for (array = (char **) VectorBase(array); *array != NULL; array++) {
		if ((suffix = calloc(1, sizeof (*suffix))) == NULL)
			goto error1;

		span = strcspn(*array, ":/");

		if ((*array)[span] == '\0') {
			suffix->mask = (unsigned long) ~0L;
		} else if ((*array)[span] == ':') {
			(*array)[span++] = '\0';
			suffix->mask = (unsigned long) strtol(*array + span, NULL, 0);
			if ((suffix->suffix = strdup(*array)) == NULL)
				goto error1;
		} else {
			(*array)[span++] = '\0';
			if ((suffix->codes = TextSplit(*array + span, ",", 0)) == NULL)
				goto error1;

			for (codes = suffix->codes; *codes != NULL; codes++) {
				if ((code = malloc(sizeof (*code))) == NULL)
					goto error1;
				if ((slash = strchr(*codes, '/')) == NULL)
					goto error1;
				*slash++ = '\0';
				if (parseIPv6(*codes, code->code) == 0)
					goto error1;
				if ((code->action = strdup(slash)) == NULL)
					goto error1;
				if (VectorReplace(suffix->codes, suffix))
					goto error1;
			}
		}
	}
}
#else
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
#endif
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
	PDQ_A *rr, *next;
	unsigned long bits;
	const char **suffixes;

	suffixes = (const char **) VectorBase(dns_list->suffixes);
	for (rr = (PDQ_A *) list; rr != NULL; rr = next) {
		next = (PDQ_A *)rr->rr.next;

		/* Ignore failed query sections. */
		if (rr->rr.section == PDQ_SECTION_QUERY
		&& ((PDQ_QUERY *) rr)->rcode != PDQ_RCODE_OK) {
			next = (PDQ_A *)pdqListFindQuery((PDQ_rr *)rr);
			continue;
		}

		if (
		/* Confine ourselves to the answer section. */
		   rr->rr.section != PDQ_SECTION_ANSWER
		/* The DNS BL return one or more numeric results as A records.
		 * There may be informational TXT records present, but are
		 * ignored here.
		 */
		|| rr->rr.type != PDQ_TYPE_A)
			continue;

		if (TextInsensitiveStartsWith(rr->rr.name.string.value, name) < 0)
			continue;

		for (i = 0; suffixes[i] != NULL; i++) {
			if (strstr(rr->rr.name.string.value, suffixes[i]) == NULL)
				continue;

			bits = NET_GET_LONG(rr->address.ip.value + rr->address.ip.offset);

			if ((bits & dns_list->masks[i]) != 0
			&& isReservedIPv4(rr->address.ip.value + rr->address.ip.offset, IS_IP_LOCAL|IS_IP_THIS_NET)) {
				/*** HACK for dbl.spamhaus.org which ignores
				 *** and returns a false positive code for IP
				 *** address lookups.
				 ***
				 *** http://www.spamhaus.org/faq/answers.lasso?section=Spamhaus%20DBL#279
				 ***
				 *** Ignore 127.0.1.255 responses.
				 ***/
				if (bits == 0x7F0001FF)
					break;

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
 *	An arbitrary string to query in one or more DNS lists.
 *
 * @return
 *	A C string pointer to a list name in which name is a member.
 *	Otherwise NULL if name was not found in a DNS list.
 */
const char *
dnsListQueryString(DnsList *dns_list, PDQ *pdq, Vector names_seen, const char *name)
{
	char *copy;
	int offset = 0;
	PDQ_rr *answers;
	const char *list_name = NULL, **seen;

	if (dns_list == NULL || name == NULL || *name == '\0')
		return NULL;

	/* Check cache of previously tested hosts/domains. */
	if (names_seen != NULL) {
		for (seen = (const char **) VectorBase(names_seen); *seen != NULL; seen++) {
			if (TextInsensitiveCompare(name, *seen) == 0) {
				if (0 < debug)
					syslog(LOG_DEBUG, "dnsListQueryString name=\"%s\" previously checked", name);
				return NULL;
			}
		}

		if (VectorAdd(names_seen, copy = strdup(name)))
			free(copy);
	}

	if (0 < debug)
		syslog(LOG_DEBUG, "dnsListQueryString name=\"%s\" offset=%d", name, offset);

	answers = pdqGetDnsList(
		pdq, PDQ_CLASS_IN, PDQ_TYPE_A, name+offset,
		(const char **) VectorBase(dns_list->suffixes), wait_fn
	);

	if (answers != NULL) {
		list_name = dnsListIsNameListed(dns_list, name+offset, answers);
		pdqListFree(answers);
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
const char *
dnsListQueryName(DnsList *dns_list, PDQ *pdq, Vector names_seen, const char *name)
{
	if (name != NULL && 0 < spanIP(name))
		return NULL;

	return dnsListQueryString(dns_list, pdq, names_seen, name);
}

static const char *
dnsListCheckIP(DnsList *dns_list, PDQ *pdq, Vector names_seen, const char *name, PDQ_rr *list)
{
	PDQ_rr *rr;
	const char *list_name = NULL;
	char buffer[DOMAIN_STRING_LENGTH];

	if (dns_list == NULL || name == NULL || *name == '\0')
		return NULL;

	for (rr = list; (rr = pdqListFindName(rr, PDQ_CLASS_IN, PDQ_TYPE_5A, name)) != NULL; rr = rr->next) {
		if (PDQ_RR_IS_NOT_VALID(rr))
			break;

		if (rr->section == PDQ_SECTION_QUERY
		/* Only compare A records related to the query.
		 * Some DNS servers will provide extra A records
		 * related to the authority NS servers listed.
		 */
		|| (rr->type != PDQ_TYPE_A && rr->type != PDQ_TYPE_AAAA))
			continue;

		/* Some domains specify a 127.0.0.0/8 address for
		 * an A recorded, like "anything.so". The whole
		 * TLD .so for Somalia, is a wild card record that
		 * maps to 127.0.0.2, which typically is a DNSBL
		 * test record that always fails.
		 */
		if (isReservedIPv6(((PDQ_AAAA *) rr)->address.ip.value, IS_IP_LOOPBACK|IS_IP_LOCALHOST))
			continue;

		if (0 < debug)
			syslog(LOG_DEBUG, "dnsListCheckIP name=\"%s\" ip=\"%s\"", rr->name.string.value, ((PDQ_AAAA *) rr)->address.string.value);

		(void) reverseIp(((PDQ_AAAA *) rr)->address.string.value, buffer, sizeof (buffer), 0);
		list_name = dnsListQueryString(dns_list, pdq, names_seen, buffer);

		if (list_name != NULL) {
			if (0 < debug)
				syslog(LOG_DEBUG, "%s [%s] listed in %s", name, ((PDQ_AAAA *) rr)->address.string.value, list_name);
			break;
		}
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
 * @param name
 *	An IP, host name, or domain name. In the case of a host or
 *	domain name, their A/AAAA records are first found and the
 *	resulting list of IP addresses checked.
 *
 * @return
 *	A C string pointer to a list name in which name is a member.
 *	Otherwise NULL if name was not found in a DNS list.
 */
const char *
dnsListQueryIP(DnsList *dns_list, PDQ *pdq, Vector names_seen, const char *name)
{
	PDQ_rr *list;
	PDQ_AAAA aaaa;
	const char *list_name = NULL;

	if (dns_list == NULL || name == NULL || *name == '\0')
		return NULL;

	memset(&aaaa, 0, sizeof (aaaa));
	if (0 < (aaaa.address.string.length = parseIPv6(name, aaaa.address.ip.value))) {
		/* Create a psuedo A record. */
		aaaa.rr.name.string.length = TextCopy(aaaa.rr.name.string.value, sizeof (aaaa.rr.name.string.value), name);
		(void) TextCopy(aaaa.address.string.value, sizeof (aaaa.address.string.value), name);

		if (isReservedIPv6(aaaa.address.ip.value, IS_IP_V4)) {
			aaaa.address.ip.offset = IPV6_OFFSET_IPV4;
			aaaa.rr.type = PDQ_TYPE_A;
		} else {
			aaaa.rr.type = PDQ_TYPE_AAAA;
		}

		aaaa.rr.section = PDQ_SECTION_ANSWER;
		aaaa.rr.class = PDQ_CLASS_IN;
		aaaa.rr.next = NULL;
		list = &aaaa.rr;
	} else {
		list = pdqGet5A(pdq, PDQ_CLASS_IN, name);
	}

	list_name = dnsListCheckIP(dns_list, pdq, names_seen, name, list);

	if (list != &aaaa.rr)
		pdqListFree(list);

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
 *	top-level-domain is passed to dnsListQueryString. If NULL is
 *	returned, then repeat with the next preceding label, until a
 *	a list name is return or the entire name has been queried.
 *
 *	Otherwise when false, the domain starting with the label
 *	immediately preceding the top-level-domain is passed to
 *	dnsListQueryString.
 *
 * @param name
 *	A host, domain, or IP to query in one or more DNS lists.
 *
 * @return
 *	A C string pointer to a list name in which name is a member.
 *	Otherwise NULL if name was not found in a DNS list.
 */
const char *
dnsListQueryDomain(DnsList *dns_list, PDQ *pdq, Vector names_seen, int test_sub_domains, const char *name)
{
	int offset;
	const char *list_name = NULL;

	if (dns_list == NULL || name == NULL || *name == '\0')
		return NULL;

	/* Find start of 1st or 2nd level TLD. */
	offset = indexValidTLD(name);

	if (offset < 0) {
		if (spanIP(name) <= 0)
			return NULL;
		return dnsListQueryIP(dns_list, pdq, names_seen, name);
	}

	/* Query domain and sub-domains from right-to-left
	 * starting with first label below the TLD. For lists
	 * like SURBL and URIBL that tend to list the parent
	 * domain, this allows for a possible hit on the
	 * first query.
	 */
	do {
		offset = strlrcspn(name, offset-1, ".");
		if (0 < debug)
			syslog(LOG_DEBUG, "dnsListQueryDomain name=\"%s\" offset=%d", name, offset);

		if ((list_name = dnsListQueryString(dns_list, pdq, names_seen, name+offset)) != NULL) {
			if (0 < debug)
				syslog(LOG_DEBUG, "%s listed in %s", name+offset, list_name);

			return list_name;
		}
	} while (test_sub_domains && 0 < offset);

	return NULL;
}

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
 *	A host or domain name whos NS records are first found and
 *	then passed to dnsListQueryDomain.
 *
 * @return
 *	A C string pointer to a list name in which name is a member.
 *	Otherwise NULL if name was not found in a DNS list.
 */
const char *
dnsListQueryNs(DnsList *ns_bl, DnsList *ns_ip_bl, PDQ *pdq, Vector names_seen, const char *name)
{
/* After some dispute with Alex Broens concerning previous lookup
 * algorithms, have simplified the search. The previous algorithm relied
 * on the DNS server doing the recursive search of the name from host to
 * top level looking for NS or SOA per the request. Some DNS servers
 * appear not to do this for us, which would result in a incorrect
 * failure.
 *
 * This version handles subdomains, CNAME redirection, and SOA records.
 * Essentially this should handle NS BL that list by NS domain (Alex B)
 * or by NS host name (April L).
 */
 	char *copy;
	PDQ_rr *rr, *ns_list;
	int offset, tld_offset;
 	int ns_found = 0, ns_rcode_ok;
	const char *list_name = NULL, **seen;

	if ((ns_bl == NULL && ns_ip_bl == NULL) || name == NULL || *name == '\0')
		return NULL;

	/* Find start of TLD. */
	if ((tld_offset = indexValidTLD(name)) < 0)
		return NULL;

	/* Check cache of previously tested hosts/domains. */
	if (names_seen != NULL) {
		for (seen = (const char **) VectorBase(names_seen); *seen != NULL; seen++) {
			if (TextInsensitiveCompare(name, *seen) == 0) {
				if (0 < debug)
					syslog(LOG_DEBUG, "dnsListQueryNs name=\"%s\" previously seen", name);
				return NULL;
			}
		}
	}

	/* Scan domain and subdomains from left-to-right. */
	for (offset = 0; offset < tld_offset; offset += strcspn(name+offset, ".")+1) {
		if (0 < debug)
			syslog(LOG_DEBUG, "dnsListQueryNs name=\"%s\" offset=%d tld_offset=%d", name, offset, tld_offset);

		if ((ns_list = pdqRootGet(pdq, PDQ_CLASS_IN, PDQ_TYPE_NS, name+offset, NULL)) != NULL) {
			if (ns_list->section == PDQ_SECTION_QUERY)
				ns_rcode_ok = ((PDQ_QUERY *)ns_list)->rcode == PDQ_RCODE_OK;
			else
				ns_rcode_ok = PDQ_RCODE_OK;

			for (rr = ns_list; rr != NULL; rr = rr->next) {
				if (rr->section == PDQ_SECTION_QUERY)
					continue;

				switch (rr->section) {
				case PDQ_SECTION_ANSWER:
					if (rr->type == PDQ_TYPE_CNAME) {
						/* Add to list to detect CNAME loops. */
						if (VectorAdd(names_seen, copy = strdup(name)))
							free(copy);
						list_name = dnsListQueryNs(ns_bl, ns_ip_bl, pdq, names_seen, ((PDQ_CNAME *) rr)->host.string.value);
						goto ns_list_break;
					}
					if (rr->type == PDQ_TYPE_NS) {
						ns_found = 1;
						if ((list_name = dnsListCheckIP(ns_ip_bl, pdq, names_seen, ((PDQ_NS *) rr)->host.string.value, ns_list)) != NULL)
							goto ns_list_break;
						if ((list_name = dnsListQueryDomain(ns_bl, pdq, names_seen, 1, ((PDQ_NS *) rr)->host.string.value)) != NULL)
							goto ns_list_break;
					}
					break;

				case PDQ_SECTION_AUTHORITY:
				/* Three possible SOA handling:
				 *
				 * 1. Use the SOA RR domain to do recursive
				 *    NS lookups. Problem with domains that
				 *    list no NS other than the parent zone's
				 *    glue records. This can result in a
				 *    recursive loop. eg. mxshelter.com
				 *
				 * 2. Do a top-down NS search from the root
				 *    looking for the glue records. Expensive.
				 *
				 * 3. Simply test the primary name server
				 *    given by the SOA mname. Cheap. Assumes
				 *    that the primary NS would be one of the
				 *    set of NS listed.
				 */
					if (!ns_found && rr->type == PDQ_TYPE_SOA) {
						if ((list_name = dnsListCheckIP(ns_ip_bl, pdq, names_seen, ((PDQ_SOA *) rr)->mname.string.value, ns_list)) != NULL)
							goto ns_list_break;
						if ((list_name = dnsListQueryDomain(ns_bl, pdq, names_seen, 1, ((PDQ_SOA *) rr)->mname.string.value)) != NULL)
							goto ns_list_break;
					}
					goto ns_list_break;

				default:
					break;
				}
			}
ns_list_break:
			pdqListFree(ns_list);

			/* We found a non-empty NS list for a (sub)domain.
			 * The dnsListQueryDomain either timed out, failed, or
			 * succeeded. We can stop processing parent
			 * domains.
			 */
			if (ns_rcode_ok)
				break;
		}
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
const char *
dnsListQueryMD5(DnsList *dns_list, PDQ *pdq, Vector already_seen, const char *string)
{
	md5_state_t md5;
	char digest_string[33];
	unsigned char digest[16];
	const char *list_name = NULL;

	md5_init(&md5);
	md5_append(&md5, (md5_byte_t *) string, strlen(string));
	md5_finish(&md5, (md5_byte_t *) digest);
	md5_digest_to_string(digest, digest_string);

	list_name = dnsListQueryString(dns_list, pdq, already_seen, digest_string);
	if (list_name != NULL && 0 < debug)
		syslog(LOG_DEBUG, "\"%s\" listed in %s", string, list_name);

	return list_name;
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
	"no-reply@*",
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
 * @param string
 *	A mail address is hashed then passed to dnsListQueryMD5.
 *
 * @return
 *	A C string pointer to a list name in which name is a member.
 *	Otherwise NULL if name was not found in a DNS list.
 */
const char *
dnsListQueryMail(DnsList *dns_list, PDQ *pdq, Vector limited_domains, Vector mails_seen, const char *mail)
{
	const char **table, *domain;

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

	return dnsListQueryMD5(dns_list, pdq, mails_seen, mail);
}

/***********************************************************************
 *** END
 ***********************************************************************/
