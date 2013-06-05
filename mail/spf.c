/*
 * spf.c
 *
 * RFC 4408
 *
 * Copyright 2005, 2007 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(HAVE_SYSLOG_H) && ! defined(__MINGW32__)
# include <syslog.h>
#endif

#include <com/snert/lib/net/pdq.h>
#include <com/snert/lib/io/Log.h>
#include <com/snert/lib/net/network.h>
#include <com/snert/lib/mail/spf.h>
#include <com/snert/lib/mail/limits.h>
#include <com/snert/lib/mail/parsePath.h>
#include <com/snert/lib/util/Text.h>
#include <com/snert/lib/util/bs.h>

/***********************************************************************
 ***
 ***********************************************************************/

static const char usage_spf_temp_error_dns[] =
  "RFC 4408 specifies that DNS lookup failures should return a TempError\n"
"# result. However, there are many broken SPF records that rely on other\n"
"# domains that may no longer exist or have connectivity  problems. Disabling\n"
"# this option allows such failures to be ignored and the remainder of the\n"
"# SPF record to be processed in hopes of finding a result.\n"
"#"
;

Option spfTempErrorDns = { "spf-temp-error-dns", "+", usage_spf_temp_error_dns };

#ifndef STRLEN
#define STRLEN(s)			(sizeof (s)-1)
#endif

#define MAX_PTR_MACRO			10
#define MAX_DNS_MECHANISMS		10

static int debug;

static const char unknown[] = "unknown";

const char spfErrorOk[] = "OK";
const char spfErrorIpLiteral[] = "IP address literal";
const char spfErrorSyntax[] = "invalid syntax";
const char spfErrorInternal[] = "internal error";
const char spfErrorNullArgument[] = "null argument";
const char spfErrorIpParse[] = "IPv4 or IPv6 parse error";
const char spfErrorMemory[] = "out of memory";
const char spfErrorCircular[] = "circular reference";
const char spfErrorDnsLimit[] = "too many DNS lookups";
const char spfErrorTooMany[] = "more than one SPF record";

const char *spfResultString[] = {
	"Pass", "Fail", "None", "Neutral", "SoftFail", "TempError", "PermError"
};

static const char *
right_hand_parts(const char *s, const char *delims, int n)
{
	size_t offset;

	for (offset = strlen(s); 0 < offset && 0 < n; n--) {
		offset = strlrspn(s, offset, delims);
		offset = strlrcspn(s, offset, delims);
	}

	return s + offset;
}

typedef struct {
	int result;
	int ptr_count;
	int temp_error;
	int mechanism_count;
	PDQ *pdq;
	char *ip;
	char *helo;
	ParsePath *mail;
	Vector circular;
	unsigned char ipv6[IPV6_BYTE_SIZE];
} spfContext;

void
spfSetDebug(int level)
{
	debug = level;
}

static int
spfMatchIp(unsigned char ipv6[IPV6_BYTE_SIZE], PDQ_rr *list, unsigned long cidr)
{
	PDQ_rr *rr;

	for (rr = list; rr != NULL; rr = rr->next) {
		if (rr->section == PDQ_SECTION_QUERY)
			continue;

		if ((rr->type == PDQ_TYPE_A || rr->type == PDQ_TYPE_AAAA)
		&& networkContainsIp(((PDQ_AAAA *) rr)->address.ip.value, cidr, ipv6))
			return 1;
	}

	return 0;
}

/*
 * @return
 *	A pointer to a macro expansion. Must be freed by caller.
 */
static char *
spfMacro(spfContext *ctx, const char *domain, const char *fmt)
{
	long rhp;
	int is_ipv6;
	const char *format;
	int offset, length, reverse, span;
	char *value, buffer[DOMAIN_SIZE], macro[DOMAIN_SIZE], workspace[DOMAIN_SIZE], delims[8], dots[8];

	fmt += *fmt == ':';
	if (*fmt == '\0' || *fmt == '/')
		fmt = "%{d}";

	format = fmt;
	is_ipv6 = strchr(ctx->ip, ':') != NULL;
	for (offset = 0; *fmt != '\0' && *fmt != '/' && offset < sizeof (buffer); fmt++) {
		if (*fmt != '%') {
			/* Copy a literal character into the buffer. */
			buffer[offset++] = *fmt;
			continue;
		}

		/* Percent format character. */
		switch (*++fmt) {
		case '%':
			/* A percent literal. */
			buffer[offset++] = *fmt;
			continue;
		case '_':
			buffer[offset++] = ' ';
			continue;
		case '-':
			offset += TextCopy(buffer+offset, sizeof (buffer)-offset, "%20");
			continue;
		case '{':
			break;
		default:
			syslog(LOG_ERR, "SPF macro syntax error in \"%s\"", format);
			return NULL;
		}

		/* Macro */
		switch (*++fmt) {
		case 'd':
			value = (char *) domain;
			break;
		case 'h':
			value = ctx->helo;
			break;
#ifdef EXP_MACRO
/* These macros only required to service exp= TXT records. */
		case 'r':
			value = "unknown";
			break;
		case 't':
			(void) snprintf(macro, sizeof (macro), "%lu", (unsigned long) time(NULL));
			goto macro_filled;
		case 'c':
#endif
		case 'i':
			(void) formatIP(
				ctx->ipv6+(is_ipv6 ? 0 : IPV6_OFFSET_IPV4),
				is_ipv6 ? IPV6_BYTE_SIZE : IPV4_BYTE_SIZE,
				*fmt == 'c', macro, sizeof (macro)
			);
			goto macro_filled;
		case 'l':
			value = ctx->mail->localLeft.string;
			break;
		case 'o':
			value = ctx->mail->domain.string;
			break;
		case 's':
			value = ctx->mail->address.string;
			break;
		case 'v':
			value = is_ipv6 ? "ip6" : "in-addr";
			break;
		case 'p':
{
			int match;
			PDQ_rr *list, *rr, *alist;

			value = (char *) unknown;

			if (MAX_PTR_MACRO <= ctx->ptr_count++) {
				syslog(LOG_ERR, "too many SPF %%{p} macro lookups");
				return NULL;
			}

			if ((list = pdqFetch(PDQ_CLASS_IN, PDQ_TYPE_PTR, ctx->ip, NULL)) == NULL)
				break;

			for (match = 0, rr = list; !match && rr != NULL; rr = rr->next) {
				if (rr->section == PDQ_SECTION_QUERY)
					continue;
				value = ((PDQ_PTR *) rr)->host.string.value;
				alist = pdqFetch5A(PDQ_CLASS_IN, ((PDQ_PTR *) rr)->host.string.value);
				match = spfMatchIp(ctx->ipv6, alist, IPV6_BIT_LENGTH);
				pdqListFree(alist);
			}

			length = TextCopy(macro, sizeof (macro), value);
			if (0 < length && macro[length-1] == '.')
				macro[length-1] = '\0';

			pdqListFree(list);
}
			goto macro_filled;
		default:
			syslog(LOG_ERR, "SPF macro syntax error in \"%s\"", format);
			return NULL;
		}

		/* Take a working copy of the macro value. */
		(void) TextCopy(macro, sizeof (macro), value);
macro_filled:
		value = macro;

		/* Get number of right-hand-parts. */
		if ((rhp = strtol(fmt+1, (char **) &fmt, 10)) < 0) {
			syslog(LOG_ERR, "SPF macro syntax error in \"%s\"", format);
			return NULL;
		}

		/* Set reverse flag. */
		reverse = 0;
		if (*fmt == 'r') {
			reverse = 1;
			fmt++;
		}

		/* Get a copy of the delimeters to split on and/or replace. */
		if (sscanf(fmt, "%[.-+,/_=]%n", delims, &span) == 1) {
			delims[span] = '\0';
			fmt += span;
		} else {
			span = 1;
			delims[0] = '.';
			delims[1] = '\0';
		}

		/* Create replacement set. */
		memset(dots, '.', span);
		dots[span] = '\0';

		/* We had better be at the end of the macro. */
		if (*fmt != '}') {
			syslog(LOG_ERR, "SPF macro syntax error in \"%s\"", format);
			return NULL;
		}

		if (reverse) {
			(void) reverseSegments(value, delims, workspace, sizeof (workspace), 0);
			value = workspace;
		}

		if (0 < rhp)
			value = (char *) right_hand_parts(value, delims, rhp);

		TextTransliterate(value, delims, dots, -1);

		offset += TextCopy(buffer+offset, sizeof (buffer)-offset, value);
	}
	buffer[offset - (sizeof (buffer) <= offset)] = '\0';

	return strdup(buffer);
}

/*
 * @param s
 *	A string terminated by a CIDR.
 *
 * @param cidr_4
 *	A pointer to an unsigned long to pass back an IPv4 CIDR.
 *
 * @param cidr_6
 *	A pointer to an unsigned long to pass back an IPv6 CIDR.
 *
 * @return
 *	The length of the number of characters parsed (strlen);
 *	otherwise -1 on a parse error.
 */
static int
spfGetDualCIDR(const char *s, unsigned long *cidr_4, unsigned long *cidr_6)
{
	const char *stop;
	unsigned long cidr4 = IPV6_BIT_LENGTH;
	unsigned long cidr6 = IPV6_BIT_LENGTH;

	stop = s + strcspn(s, "/");

	if (stop[0] == '/' && stop[1] != '/') {
		cidr4 = (unsigned long) strtol(stop + 1, (char **) &stop, 10);

		/* The IPv4 addresses are mapped to IPv6 addresses,
		 * so we have to convert the CIDR from IPv4 to IPv6.
		 */
		cidr4 = IPV6_BIT_LENGTH - IPV4_BIT_LENGTH + cidr4;
	}

	if (cidr_6 != NULL && stop[0] == '/' && stop[1] == '/')
		cidr6 = (unsigned long) strtol(stop + 2, (char **) &stop, 10);

	if (*stop != '\0')
		return -1;

	if (IPV6_BIT_LENGTH < cidr4 || IPV6_BIT_LENGTH < cidr6)
		return -1;

	if (cidr_4 != NULL)
		*cidr_4 = cidr4;

	if (cidr_6 != NULL)
		*cidr_6 = cidr6;

	return stop - s;
}

static int
spfIsCircularReference(spfContext *ctx, const char *domain)
{
	long i;
	char *name;

	/* Check for circular references. If none are found
	 * add the name to the list.
	 */
	for (i = 0; i < VectorLength(ctx->circular); i++) {
		if ((name = VectorGet(ctx->circular, i)) == NULL)
			continue;

		if (TextInsensitiveCompare(name, domain) == 0) {
			syslog(LOG_WARN, "circular SPF references for %s", domain);
			return 1;
		}
	}

	if (VectorAdd(ctx->circular, strdup(domain)))
		return -1;

	return 0;
}

static const char *
spfCheck(spfContext *ctx, const char *domain, const char *alt_txt)
{
	PDQ_rr *list, *rr;
	char *txt;
	Vector terms;
	int qualifier;
	const char *err;
	long i, length;
	unsigned long cidr, cidr6;
	char *term, *redirect, *target, *explain;
	unsigned char net[IPV6_BYTE_SIZE];

	txt = NULL;
	err = NULL;
	term = NULL;
	qualifier = SPF_NONE;

	if (ctx == NULL || domain == NULL) {
		err = spfErrorNullArgument;
		goto error0;
	}
	if (debug) {
		syslog(
			LOG_DEBUG, "enter %s(%lx, %s, \"%s\") ip=%s helo=%s mail=%s",
			__func__, (unsigned long) ctx, domain, TextNull(alt_txt),
			ctx->ip, ctx->helo, ctx->mail->address.string
		);
	}

	if (*ctx->ip == '\0' || *domain == '\0')
		goto error1;

	if (*domain == '[') {
		err = spfErrorIpLiteral;
		goto error1;
	}

	switch (spfIsCircularReference(ctx, domain)) {
	case -1:
		err = spfErrorMemory;
		goto error1;
	case 1:
		/* Circular references with include: or redirect=, see
		 * budgetdialup.com and expedia.com. Ignore already visited
		 * entries.
		 *
		 * The SPF Internet draft in section 10.1 places a limit
		 * on the total number of DNS lookups at 10 after which
		 * PermError should be returned. By returning Neutral for
		 * already included/redirected domains, we can allow the
		 * remainder of the mechanisms to be processed.
		 *
		 * Consider the following case:
		 *
		 * example.org says my users might be roaming on foobar.net
		 * foobar.net says their users might be roaming on example.org
		 * Where both are legit.
		 */
		qualifier = SPF_NEUTRAL;
		goto error1;
	}

	if (alt_txt == NULL) {
		/* RFC 4408 states that SPF RR trump SPF TXT RR, however
		 * the SPF in TXT is still more commonly used.
		 */
		if ((list = pdqGet(ctx->pdq, PDQ_CLASS_IN, PDQ_TYPE_TXT, domain, NULL)) == NULL) {
			if ((list = pdqGet(ctx->pdq, PDQ_CLASS_IN, PDQ_TYPE_SPF, domain, NULL)) == NULL) {
				if (errno != 0)
					qualifier = SPF_TEMP_ERROR;
				goto error1;
			}
		}

		if (list->section == PDQ_SECTION_QUERY) {
			err = pdqRcodeName(((PDQ_QUERY *)list)->rcode);

			switch (((PDQ_QUERY *)list)->rcode) {
			case PDQ_RCODE_OK:
				/* With a TXT record, up the default to neutral (?all). */
				qualifier = SPF_NEUTRAL;
				err = NULL;
				break;

			/* This result code is not to spec. but treats
			 * broken DNS servers as Neutral (not None),
			 * instead of TempError. This allows include: of
			 * non-exsistant/broken domains to be ignored.
			 */
			case PDQ_RCODE_SERVER:
			/* RFC 4408 section 5 paragraph 8 NXDOMAIN (3)
			 * treats as OK (0) with zero answers.
			 */
			case PDQ_RCODE_NXDOMAIN:
				qualifier = SPF_NEUTRAL;
				goto error3;

			case PDQ_RCODE_ERRNO:
			case PDQ_RCODE_NOT_IMPLEMENTED:
				goto error3;

			default:
				qualifier = SPF_TEMP_ERROR;
				goto error3;
			}
		} else {
			err = NULL;
		}

		for (rr = list; rr != NULL; rr = rr->next) {
			if (rr->section == PDQ_SECTION_QUERY || rr->type != PDQ_TYPE_TXT)
				continue;

			if (((PDQ_TXT *) rr)->text.value != NULL
			&& strncmp((char *) ((PDQ_TXT *) rr)->text.value, "v=spf1 ", sizeof ("v=spf1 ")-1) != 0)
				continue;

			if (txt != NULL) {
				qualifier = SPF_PERM_ERROR;
				err = spfErrorTooMany;
				goto error4;
			}

			if ((txt = malloc(((PDQ_TXT *) rr)->text.length+1)) == NULL) {
				qualifier = SPF_TEMP_ERROR;
				err = spfErrorInternal;
				goto error3;
			}

			TextCopy(txt, ((PDQ_TXT *) rr)->text.length+1, (char *) ((PDQ_TXT *) rr)->text.value);
		}

		/* Zero entries or no matching ones found? */
		if (txt == NULL)
			goto error3;
	} else {
		/* With a TXT record given, up the default to neutral (?all). */
		qualifier = SPF_NEUTRAL;
		txt = (char *) alt_txt;
		list = NULL;
	}

	if ((terms = TextSplit(txt, " ", 0)) == NULL) {
		err = spfErrorMemory;
		goto error4;
	}

	redirect = target = NULL;

	/* Start after the version specifier. */
	for (i = 1; i < VectorLength(terms); i++, qualifier = SPF_NEUTRAL) {
		pdqListFree(list);
		list = NULL;
		free(target);
		target = NULL;

		if ((term = VectorGet(terms, i)) == NULL)
			continue;

		if (debug)
			syslog(LOG_DEBUG, "term=%s", term);

		switch (*term++) {
		case '+': qualifier = SPF_PASS;		break;
		case '-': qualifier = SPF_FAIL; 	break;
		case '~': qualifier = SPF_SOFTFAIL; 	break;
		case '?': qualifier = SPF_NEUTRAL;	break;
		default: term--; qualifier = SPF_PASS;	break;
		}

		if (TextInsensitiveCompareN(term , "all", STRLEN("all")) == 0) {
#ifdef DOWNGRADE_PASS_ALL
			/* Disallow +all; spammers are setting themselves
			 * overly permissive SPF records to allow botnets
			 * through. For example pianobandage.com
			 *
			 * "v=spf1 ip4:66.248.130.212 a mx ptr mx:pianobandage.com +all"
			 */
			if (qualifier == SPF_PASS) {
				syslog(LOG_DEBUG, "+all downgraded to ?all");
				qualifier = SPF_NEUTRAL;
			}
#endif
			if (ctx->temp_error)
				qualifier = SPF_TEMP_ERROR;
			redirect = NULL;
			break;
		}

		else if (TextInsensitiveCompareN(term , "a", STRLEN("a")) == 0) {
			if ((target = spfMacro(ctx, domain, term+STRLEN("a"))) == NULL) {
				qualifier = SPF_PERM_ERROR;
				err = spfErrorSyntax;
				goto error5;
			}

			if (MAX_DNS_MECHANISMS <= ctx->mechanism_count++) {
				qualifier = SPF_PERM_ERROR;
				err = spfErrorDnsLimit;
				goto error5;
			}

			if (spfGetDualCIDR(term, &cidr, &cidr6) < 0) {
				qualifier = SPF_PERM_ERROR;
				err = spfErrorSyntax;
				goto error5;
			}

			if ((list = pdqGet5A(ctx->pdq, PDQ_CLASS_IN, target)) == NULL) {
				qualifier = SPF_TEMP_ERROR;
				err = spfErrorInternal;
				goto error5;
			}

			if (list->section == PDQ_SECTION_QUERY) {
				if (((PDQ_QUERY *)list)->rcode != PDQ_RCODE_OK) {
					if (((PDQ_QUERY *)list)->rcode == PDQ_RCODE_UNDEFINED)
						continue;

					if (!spfTempErrorDns.value) {
						ctx->temp_error = 1;
						continue;
					}

					err = pdqRcodeName(((PDQ_QUERY *)list)->rcode);
					qualifier = SPF_TEMP_ERROR;
					goto error5;
				}
			}

			if (spfMatchIp(ctx->ipv6, list, cidr))
				goto done;
			if (spfMatchIp(ctx->ipv6, list, cidr6))
				goto done;
		}

		else if (TextInsensitiveCompareN(term , "mx", STRLEN("mx")) == 0) {
			if ((target = spfMacro(ctx, domain, term+STRLEN("mx"))) == NULL) {
				qualifier = SPF_PERM_ERROR;
				err = spfErrorSyntax;
				goto error5;
			}

			if (MAX_DNS_MECHANISMS <= ctx->mechanism_count++) {
				qualifier = SPF_PERM_ERROR;
				err = spfErrorDnsLimit;
				goto error5;
			}

			if (spfGetDualCIDR(term, &cidr, &cidr6) < 0) {
				qualifier = SPF_PERM_ERROR;
				err = spfErrorSyntax;
				goto error5;
			}

			if ((list = pdqGetMX(ctx->pdq, PDQ_CLASS_IN, target, 0)) == NULL) {
				if (errno == 0)
					continue;
				qualifier = SPF_TEMP_ERROR;
				err = spfErrorInternal;
				goto error5;
			}

			if (list->section == PDQ_SECTION_QUERY) {
				if (((PDQ_QUERY *)list)->rcode != PDQ_RCODE_OK) {
					if (((PDQ_QUERY *)list)->rcode == PDQ_RCODE_UNDEFINED)
						continue;

					if (!spfTempErrorDns.value) {
						ctx->temp_error = 1;
						continue;
					}

					err = pdqRcodeName(((PDQ_QUERY *)list)->rcode);
					qualifier = SPF_TEMP_ERROR;
					goto error5;
				}
			}

			if (spfMatchIp(ctx->ipv6, list, cidr))
				goto done;
			if (spfMatchIp(ctx->ipv6, list, cidr6))
				goto done;
		}

		else if (TextInsensitiveCompareN(term , "ptr", STRLEN("ptr")) == 0) {
			if ((target = spfMacro(ctx, domain, term+STRLEN("ptr"))) == NULL) {
				qualifier = SPF_PERM_ERROR;
				err = spfErrorSyntax;
				goto error5;
			}

			if (MAX_DNS_MECHANISMS <= ctx->mechanism_count++) {
				qualifier = SPF_PERM_ERROR;
				err = spfErrorDnsLimit;
				goto error5;
			}

			if ((list = pdqGet(ctx->pdq, PDQ_CLASS_IN, PDQ_TYPE_PTR, ctx->ip, NULL)) == NULL) {
				qualifier = SPF_TEMP_ERROR;
				err = spfErrorInternal;
				goto error5;
			}

			if (list->section == PDQ_SECTION_QUERY) {
				if (((PDQ_QUERY *)list)->rcode != PDQ_RCODE_OK) {
					if (((PDQ_QUERY *)list)->rcode == PDQ_RCODE_UNDEFINED)
						continue;

					if (!spfTempErrorDns.value) {
						ctx->temp_error = 1;
						continue;
					}

					err = pdqRcodeName(((PDQ_QUERY *)list)->rcode);
					qualifier = SPF_TEMP_ERROR;
					goto error5;
				}
			}

			cidr = cidr6;

			for (rr = list; rr != NULL; rr = rr->next) {
				int match;
				PDQ_rr *alist, *ar;

				if (rr->section == PDQ_SECTION_QUERY || rr->type != PDQ_TYPE_PTR)
					continue;

				if ((alist = pdqGet5A(ctx->pdq, PDQ_CLASS_IN, ((PDQ_PTR *) rr)->host.string.value)) == NULL)
					break;

				/* Remove trailing root dot. */
				if (((PDQ_PTR *) rr)->host.string.value[((PDQ_PTR *) rr)->host.string.length-1] == '.')
					((PDQ_PTR *) rr)->host.string.value[((PDQ_PTR *) rr)->host.string.length-1] = '\0';

				match = 0;
				for (ar = alist; ar != NULL; ar = ar->next) {
					if (ar->section == PDQ_SECTION_QUERY)
						continue;

					if (ar->type != PDQ_TYPE_A && ar->type != PDQ_TYPE_AAAA)
						continue;

					if (networkContainsIp(((PDQ_AAAA *) ar)->address.ip.value, IPV6_BIT_LENGTH, ctx->ipv6)) {
						if (1 < debug)
							syslog(LOG_DEBUG, "ptr=%s target=%s", ar->name.string.value, target);
						if (0 <= TextInsensitiveEndsWith(((PDQ_PTR *) rr)->host.string.value, target)) {
							match = 1;
							break;
						}
					}
				}

				pdqListFree(alist);
				if (match)
					goto done;
			}
		}

		else if (TextInsensitiveCompareN(term , "ip4:", 4) == 0 || TextInsensitiveCompareN(term , "ip6:", 4) == 0) {
			if ((length = parseIPv6(term+4, net)) <= 0) {
				qualifier = SPF_PERM_ERROR;
				err = spfErrorIpParse;
				goto error5;
			}

			if (term[4+length] == '\0') {
				cidr = IPV6_BIT_LENGTH;
			}

			else if (term[4+length] != '/') {
				qualifier = SPF_PERM_ERROR;
				err = spfErrorSyntax;
				goto error5;
			}

			else {
				cidr = (unsigned long) strtol(term+4+length+1, NULL, 10);
				if (term[2] == '4')
					cidr = IPV6_BIT_LENGTH - 32 + cidr;
			}

			if (IPV6_BIT_LENGTH < cidr) {
				qualifier = SPF_PERM_ERROR;
				err = spfErrorSyntax;
				goto error5;
			}

			if (networkContainsIp(net, cidr, ctx->ipv6))
				goto done;
		}

		else if (TextInsensitiveCompareN(term , "include:", STRLEN("include:")) == 0) {
			if ((target = spfMacro(ctx, domain, term+STRLEN("include:"))) == NULL) {
				qualifier = SPF_PERM_ERROR;
				err = spfErrorSyntax;
				goto error5;
			}

			err = spfCheck(ctx, target, NULL);
			switch (ctx->result) {
			case SPF_NONE:
				/* According to the SPF-Classic Internet Draft
				 * a None result from include: throws PermError.
				 */
				qualifier = SPF_PERM_ERROR;
				/*@fallthrough@*/
			case SPF_PASS:
				/* A Pass result from the evaluated "include"
				 * must return the qualifier specified for
				 * the include, NOT the result of the include
				 * evaluation.
				 */
				goto done;
			case SPF_TEMP_ERROR:
			case SPF_PERM_ERROR:
				qualifier = ctx->result;
				goto done;
			}
		}

		else if (TextInsensitiveCompareN(term , "exists:", STRLEN("exists:")) == 0) {
			if ((target = spfMacro(ctx, domain, term+STRLEN("exists:"))) == NULL) {
				qualifier = SPF_PERM_ERROR;
				err = spfErrorSyntax;
				goto error5;
			}

			if (MAX_DNS_MECHANISMS <= ctx->mechanism_count++) {
				qualifier = SPF_PERM_ERROR;
				err = spfErrorDnsLimit;
				goto error5;
			}

			if ((list = pdqGet(ctx->pdq, PDQ_CLASS_IN, PDQ_TYPE_A, target, NULL)) != NULL) {
				if (list->section == PDQ_SECTION_QUERY && ((PDQ_QUERY *)list)->rcode != PDQ_RCODE_OK) {
					if (((PDQ_QUERY *)list)->rcode == PDQ_RCODE_UNDEFINED)
						continue;

					if (!spfTempErrorDns.value) {
						ctx->temp_error = 1;
						continue;
					}

					err = pdqRcodeName(((PDQ_QUERY *)list)->rcode);
					qualifier = SPF_TEMP_ERROR;
					goto error5;
				}

				goto done;
			}
		}

		else if (TextInsensitiveCompareN(term , "redirect=", STRLEN("redirect=")) == 0) {
			if (redirect != NULL) {
				qualifier = SPF_PERM_ERROR;
				err = spfErrorSyntax;
				goto error5;
			}
			redirect = term+STRLEN("redirect=");
		}

		else if (TextInsensitiveCompareN(term , "exp=", STRLEN("exp=")) == 0) {
			if (explain != NULL) {
				qualifier = SPF_PERM_ERROR;
				err = spfErrorSyntax;
				goto error5;
			}
			explain = term+STRLEN("exp=");
		}

		/* Unsupported terms, such as exp= and RFC 6652
		 * ra=, rp=, and rr= are simply skipped.
		 */
	}

	if (redirect != NULL) {
		if ((target = spfMacro(ctx, domain, redirect)) == NULL) {
			qualifier = SPF_PERM_ERROR;
			err = spfErrorSyntax;
			goto error5;
		}
		err = spfCheck(ctx, target, NULL);
		qualifier = ctx->result;
	}
done:
error5:
	VectorDestroy(terms);
	free(target);
error4:
	if (alt_txt == NULL)
		free(txt);
error3:
	pdqListFree(list);
error1:
	ctx->result = qualifier;
	if (debug) {
		syslog(
			LOG_DEBUG, "exit %s(%lx, %s,\"%s\") txt=\"%s\" match=%s result=%s error=%s",
			__func__, (unsigned long) ctx, domain,
			TextEmpty(alt_txt), TextEmpty(txt), TextEmpty(term),
			spfResultString[qualifier], TextEmpty(err)
		);
	}
error0:
	return err;
}

const char *
spfCheckHeloMailTxt(const char *client_addr, const char *helo, const char *mail, const char *txt, int *result)
{
	spfContext ctx;
	const char *error;
	char postmaster[SMTP_PATH_LENGTH];

	if (client_addr == NULL || mail == NULL || result == NULL) {
		error = spfErrorNullArgument;
		goto error0;
	}

	*result = SPF_PERM_ERROR;
	ctx.result = SPF_PERM_ERROR;
	ctx.ip = (char *) client_addr;
	ctx.helo = (char *) (helo == NULL ? unknown : helo);
	ctx.ptr_count = ctx.mechanism_count = 0;
	ctx.temp_error = 0;

	if (parseIPv6(client_addr, ctx.ipv6) == 0) {
		error = spfErrorIpParse;
		goto error0;
	}
	if (parsePath(mail, STRICT_LITERAL_PLUS, 0, &ctx.mail) != NULL) {
		error = spfErrorSyntax;
		goto error0;
	}

	/* If `mail' is the null address, then use "postmaster" combined
	 * with the `helo'. See SPF-Classic Internet draft section 2.2.
	 */
	if (ctx.mail->address.length == 0) {
		free(ctx.mail);
		snprintf(postmaster, sizeof (postmaster), "postmaster@%s", ctx.helo);
		if (parsePath(postmaster, 0, 0, &ctx.mail) != NULL) {
			error = spfErrorSyntax;
			goto error0;
		}
	}

	/* If `mail' has no localpart, then use "postmaster" combined with
	 * the `mail' domain. See SPF-Classic Internet draft section 4.3.
	 */
	else if (ctx.mail->domain.length == 0) {
		free(ctx.mail);
		snprintf(postmaster, sizeof (postmaster), "postmaster@%s", mail);
		if (parsePath(postmaster, 0, 0, &ctx.mail) != NULL) {
			error = spfErrorSyntax;
			goto error0;
		}
	}

	if ((ctx.pdq = pdqOpen()) == NULL) {
		error = spfErrorInternal;
		goto error1;
	}

	if ((ctx.circular = VectorCreate(5)) == NULL) {
		error = spfErrorMemory;
		goto error2;
	}
	VectorSetDestroyEntry(ctx.circular, free);

	error = spfCheck(&ctx, ctx.mail->domain.string, txt);
	*result = ctx.result;

	VectorDestroy(ctx.circular);
error2:
	pdqClose(ctx.pdq);
error1:
	free(ctx.mail);
error0:
	return error;
}

const char *
spfCheckHeloMail(const char *client_addr, const char *helo, const char *mail, int *result)
{
	return spfCheckHeloMailTxt(client_addr, helo, mail, NULL, result);
}

const char *
spfCheckMail(const char *client_addr, const char *mail, int *result)
{
	return spfCheckHeloMail(client_addr, NULL, mail, result);
}

const char *
spfCheckDomain(const char *client_addr, const char *domain, int *result)
{
	return spfCheckHeloMail(client_addr, NULL, domain, result);
}

#ifdef TEST
#include <com/snert/lib/util/getopt.h>
#include <com/snert/lib/sys/sysexits.h>

static char usage[] =
"usage: spf [-v][-h helo]"
"[-t txt] client-ip domain|mail ...\n"
"\n"
"-h helo\t\tthe SMTP EHLO/HELO argument to verify\n"
"-t txt\t\tspecify the initial TXT record to use\n"
"-v\t\tsend debugging information to the mail log.\n"
"\n"
"client-ip\tthe SMTP client connection IP\n"
"domain\t\tone or more domains to verify\n"
"mail\t\tone or more mail addresses to verify\n"
"\n"
"Exit Codes\n"
QUOTE(SPF_PASS) "\t\tSPF Pass\n"
QUOTE(SPF_FAIL) "\t\tSPF Fail\n"
QUOTE(SPF_NONE) "\t\tSPF None\n"
QUOTE(SPF_NEUTRAL) "\t\tSPF Neutral\n"
QUOTE(SPF_SOFTFAIL) "\t\tSPF SoftFail\n"
QUOTE(SPF_TEMP_ERROR) "\t\tSPF Temporary Error\n"
QUOTE(SPF_PERM_ERROR) "\t\tSPF Permanent Error\n"
QUOTE(EX_USAGE) "\t\tusage error\n"
QUOTE(EX_SOFTWARE) "\t\tinternal error\n"
"\n"
LIBSNERT_COPYRIGHT "\n"
;

static const char test_ipv4[] = "192.0.2.3";
static const char test_ipv6[] = "2001:DB8::CB01";
static const char test_helo[] = "pop.snert.net";
static const char test_ptr[] = "mx.example.org";
static const char test_mail[] = "strong-bad@email.example.com";

static int
testMacro(const char *ip, ParsePath *mail, const char *spec, const char *expect)
{
	int rc;
	char *target;
	spfContext ctx;

	ctx.ip = (char *) ip;
	ctx.mail = mail;
	ctx.helo = (char *) test_helo;

	target = spfMacro(&ctx, mail->domain.string, spec);
	rc = TextInsensitiveCompare(expect, target) == 0;
	printf("%s <%s> %s -> %s %s\n", ip, mail->address.string, spec, target, rc ? "PASS" : "FAIL");
	free(target);

	return rc;
}

static int
testMacros(void)
{
	ParsePath *path;

	if (parsePath(test_mail, 0, 0, &path) != NULL)
		return -1;

	testMacro(test_ipv4, path, "%{s}", 	"strong-bad@email.example.com");
	testMacro(test_ipv4, path, "%{o}", 	"email.example.com");
	testMacro(test_ipv4, path, "%{d}", 	"email.example.com");
	testMacro(test_ipv4, path, "%{d4}",	"email.example.com");
	testMacro(test_ipv4, path, "%{d3}",	"email.example.com");
	testMacro(test_ipv4, path, "%{d2}",	"example.com");
	testMacro(test_ipv4, path, "%{d1}",	"com");
	testMacro(test_ipv4, path, "%{dr}",	"com.example.email");
	testMacro(test_ipv4, path, "%{d2r}", 	"example.email");
	testMacro(test_ipv4, path, "%{l}", 	"strong-bad");
	testMacro(test_ipv4, path, "%{l-}", 	"strong.bad");
	testMacro(test_ipv4, path, "%{lr}", 	"strong-bad");
	testMacro(test_ipv4, path, "%{lr-}",	"bad.strong");
	testMacro(test_ipv4, path, "%{l1r-}",	"strong");

	testMacro(test_ipv4, path, "%{ir}.%{v}._spf.%{d2}",	 	"3.2.0.192.in-addr._spf.example.com");
	testMacro(test_ipv4, path, "%{lr-}.lp._spf.%{d2}", 		"bad.strong.lp._spf.example.com");
	testMacro(test_ipv4, path, "%{lr-}.lp.%{ir}.%{v}._spf.%{d2}",	"bad.strong.lp.3.2.0.192.in-addr._spf.example.com");
	testMacro(test_ipv4, path, "%{ir}.%{v}.%{l1r-}.lp._spf.%{d2}", "3.2.0.192.in-addr.strong.lp._spf.example.com");
	testMacro(test_ipv4, path, "%{d2}.trusted-domains.example.net", "example.com.trusted-domains.example.net");
	testMacro(test_ipv6, path, "%{ir}.%{v}._spf.%{d2}", 		"1.0.B.C.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.8.B.D.0.1.0.0.2.ip6._spf.example.com");

	testMacro("192.168.0.1", path, "%{p}",		"unknown");
	testMacro("82.97.10.34", path, "%{p}",		"mx.snert.net");
	testMacro("82.97.10.34", path, "%{pr}",		"net.snert.mx");
	testMacro("82.97.10.34", path, "%{p2r}",	"snert.mx");

	free(path);

	return 0;
}

int
main(int argc, char **argv)
{
	int i, ch, spf;
	const char *error, *helo = NULL, *txt = NULL;

	while ((ch = getopt(argc, argv, "h:n:t:Tv")) != -1) {
		switch (ch) {
		case 'h':
			helo = optarg;
			break;
		case 't':
			txt = optarg;
			break;
		case 'T':
			testMacros();
			return 0;
		case 'v':
			pdqSetDebug(debug);
			spfSetDebug(debug+1);
			setlogmask(LOG_UPTO(LOG_DEBUG));
			break;
		default:
			optind = argc;
			break;
		}
	}

	if (argc < optind + 2) {
		(void) fprintf(stderr, usage);
		return EX_USAGE;
	}

	if (atexit(pdqFini)) {
		fprintf(stderr, "atexit() failed\n");
		exit(EX_SOFTWARE);
	}

	if (pdqInit()) {
		fprintf(stderr, "pdqInit() failed\n");
		exit(EX_SOFTWARE);
	}

	if (debug)
		openlog("spf", LOG_PID|LOG_PERROR, LOG_MAIL);

	for (i = optind+1; i < argc; i++) {
		error = spfCheckHeloMailTxt(argv[optind], helo, argv[i], txt, &spf);
		fprintf(stdout, "<%s> %s %s\n", argv[i], spfResultString[spf], error == NULL ? "" : error);
	}

	return spf;
}
#endif /* TEST */
