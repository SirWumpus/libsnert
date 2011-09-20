/*
 * uri.c
 *
 * RFC 2821, 2396 Support Routines
 *
 * Copyright 2006, 2010 by Anthony Howe. All rights reserved.
 */

#ifndef HTTP_BUFFER_SIZE
#define HTTP_BUFFER_SIZE		(4 * 1024)
#endif

#ifndef IMPLICIT_DOMAIN_MIN_DOTS
#define IMPLICIT_DOMAIN_MIN_DOTS	1
#endif

#define TEXT_VS_INLINE
#define TEST_MESSAGE_PARTS
#define URI_HTTP_ORIGIN_QUERY_FIRST

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#define _REENTRANT	1

#include <com/snert/lib/version.h>

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#endif

#if defined(HAVE_SYSLOG_H) && ! defined(__MINGW32__)
# include <syslog.h>
#endif

#include <com/snert/lib/io/Log.h>
#include <com/snert/lib/io/file.h>
#include <com/snert/lib/io/socket2.h>
#include <com/snert/lib/net/network.h>
#include <com/snert/lib/net/dnsList.h>
#include <com/snert/lib/mail/tlds.h>
#include <com/snert/lib/mail/limits.h>
#include <com/snert/lib/mail/parsePath.h>
#include <com/snert/lib/util/b64.h>
#include <com/snert/lib/util/uri.h>
#include <com/snert/lib/util/html.h>
#include <com/snert/lib/util/Text.h>

/***********************************************************************
 ***
 ***********************************************************************/

const char uriErrorConnect[] = "socket connect error";
const char uriErrorHttpResponse[] = "HTTP response parse error";
const char uriErrorLoop[] = "HTTP redirection loop";
const char uriErrorMemory[] = "out of memory";
const char uriErrorNoLocation[] = "no Location given";
const char uriErrorNoOrigin[] = "no HTTP origin";
const char uriErrorNotHttp[] = "not an http: URI";
const char uriErrorNullArgument[] = "null argument";
const char uriErrorParse[] = "URI parse error";
const char uriErrorPort[] = "unknown port";
const char uriErrorRead[] = "socket read error";
const char uriErrorWrite[] = "socket write error";
const char uriErrorOverflow[] = "buffer overflow";

int uriDebug;

static char at_sign_delim = '@';
#ifdef OLD
/* RFC 2396 */
static char uri_excluded[] = "#<>%\"{}|\\^[]`";
static char uri_reserved[] = ";/?:@&=+$,";
static char uri_unreserved[] = "-_.!~*'()";
#else
/* RFC 3986
 *
 * Note that vertical bar (|) is not excluded, since it can
 * appear in the file: scheme, eg. "file:///C|/foo/bar/"
 * Assume it is reserved.
 */
static char uri_excluded[] = "\"<>{}\\^`";
static char uri_reserved[] = "%:/?#[]@!$&'()*+,;=|";
static char uri_unreserved[] = "-_.~";
#endif
static long socket_timeout = SOCKET_CONNECT_TIMEOUT;
static const char log_error[] = "%s(%d): %s";

/***********************************************************************
 *** URI related support routines.
 ***********************************************************************/

/**
 * @param octet
 *	An octet value to test.
 *
 * @return
 *	True if octet is valid within a URI string.
 *
 * @see
 *	RFC 3986 sections 2.2, 2.3.
 */
int
isCharURI(int octet)
{
	/* Throw away the ASCII controls, space, and high-bit octets. */
	if (octet <= 0x20 || 0x7F <= octet)
		return 0;

	/* uri_excluded is the inverse set of unreserved and
	 * reserved characters from printable ASCII.
	 */
	if (strchr(uri_excluded, octet) != NULL)
		return 0;

	return 1;
}

#ifdef NOT_YET
/*
 *
 */
int
spanUriEncode(const char *uri)
{
	const char *start;

	for (start = uri; *uri != '\0'; uri += 3) {
		if (*uri != '%')
			break;
		if (qpHexDigit(uri[1]) < 0)
			break;
		if (qpHexDigit(uri[2]) < 0)
			break;
	}

	return uri - start;
}
#endif

/*
 * RFC 2396
 */
int
spanURI(const char *uri)
{
	int a, b;
	const char *start;

	if (uri == NULL)
		return 0;

	for (start = uri; *uri != '\0'; uri++) {
		/* I include %HH within a URI since you typically
		 * want to extract a URI before you can decode
		 * any percent-encoded characters.
		 */
		if (*uri == '%') {
			if ((a = qpHexDigit(uri[1])) < 0 || (b = qpHexDigit(uri[2])) < 0)
				break;
			if (!isCharURI(a * 16 + b))
				break;
			uri += 2;
		} else if (!isCharURI(*uri)) {
			break;
		}
	}

	return uri - start;
}

/**
 * @param scheme
 *	A C string pointer to the start of scheme.
 *
 * @return
 *	The length of the valid scheme; otherwise zero (0).
 *
 * @see
 *	RFC 3986 section 3.1
 */
int
spanScheme(const char *scheme)
{
	const char *start;

	if (scheme == NULL)
		return 0;

	if (!isalnum(*scheme))
		return 0;

	for (start = scheme++; *scheme != '\0'; scheme++) {
		switch (*scheme) {
		case '+': case '-': case '.':
			continue;
		default:
			if (isalnum(*scheme))
				continue;
		}
		break;
	}

	return scheme - start;
}

/*
 * RFC 2396
 */
int
spanQuery(const char *query)
{
	const char *start;

	for (start = query; *query != '\0'; query++) {
		if (isalnum(*query))
			continue;
		if (*query == '%' && isxdigit(query[1]) && isxdigit(query[2])) {
			query += 2;
			continue;
		}
		if (strchr(uri_unreserved, *query) != NULL)
			continue;
		if (strchr(uri_reserved, *query) == NULL)
			break;
	}

	return query - start;
}

/*
 * RFC 2396
 */
int
spanFragment(const char *fragment)
{
	return spanQuery(fragment);
}

/*
 * RFC 2821 domain syntax excluding address-literal.
 *
 * Note that RFC 1035 section 2.3.1 indicates that domain labels
 * should begin with an alpha character and end with an alpha-
 * numeric character. However, all numeric domains do exist, such
 * as 123.com, so are permitted.
 */
static int
alt_spanDomain(const char *domain, int minDots)
{
	const char *start;
	int dots, previous, label_is_alpha;

	if (domain == NULL)
		return 0;

	dots = 0;
	previous = '.';
	label_is_alpha = 1;

	for (start = domain; *domain != '\0'; domain++) {
		switch (*domain) {
		case '.':
#ifdef RFC_1035_DISALLOW_TRAILING_HYPHEN
/* Spam sample:
 * http://www.creditcard.com.---------phpsessionoscommerce23452.st-partners.ru/priv/cc/verification.html
 */
			/* A domain segment must end with an alpha-numeric. */
			if (!isalnum(previous))
				return 0;
#endif
			/* Double dots are illegal. */
			if (domain[1] == '.')
				return 0;

			/* Count only internal dots, not the trailing root dot. */
			if (domain[1] != '\0') {
				label_is_alpha = 1;
				dots++;
			}
			break;
		case '-':
#ifdef RFC_1035_DISALLOW_LEADING_HYPHEN
/* Spam sample:
 * http://www.creditcard.com.---------phpsessionoscommerce23452.st-partners.ru/priv/cc/verification.html
 */
			/* A domain segment cannot start with a hyphen. */
			if (previous == '.')
				return 0;
#endif
			break;
		default:
			if (!isalnum(*domain))
				goto stop;

			label_is_alpha = label_is_alpha && isalpha(*domain);
			break;
		}

		previous = *domain;
	}

	/* Top level domain must end with dot or alpha character. */
	if (0 < dots && !label_is_alpha)
		return 0;
stop:
	if (dots < minDots)
		return 0;

	return domain - start;
}


static int
alt_spanHost(const char *host, int minDots)
{
	int span;

	if (0 < (span = spanIP(host)))
		return span;

	return alt_spanDomain(host, minDots);
}

/***********************************************************************
 ***
 ***********************************************************************/

struct mapping {
	const char *name;
	int length;
	int value;
};

static struct mapping schemeTable[] = {
	{ "ip",		sizeof ("ip")-1, 		0 },
	{ "cid",	sizeof ("cid")-1, 		0 },
	{ "file",	sizeof ("file")-1,		0 },
	{ "about",	sizeof ("about")-1,		0 },
	{ "javascript",	sizeof ("javascript")-1,	0 },
	{ "ftp",	sizeof ("ftp")-1, 		21 },
	{ "gopher",	sizeof ("gopher")-1, 		70 },
	{ "http",	sizeof ("http")-1, 		80 },
	{ "https",	sizeof ("https")-1, 		443 },
	{ "imap",	sizeof ("imap")-1, 		143 },
	{ "ldap",	sizeof ("ldap")-1, 		389 },
	{ "mailto",	sizeof ("mailto")-1, 		25 },
	{ "smtp",	sizeof ("smtp")-1, 		25 },
	{ "email",	sizeof ("email")-1, 		25 },
	{ "mail",	sizeof ("mail")-1, 		25 },
	{ "from",	sizeof ("from")-1, 		25 },
	{ "nntp",	sizeof ("nntp")-1, 		119 },
	{ "pop3",	sizeof ("pop3")-1, 		110 },
	{ "telnet",	sizeof ("telnet")-1, 		23 },
	{ "rtsp",	sizeof ("rtsp")-1, 		554 },
	{ NULL, 0 }
};

/**
 * @param uri
 *	A pointer to a URI structure.
 *
 * @return
 *	A positive port number or -1 if the scheme is unknown.
 */
int
uriGetSchemePort(URI *uri)
{
	struct mapping *t;

	if (uri == NULL)
		return -1;

	if (uri->port != NULL) {
		/* Make sure the port is a postive number. */
		long value = strtol(uri->port, NULL, 10);
		return value <= 0 ? -1 : (int) value;
	}

	if (uri->scheme == NULL) {
		if (uri->host == NULL)
			return -1;
		if (TextMatch(uri->host, "www.*", -1, 1))
			return 80;
		if (TextMatch(uri->host, "pop.*", -1, 1))
			return 110;
		if (TextMatch(uri->host, "imap.*", -1, 1))
			return 143;
		if (TextMatch(uri->host, "smtp.*", -1, 1))
			return 25;

		return -1;
	}

	for (t = schemeTable; t->name != NULL; t++) {
		if (TextInsensitiveCompare(uri->scheme, t->name) == 0)
			return t->value;
	}

	return -1;
}

void
uriSetDebug(int flag)
{
	uriDebug = flag;
}

void
uriSetTimeout(long ms)
{
	socket_timeout = ms;
}

URI *
uriParse(const char *u, int length)
{
	return uriParse2(u, length, IMPLICIT_DOMAIN_MIN_DOTS);
}

/**
 * @param u
 *	A pointer to a C string representing a URI as described by
 *	RFC 2396, 3986.
 *
 * @param length
 *	The maximum length of the URI string to parse or -1 for the
 *	whole string.
 *
 * @param implicit_domain_min_dots
 *	When there is no scheme present, an attempt is made to infer
 *	the presence of a domain name by the number of internal dots
 *	separating the domain labels (the root dot does not count).
 *	This value is typically set to a more conservative value of 2.
 *	Some schools of thought consider 1 more useful.
 *
 * @return
 *	A pointer to a URI structure. Its the caller's responsibility
 *	to free() this pointer.
 *
 * @todo
 *	Parse IP as domain literals, ie. [123.45.67.89]
 */
URI *
uriParse2(const char *u, int length, int implicit_domain_min_dots)
{
	URI *uri;
	int span, port;
	struct mapping *t;
	char *value, *mark, *str;

	if (u == NULL)
		goto error0;

	if (3 < uriDebug)
		syslog(LOG_DEBUG, "uriParse2(\"%.60s...\", %d, %d)", u, length, implicit_domain_min_dots);

	if (length < 0)
		length = (int) strlen(u);

	/* Deal with instances of:
	 *
	 * 	Visit us at http://some.domain.tld/file.html.
	 *
	 *	Contact us at achowe@snert.com.
	 *
	 * Note that dot is not a reserved character in the path,
	 * query, or fragment elements, so dot at the end of the
	 * URI is valid.
	 *
	 * However, common occurrences of URI appear in mail or
	 * documentation, possibly as part of a sentence, where
	 * the average author is not aware the end of sentence
	 * period could be confused as part of the URI itself.
	 * Thus popular wisdom suggests removing one trailing dot.
	 *
	 * In the case of a trailing dot at the end of an mail
	 * address, the trailing dot could signify the domain
	 * root. Removing the trailing dot will not invalidate
	 * the mail address.
	 */
	if (0 < length && u[length-1] == '.')
		length--;
	if (length == 0)
		goto error0;

	/* Allocate space for the structure, two copies of the URI
	 * string, and an extra byte used for a '\0'.
	 */
	if ((uri = malloc(sizeof (*uri) + length + length + length + 4)) == NULL)
		goto error0;
	memset(uri, 0, sizeof (*uri));

	/* First copy remains unmodified. */
	uri->uri = (char *)(uri + 1);
	(void) TextCopy(uri->uri, length+1, (char *) u);

	/* Second copy is URI decoded. */
	uri->uriDecoded = uri->uri + length + 1;
	(void) TextCopy(uri->uriDecoded, length+1, (char *) u);
	uriDecodeSelf(uri->uriDecoded);

	/* Third copy gets parsed into pieces. Skip over the
	 * 2nd and the spare byte. The spare byte is used if
	 * we have to shift the scheme left by one byte to make
	 * room to insert a null.
	 */
	(void) TextCopy(mark = uri->uriDecoded + length + 2, length+1, uri->uriDecoded);

	if (0 < (span = spanScheme(uri->uriDecoded)) && uri->uriDecoded[span] == ':' && spanIPv6(uri->uriDecoded) == 0) {
		/* We have a scheme that does not look like the
		 * first word of an IPv6 address.
		 */
		uri->schemeInfo = uri->uriDecoded + span + 1;
		uri->scheme = mark;
		mark[span] = '\0';

		value = uri->scheme + span + 1;
	} else if (0 < (span = strcspn(uri->uriDecoded, ":")) && 3 < length-span && uri->uriDecoded[span+1] == '/' && uri->uriDecoded[span+2] == '/') {
		/* Invalid or unknown scheme followed by ://. */

		uri->schemeInfo = uri->uriDecoded + span + 1;

		/* Try finding a known scheme that might be prefixed
		 * with ASCII looking multibyte characters sequences.
		 * For example:
		 *
		 *	パソコン・携帯　http://www.c-evian.com
		 */
		for (t = schemeTable; t->name != NULL; t++) {
			if (t->length <= span && TextInsensitiveCompareN(&mark[span - t->length], t->name, t->length) == 0) {
				mark = &mark[span - t->length];
				uri->uriDecoded += span - t->length;
				span = t->length;
				break;
			}
		}

		uri->scheme = mark;
		mark[span] = '\0';

		value = uri->scheme + span + 1;
	} else {
		/* No scheme, no schemeInfo. */
		value = mark;
	}

	/* Is the original URI completely URI encoded in an effort
	 * to obfuscate the actual URI, eg.
	 *
	 *	document.write(unescape('%3C%46%4F%52%4D%20%6E%61%6D%65%3D
	 *	%61%66%66%69%6C%69%61%74%65%46%6F%72%6D%20%6F%6E%73%75%62
	 *	%6D%69%74%3D%22%72%65%74%75%72%6E%20%66%61%72%61%5F%64%61
	 *	%74%65%28%29%3B%22%20%61%63%74%69%6F%6E%3D%68%74%74%70%3A
	 *	%2F%2F%32%31%33%2E%32%31%30%2E%32%33%37%2E%38%33%2F%77%65
	 *	%62%73%63%72%2F%63%6D%64%2F%70%72%6F%74%65%63%74%5F%66%69
	 *	%6C%65%73%2F%79%61%73%73%69%6E%6F%2D%66%69%6C%65%2E%70%68
	 *	%70%20%6D%65%74%68%6F%64%3D%70%6F%73%74%3E'));
	 *
	 * decodes to:
	 *
	 *	document.write(unescape('<FORM name=affiliateForm onsubmit=\"return fara_date();\"
	 *	action=http://213.210.237.83/webscr/cmd/protect_files/yassino-file.php method=post>'));
	 *
	 * Determine if the URI was completely encoded by trying to find the
	 * scheme unencoded in the original URI, otherwise assume that the
	 * URI was completely encoded and that we can trim the junk from the
	 * tail at the first non-URI character. So the above decoded URI
	 * results in:
	 *
	 *	http://213.210.237.83/webscr/cmd/protect_files/yassino-file.php
	 */
	if (uri->scheme != NULL && (str = strstr(u, uri->scheme)) == NULL) {
		span = spanURI(uri->uriDecoded);
		uri->uriDecoded[span] = '\0';
		span = spanURI(value);
		value[span] = '\0';
	}

	if ((uri->fragment = strchr(value, '#')) != NULL)
		*uri->fragment++ = '\0';

	if ((uri->query = strchr(value, '?')) != NULL)
		*uri->query++ = '\0';

	if (value[0] == '/' && value[1] == '/') {
		/* net_path (2396) / authority (rfc 3986) */
		value += 2;

		if ((uri->host = strchr(value, '@')) != NULL) {
			uri->userInfo = value;
			*uri->host++ = '\0';
		} else {
			uri->host = value;
		}

		uriDecodeSelf(uri->host);

		if (0 < (span = alt_spanHost(uri->host, 0))) {
			if ((uri->path = strchr(uri->host + span, '/')) != NULL) {
				/* Shift the scheme & authority left one byte to retain
				 * the leading slash in path and to make room for a null
				 * byte.
				 */
				memmove(mark - 1, mark, uri->path - mark);
				uri->userInfo -= uri->userInfo != NULL;
				uri->scheme -= uri->scheme != NULL;
				uri->path[-1] = '\0';
				uri->host--;
			}

			/* Check for colon-port following host. */
			if (uri->host[span] == ':') {
				uri->port = uri->host + span;
				*uri->port++ = '\0';
				if (uriGetSchemePort(uri) == -1) {
					goto error1;
				}
			}

			/* Remove square brackets from ip-domain-literals. */
			if (*uri->host == '[' && uri->host[span-1] == ']') {
				uri->host[span-1] = '\0';
				uri->host++;
			}
		} else if (uri->scheme != NULL && TextInsensitiveCompare(uri->scheme, "file") == 0) {
			uri->host = "localhost";
			uri->path = value;
		} else {
			uri->host = NULL;
		}
	} else if (value[0] == '/') {
		/* abs_path (2396) */
		if (uri->scheme != NULL && TextInsensitiveCompare(uri->scheme, "file") == 0)
			uri->host = "localhost";
		uri->path = value;
	}

	else if ((uri->scheme == NULL || uriGetSchemePort(uri) == 25) && (uri->host = strchr(value, '@')) != NULL && value < uri->host) {
		*uri->host++ = '\0';

		for (uri->userInfo = value; *uri->userInfo != '\0' && spanLocalPart(uri->userInfo) <= 0; uri->userInfo++)
			;

		if (*uri->userInfo == '\0' || (span = alt_spanDomain(uri->host, 1)) <= 0)
			goto error1;

		uri->host[span] = '\0';
		uri->scheme = "mailto";
		uri->schemeInfo = uri->uriDecoded;
		snprintf(uri->uriDecoded, length+1, "%s%c%s", uri->userInfo, at_sign_delim, uri->host);
	}

	/* This used to be spanDomain, but it is useful to also
	 * try and find either IPv4 or IPv6 addresses.
	 */
	else if (uri->scheme == NULL) {
		if (0 < (span = spanIP(value))) {
			uri->scheme = "ip";
			if (value[span-1] == ']')
				value[span-1] = '\0';
			if (*value == '[')
				value++;
			uri->host = value;
		} else if (0 < (span = alt_spanDomain(value, implicit_domain_min_dots))) {
			if (value[span] == '/') {
				/* Shift the host left one byte to retain the
				 * leading slash in path and to make room for
				 * a null byte after the host name.
				 */
				memmove(value-1, value, span);
				uri->path = value + span;
				uri->path[-1] = '\0';
				value--;

			}

			if (0 < indexValidTLD(value)) {
#ifdef NOT_YET
				uri->schemeInfo = uri->uriDecoded;
#endif
				uri->scheme = "http";
				uri->host = value;
			}
		}
	}

	/* RFC 3986 allows everything after the scheme to be empty.
	 * Consider Firefox's special "about:" URI. Since headers
	 * can look like schemes, only return success for schemes
	 * we know.
	 */
	if (uri->scheme != NULL

	/* A scheme that can be empty has an unknown port 0 value,
	 * while schemes that expect to be non-empty have a port
	 * and need a host.
	 */
	&& ((port = uriGetSchemePort(uri)) == 0 || (0 < port && uri->host != NULL)))
		return uri;
error1:
	free(uri);
error0:
	return NULL;
}

/**
 * @param s
 *	A pointer to a URI encoded C string, which is to be decoded
 *	in place.
 */
void
uriDecodeSelf(char *s)
{
	char *t;
	int a, b;

	for (t = s; *s != '\0'; t++, s++) {
		switch (*s) {
		case '+':
			*t = ' ';
			continue;
		case '%':
			if (0 <= (a = qpHexDigit(s[1])) && 0 <= (b = qpHexDigit(s[2]))) {
				*t = a * 16 + b;;
				s += 2;
				continue;
			}
			/*@fallthrough@*/
		default:
			*t = *s;
			continue;
		}
		break;
	}

	/* Terminate decoded string. */
	*t = '\0';
}

/**
 * @param s
 *	A pointer to a URI encoded C string.
 *
 * @return
 *	A pointer to an allocated C string containing the decode URI.
 *	Its the caller's responsibility to free() this pointer.
 */
char *
uriDecode(const char *s)
{
	char *decoded;

	if ((decoded = strdup(s)) != NULL)
		uriDecodeSelf(decoded);

	return decoded;
}

/**
 * @param s
 *	A pointer to a URI decoded C string.
 *
 * @return
 *	A pointer to an allocated C string containing the encoded URI.
 *	Its the caller's responsibility to free() this pointer.
 */
char *
uriEncode(const char *string)
{
	size_t length;
	char *out, *op;
	static char uri_unreserved[] = "-_.~";
	static const char hex_digit[] = "0123456789ABCDEF";

	length = strlen(string);
	if ((out = malloc(length * 3 + 1)) == NULL)
		return NULL;

	for (op = out ; *string != '\0'; string++) {
		if (isalnum(*string) || strchr(uri_unreserved, *string) != NULL) {
			*op++ = *string;
		} else {
			*op++ = '%';
			*op++ = hex_digit[(*string >> 4) & 0x0F];
			*op++ = hex_digit[*string & 0x0F];
		}
	}
	*op = '\0';

	return out;
}

static const char *
uri_http_origin(const char *url, Vector visited, char *buffer, size_t size, URI **origin);

static URI *
uri_http_list(const char *list, const char *delim, Vector visited, char *buffer, size_t size)
{
	int i;
	Vector args;
	char *arg, *ptr;
	const char *error;
	URI *uri, *origin;

	if (list == NULL)
		return NULL;

	origin = NULL;
	args = TextSplit(list, delim, 0);

	for (i = 0; i < VectorLength(args); i++) {
		if ((arg = VectorGet(args, i)) == NULL)
			continue;

		/* Skip leading symbol name and equals sign. */
		for (ptr = arg; *ptr != '\0'; ptr++) {
			if (!isalnum(*ptr) && *ptr != '_') {
				if (*ptr == '=')
					arg = ptr+1;
				break;
			}
		}

		if ((uri = uriParse2(arg, -1, 1)) != NULL) {
			error = uri_http_origin(uri->uri, visited, buffer, size, &origin);
			free(uri);
			if (error == NULL)
				break;
		}
	}

	VectorDestroy(args);

	return origin;
}

static URI *
uri_http_query(URI *uri, Vector visited, char *buffer, size_t size)
{
	URI *origin;

	if (uri == NULL)
		return NULL;

	if (uri->query == NULL)
		origin = uri_http_list(uri->path, "&", visited, buffer, size);
	else if ((origin = uri_http_list(uri->query, "&", visited, buffer, size)) == NULL)
		origin = uri_http_list(uri->query, "/", visited, buffer, size);
	if (origin == NULL)
		origin = uri_http_list(uri->path, "/", visited, buffer, size);

	return origin;
}

static const char *
uri_http_origin(const char *url, Vector visited, char *buffer, size_t size, URI **origin)
{
	URI *uri;
	Socket2 *socket;
	const char *error;
	char **visited_url, *copy;
	int port, n, span, redirect_count;

	uri = NULL;
	error = NULL;
	*origin = NULL;

	for (socket = NULL, redirect_count = 0; ; ) {
		for (visited_url = (char **) VectorBase(visited); *visited_url != NULL; visited_url++) {
			if (TextInsensitiveCompare(url, *visited_url) == 0) {
				error = uriErrorLoop;
				if (0 < uriDebug)
					syslog(LOG_DEBUG, "%s: %s", error, url);
				goto error2;
			}
		}

		if (VectorAdd(visited, copy = strdup(url))) {
			free(copy);
			error = uriErrorMemory;
			syslog(LOG_ERR, log_error, __FILE__, __LINE__, error);
			goto error2;
		}

		free(uri);

		/* Consider current URL trend for shorter strings using
		 * single dot domains, eg. http://twitter.com/
		 */
		if ((uri = uriParse2(url, -1, 1)) == NULL) {
			error = uriErrorParse;
			if (0 < uriDebug)
				syslog(LOG_DEBUG, "%s: %s", error, url);
			goto error2;
		}

		if ((port = uriGetSchemePort(uri)) == -1) {
			error = uriErrorPort;
			if (0 < uriDebug)
				syslog(LOG_DEBUG, "%s: %s", error, uri->uriDecoded);
			goto error2;
		}

		/* We won't bother with https: nor anything that didn't
		 * default to the http: port 80 nor explicitly specify
		 * a port.
		 */
		if (port == 443 || (port != 80 && uri->port == NULL)) {
			error = uriErrorNotHttp;
			if (0 < uriDebug)
				syslog(LOG_DEBUG, "%s: %s", error, uri->uriDecoded);
			goto error2;
		}

#ifdef URI_HTTP_ORIGIN_QUERY_FIRST
		/* Check URI query string first if any. */
		if ((*origin = uri_http_query(uri, visited, buffer, size)) != NULL)
			break;
#endif
		if (0 < uriDebug)
			syslog(LOG_DEBUG, "connect %s:%d", uri->host, port);

		socketClose(socket);

		if (socketOpenClient(uri->host, port, socket_timeout, NULL, &socket) == SOCKET_ERROR) {
			error = uriErrorConnect;
			if (0 < uriDebug)
				syslog(LOG_DEBUG, "%s: %s:%d", error, uri->host, port);
			goto error2;
		}

		(void) fileSetCloseOnExec(socketGetFd(socket), 1);
		socketSetTimeout(socket, socket_timeout);
		(void) socketSetLinger(socket, 0);

		/* NOTE the query string is not added in order to
		 * minimize  identifying / confirming anything.
		 */
		n = snprintf(
			buffer, size, "HEAD %s%s%s%s HTTP/1.0\r\nHost: %s",
			(uri->path == NULL || *uri->path != '/') ? "/" : "",
			uri->path == NULL ? "" : uri->path,
			(0 < redirect_count && uri->query != NULL) ? "?" : "",
			(0 < redirect_count && uri->query != NULL) ? uri->query : "",
			uri->host
		);

		/* Some spam servers react differently when the default
		 * port 80 is appended to the host name. In one test case
		 * with the port, the server redirected back to the same
		 * URL creating a loop; without the port, it redirected back
		 * to the same host, but a different path that eventually
		 * resulted in a 200 status.
		 */
		if (port != 80)
			n += snprintf(buffer+n, size-n, ":%d", port);

		n += snprintf(buffer+n, size-n, "\r\n\r\n");

		if (0 < uriDebug)
			syslog(LOG_DEBUG, "> %d:%s", n, buffer);

		if (socketWrite(socket, (unsigned char *) buffer, n) != n) {
			error = uriErrorWrite;
			if (0 < uriDebug)
				syslog(LOG_DEBUG, "%s: %s:%d", error, uri->host, port);
			goto error2;
		}

		if ((n = (int) socketReadLine(socket, buffer, size)) < 0) {
			error = uriErrorRead;
			if (0 < uriDebug)
				syslog(LOG_DEBUG, "%s (%d): %s:%d", error, n, uri->host, port);
			goto error2;
		}

		if (0 < uriDebug)
			syslog(LOG_DEBUG, "< %d:%s", n, buffer);

		if (sscanf(buffer, "HTTP/%*s %d", &uri->status) != 1) {
			error = uriErrorHttpResponse;
			if (0 < uriDebug)
				syslog(LOG_DEBUG, "%s: %s", error, buffer);
			goto error2;
		}

		if (400 <= uri->status) {
			error = uriErrorNoOrigin;
			if (0 < uriDebug)
				syslog(LOG_DEBUG, "%s: %s", error, uri->uriDecoded);
			goto error2;
		}

		/* Have we found the origin server? */
		if (200 <= uri->status && uri->status < 300) {
			if (0 < uriDebug)
				syslog(LOG_DEBUG, "found HTTP origin %s:%d", uri->host, uriGetSchemePort(uri));
			*origin = uri;
			goto origin_found;
		}

		/* Assume that we have a 301, 302, 303, or 307 response.
		 * Read and parse the Location header. If not found then
		 * it was different 3xx response code and an error.
		 */

		while (0 < (n = socketReadLine(socket, buffer, size))) {
			if (0 < uriDebug)
				syslog(LOG_DEBUG, "< %d:%s", n, buffer);
			if (0 < TextInsensitiveStartsWith(buffer, "Location:")) {
				url = buffer + sizeof("Location:")-1;
				url += strspn(url, " \t");

				/* Is it a redirection on the same server?
				 * Does it have a leading scheme or not?
				 * Without a scheme, assume relative URL.
				 */
				if (!(0 < (span = spanScheme(url)) && url[span] == ':')) {
					/* Use the upper portion of the buffer
					 * just past the end of the line to
					 * construct a new URL.
					 */
					int length;
					char *path, root[] = { '/', '\0' };

					/* Is path defined? */
					path = (uri->path == NULL || *uri->path != '/') ? root : uri->path;
					/* Find previous slash. */
					length = strlrcspn(path, strlen(path), "/");
					/* Remove basename. */
					path[length] = '\0';
					/* Include null byte in length. */
					n++;
					/* Build absolute path from relative one. */
					length = snprintf(buffer+n, size-n, "http://%s:%d%s%s", uri->host, port, path, url);
					/* Buffer overflow? */
					if (size-n <= length) {
						error = uriErrorOverflow;
						syslog(LOG_ERR, log_error, __FILE__, __LINE__, error);
						goto error2;
					}

					url = buffer+n;
				}

				redirect_count++;
				break;
			}
		}

		if (n <= 0) {
			error = uriErrorNoLocation;
			if (0 < uriDebug)
				syslog(LOG_DEBUG, "%s: %s:%d", error, uri->host, port);
			goto error2;
		}
#ifndef URI_HTTP_ORIGIN_QUERY_FIRST
		/* Check URI query string first if any. */
		if ((*origin = uri_http_query(uri, visited, buffer, size)) != NULL)
			break;
#endif
	}
error2:
	free(uri);
origin_found:
	socketClose(socket);

	return error;
}

/**
 * @param url
 *	Find the HTTP origin server by following all redirections.
 *
 * @param origin
 *	A pointer to a URI pointer used to pass back to the caller
 *	the HTTP origin server found. Its the caller's responsibility
 *	to free() this pointer. Otherwise NULL on error.
 *
 * @return
 *	NULL on success; otherwise on error a pointer to a C string
 *	giving brief decription.
 *
 * @see
 *	uriError messages
 */
const char *
uriHttpOrigin(const char *url, URI **origin)
{
	char *buffer;
	Vector visited;
	const char *error;

	if (url == NULL && origin == NULL) {
		error = uriErrorNullArgument;
		errno = EFAULT;
		goto error0;
	}

	*origin = NULL;

	if ((buffer = malloc(HTTP_BUFFER_SIZE)) == NULL) {
		error = uriErrorMemory;
		syslog(LOG_ERR, log_error, __FILE__, __LINE__, error);
		goto error0;
	}

	if ((visited = VectorCreate(10)) == NULL) {
		error = uriErrorMemory;
		syslog(LOG_ERR, log_error, __FILE__, __LINE__, error);
		goto error1;
	}

	VectorSetDestroyEntry(visited, free);

	error = uri_http_origin(url, visited, buffer, HTTP_BUFFER_SIZE, origin);

	VectorDestroy(visited);
error1:
	free(buffer);
error0:
	return error;
}
/***********************************************************************
 *** Parsing URI in MIME parts
 ***********************************************************************/

#ifndef URI_MIME_BUFFER_SIZE
#define URI_MIME_BUFFER_SIZE	1024
#endif

#define HTML_ENTITY_SHY		0xAD

struct uri_mime {
	/* Must be first in structure for MIME API */
	MimeHooks hook;

	int length;
	int is_body;
	int is_text_part;
	int headers_and_body;
	char buffer[URI_MIME_BUFFER_SIZE];
	UriMimeHook uri_found_cb;
	void *data;
};

static void
uri_mime_free(Mime *m, void *_data)
{
	UriMime *hold = _data;

	if (hold != NULL) {
		free(hold);
	}
}

static void
uri_mime_body_start(Mime *m, void *_data)
{
	UriMime *hold = _data;

	/* When a message or mime part has no Content-Type header
	 * then the message / mime part defaults to text/plain
	 * RFC 2045 section 5.2.
	 */
	if (!m->state.has_content_type)
		hold->is_text_part = 1;

	hold->is_body = 1;
	hold->length = 0;
}

static void
uri_mime_body_finish(Mime *m, void *_data)
{
	UriMime *hold = _data;

	hold->is_text_part = 0;
	hold->is_body = 0;
	hold->length = 0;
}

static void
uri_mime_decoded_octet(Mime *m, int ch, void *_data)
{
	URI *uri;
	UriMime *hold = _data;

	/* Only process text only parts. Otherwise with simplified
	 * implicit URI rules, decoding binary attachments like
	 * images can result in false positives.
	 */
	if (!hold->headers_and_body && hold->is_body && !hold->is_text_part)
		return;

        /* Ignore CR as it does not help us with parsing.
         * Assume LF will follow.
         */
        if (ch == '\r')
                return;

        /* If the hold buffer is full, just dump it. The
         * buffer is larger that any _normal_ URL should
         * be and its assumed it would fail to parse.
         */
	if (sizeof (hold->buffer) <= hold->length)
		hold->length = 0;

	/* Accumulate URI characters in the hold buffer. */
	if (isCharURI(ch)) {
		/* Look for HTML numerical entities &#NNN; or &#xHHHH; */
		if (0 < hold->length && ch == ';') {
			int offset;
			size_t length;

			offset = strlrcspn(hold->buffer, hold->length, "&");
			hold->buffer[hold->length++] = ch;

			if (0 < offset) {
				offset--;

				/* Note that htmlEntityDecode() discards soft-hyphen &shy; */
				length = htmlEntityDecode(
					hold->buffer+offset, hold->length-offset,
					hold->buffer+offset, hold->length-offset
				);
				hold->length = offset + length;
			}
		} else {
			hold->buffer[hold->length++] = ch;
		}
		return;
	}

	/* Attempt to parse the hold buffer for a valid URI. */
	if (0 < hold->length) {
		int value;

		hold->buffer[hold->length] = '\0';

		/* Skip leading src= or href= that could be prepended when
		 * a message is text/html with no quotes around URL contained
		 * there in.
		 */
		for (value = 0; hold->buffer[value] != '\0'; value++) {
			if (!isalnum(hold->buffer[value]))
				break;
		}
		value = hold->buffer[value] == '=' ? value+1 : 0;

		/* RFC 2396 lists parens and single quotes as unreserved mark
		 * characters that can appear in a URI (how stupid). If the
		 * hold buffer looks to be bracketed by these characters, then
		 * strip them off. RFC 3986 has them as reserved. Note too that
		 * parens and single quotes might be imbalanced.
		 */
		if (hold->buffer[value] == '(' || hold->buffer[value] == '\'')
			value++;
		if (hold->buffer[hold->length-1] == ')' || hold->buffer[hold->length-1] == '\'')
			hold->length--;

		/* RFC 3986 and 1035 does not allow underscore in a URI scheme
		 * nor in a domain/host name.
		 */
		while (hold->buffer[hold->length-1] == '_') {
			hold->length--;
			hold->buffer[hold->length] = '\0';
		}
		while (hold->buffer[value] == '_')
			value++;

		uri = uriParse2(hold->buffer+value, hold->length-value, IMPLICIT_DOMAIN_MIN_DOTS);

		if (uri != NULL) {
			if (2 < uriDebug)
				syslog(LOG_DEBUG, "found URL \"%s\"", uri->uri);
			if (hold->uri_found_cb != NULL)
				(*hold->uri_found_cb)(uri, hold->data);
			free(uri);
		}

		hold->length = 0;
	}
}

static void
uri_mime_header(Mime *m, void *_data)
{
	UriMime *hold = _data;

	if (hold->headers_and_body) {
		unsigned char *s;
		for (s = m->source.buffer; *s != '\0'; s++)
			uri_mime_decoded_octet(m, *s, _data);
		uri_mime_decoded_octet(m, '\r', _data);
		uri_mime_decoded_octet(m, '\n', _data);
	}

	if (TextMatch((char *) m->source.buffer, "Content-Type:*text/*", m->source.length, 1))
		hold->is_text_part = 1;
}

/**
 * @param uri_found_cb
 *	A call-back function when a URI is found.
 *
 * @param all
 *	When set search both headers and body for URI, otherwise
 *	just the body portion.
 *
 * @param data
 *	Application data to be passed to URI call-backs.
 *
 * @return
 *	A pointer to a UriMime structure suitable for passing to
 *	mimeHooksAdd(). The UriMime * will have to cast to MimeHooks *.
 *	This structure and data are freed by mimeFree().
 */
UriMime *
uriMimeInit(UriMimeHook uri_found_cb, int all, void *data)
{
	UriMime *hold;

	if ((hold = calloc(1, sizeof (UriMime))) != NULL) {
		hold->data = data;
		hold->headers_and_body = all;
		hold->uri_found_cb = uri_found_cb;

		hold->hook.data = hold;
		hold->hook.free = uri_mime_free;
		hold->hook.header = uri_mime_header;
		hold->hook.body_start = uri_mime_body_start;
		hold->hook.body_finish = uri_mime_body_finish;
		hold->hook.decoded_octet = uri_mime_decoded_octet;
	}

	return hold;
}

/***********************************************************************
 ***
 ***********************************************************************/

#ifdef TEST
#include <stdio.h>
#include <stdlib.h>

#ifdef HAVE_SYS_STAT_H
# include <sys/stat.h>
#endif

#include <com/snert/lib/sys/sysexits.h>
#include <com/snert/lib/util/getopt.h>
#include <com/snert/lib/net/pdq.h>

static int exit_code;
static int headers_and_body;
static int check_link;
static int check_query;
static int check_subdomains;
static int print_uri_parse;
static char *dBlOption;
static char *ipBlOption;
static char *nsBlOption;
static char *nsIpBlOption;
static char *uriBlOption;
static char *mailBlOption;
static Vector print_uri_ports;
static long *uri_ports;

static const char *mailBlDomains = "*";
static Vector mail_bl_domains;

#ifdef MAIL_BL_DOMAINS
	 "gmail.*"
	",googlemail.*"
	",hotmail.*"
	",yahoo.*"
	",aol.*"
	",aim.*"
	",live.*"
	",ymail.com"
	",rocketmail.com"
	",centrum.cz"
	",centrum.sk"
	",inmail24.com"
	",libero.it"
	",mail2world.com"
	",msn.com"
	",she.com"
	",shuf.com"
	",sify.com"
	",terra.es"
	",tiscali.it"
	",ubbi.com"
	",virgilio.it"
	",voila.fr"
	",walla.com"
	",y7mail.com"
	",yeah.net"
#endif

static char usage[] =
"usage: uri [-ablLpqRsUv][-A delim][-d dbl,...][-i ip-bl,...][-m mail-bl,...]\n"
"           [-M domain-pat,...][-n ns-bl,...][-N ns-ip-bl,...][-u uri-bl,...]\n"
"           [-P port,...][-Q ns,...][-t sec][-T sec][arg ...][<input]\n"
"\n"
"-a\t\tcheck headers and body\n"
"-b\t\tcheck body only (default)\n"
"-A delim\tan alternative delimiter to replace the at-sign (@)\n"
"-b\t\tassume text body, otherwise mail message headers & body\n"
"-d dbl,...\tcomma separate list of domain black lists\n"
"-i ip-bl,...\tDNS suffix[/mask] list to apply. Without the /mask\n"
"\t\ta suffix would be equivalent to suffix/0x00fffffe\n"
"-l\t\tcheck HTTP links are valid & find origin server\n"
"-L\t\twait for all the replies from DNS list queries, need -v\n"
"-m mail-bl,...\tDNS suffix[/mask] list to apply. Without the /mask\n"
"\t\ta suffix would be equivalent to suffix/0x00fffffe\n"
"-M domain,...\tlist of domain glob-like patterns by which to limit\n"
"\t\tchecking against mail-bl; default *\n"
"-n ns-bl,...\tDNS suffix[/mask] list to apply. Without the /mask\n"
"\t\ta suffix would be equivalent to suffix/0x00fffffe\n"
"-N ns-ip-bl,...\tDNS suffix[/mask] list to apply. Without the /mask\n"
"\t\ta suffix would be equivalent to suffix/0x00fffffe\n"
"-p\t\tprint each URI parsed\n"
"-P port,...\tselect only the URI corresponding to the comma\n"
"\t\tseparated list of port numbers to print and/or test\n"
"-q\t\tcheck URL query part for embedded URLs\n"
"-Q ns,...\tcomma separated list of alternative name servers\n"
"-R\t\tenable DNS round robin mode, default parallel mode\n"
"-s\t\tcheck URI domain has valid SOA\n"
"-t sec\t\tHTTP socket timeout in seconds, default 60\n"
"-T sec\t\tDNS timeout in seconds, default 45\n"
"-u uri-bl,...\tDNS suffix[/mask] list to apply. Without the /mask\n"
"\t\ta suffix would be equivalent to suffix/0x00fffffe\n"
"-U\t\tcheck sub-domains segments of URI domains\n"
"-v\t\tverbose logging to system's user log\n"
"\n"
"Each argument is either a filepath or string to be parsed and tested. If\n"
"the filepath \"-\" is given, then standard input is read. Also input can\n"
"be redirected.\n"
"\n"
"Exit Codes\n"
QUOTE(EXIT_SUCCESS) "\t\tall URI tested are OK\n"
QUOTE(EXIT_FAILURE) "\t\tone or more URI are blacklisted\n"
QUOTE(EX_USAGE) "\t\tusage error\n"
QUOTE(EX_SOFTWARE) "\t\tinternal error\n"
"\n"
LIBSNERT_STRING " " LIBSNERT_COPYRIGHT "\n"
;

#if ! defined(__MINGW32__)
void
syslog(int level, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	if (logFile == NULL)
		vsyslog(level, fmt, args);
	else
		LogV(level, fmt, args);
	va_end(args);
}
#endif

PDQ *pdq;
int debug;
int check_soa;
DnsList *d_bl_list;
DnsList *ip_bl_list;
DnsList *ns_bl_list;
DnsList *ns_ip_bl_list;
DnsList *uri_bl_list;
DnsList *mail_bl_list;
Vector uri_names_seen;
Vector mail_names_seen;
const char *name_servers;

void
test_uri(URI *uri, const char *filename)
{
	PDQ_valid_soa code;
	const char *list_name = NULL;

	if ((list_name = dnsListQueryName(d_bl_list, pdq, uri_names_seen, uri->host)) != NULL) {
		if (filename != NULL)
			printf("%s: ", filename);
		printf("%s domain blacklisted %s\n", uri->host, list_name);
		exit_code = EXIT_FAILURE;
	}

	if ((list_name = dnsListQueryDomain(uri_bl_list, pdq, uri_names_seen, check_subdomains, uri->host)) != NULL) {
		if (filename != NULL)
			printf("%s: ", filename);
		printf("%s domain blacklisted %s\n", uri->host, list_name);
		exit_code = EXIT_FAILURE;
	}

	if ((list_name = dnsListQueryNs(ns_bl_list, ns_ip_bl_list, pdq, uri_names_seen, uri->host)) != NULL) {
		if (filename != NULL)
			printf("%s: ", filename);
		printf("%s NS blacklisted %s\n", uri->host, list_name);
		exit_code = EXIT_FAILURE;
	}

	if ((list_name = dnsListQueryIP(ip_bl_list, pdq, uri_names_seen, uri->host)) != NULL) {
		if (filename != NULL)
			printf("%s: ", filename);
		printf("%s IP blacklisted %s\n", uri->host, list_name);
		exit_code = EXIT_FAILURE;
	}

	if (uriGetSchemePort(uri) == SMTP_PORT && (list_name = dnsListQueryMail(mail_bl_list, pdq, mail_bl_domains, mail_names_seen, uri->uriDecoded)) != NULL) {
		if (filename != NULL)
			printf("%s: ", filename);
		printf("%s mail blacklisted %s\n", uri->uriDecoded, list_name);
		exit_code = EXIT_FAILURE;
	}

	if (check_soa && (code = pdqTestSOA(pdq, PDQ_CLASS_IN, uri->host, NULL)) != PDQ_SOA_OK) {
		if (filename != NULL)
			printf("%s: ", filename);
		printf("%s bad SOA %s (%d)\n", uri->host, pdqSoaName(code), code);
		exit_code = EXIT_FAILURE;
	}
}

void
process(URI *uri, const char *filename)
{
	long *p;
	const char *error;
	URI *origin = NULL;

	if (uri == NULL)
		return;

	if (uri_ports != NULL) {
		for (p = uri_ports; 0 <= *p; p++) {
			if (uriGetSchemePort(uri) == *p)
				break;
		}

		if (*p < 0)
			return;
	}

	if (print_uri_parse) {
		if (filename != NULL)
			printf("%s: ", filename);
		printf("uri=%s\n", uri->uri);
		printf("\turiDecoded=%s\n", TextNull(uri->uriDecoded));
		printf("\tscheme=%s\n", TextNull(uri->scheme));
		printf("\tschemeInfo=%s\n", TextNull(uri->schemeInfo));
		printf("\tuserInfo=%s\n", TextNull(uri->userInfo));
		printf("\thost=%s\n", TextNull(uri->host));
		printf("\tport=%d\n", uriGetSchemePort(uri));
		printf("\tpath=%s\n", TextNull(uri->path));
		printf("\tquery=%s\n", TextNull(uri->query));
		printf("\tfragment=%s\n", TextNull(uri->fragment));
	} else if (print_uri_ports != NULL) {
		fputs(uri->uriDecoded, stdout);
		fputc('\n', stdout);
	}

	if (uri->host != NULL)
		test_uri(uri, filename);

	if (check_link && uri->host != NULL) {
		error = uriHttpOrigin(uri->uriDecoded, &origin);
		if (filename != NULL)
			printf("%s: ", filename);
		printf("%s -> ", uri->uriDecoded);
		printf("%s\n", error == NULL ? origin->uri : error);
		if (error == uriErrorLoop)
			exit_code = EXIT_FAILURE;
	}

	if (origin != NULL && origin->host != NULL && strcmp(uri->host, origin->host) != 0) {
		test_uri(origin, filename);
		free(origin);
	}
}

void
process_list(const char *list, const char *delim, const char *filename)
{
	int i;
	URI *uri;
	Vector args;
	char *arg, *ptr;

	if (list == NULL)
		return;

	args = TextSplit(list, delim, 0);

	for (i = 0; i < VectorLength(args); i++) {
		if ((arg = VectorGet(args, i)) == NULL)
			continue;

		/* Skip leading symbol name and equals sign. */
		for (ptr = arg; *ptr != '\0'; ptr++) {
			if (!isalnum(*ptr) && *ptr != '_') {
				if (*ptr == '=')
					arg = ptr+1;
				break;
			}
		}

		uri = uriParse2(arg, -1, 1);
		process(uri, filename);
		free(uri);
	}

	VectorDestroy(args);
}

void
process_query(URI *uri, const char *filename)
{
	if (uri->query == NULL) {
		process_list(uri->path, "&", filename);
	} else {
		process_list(uri->query, "&", filename);
		process_list(uri->query, "/", filename);
	}
	process_list(uri->path, "/", filename);
}

void
process_uri(URI *uri, void *data)
{
	process(uri, data);
	if (check_query)
		process_query(uri, data);
}

int
process_input(Mime *m, FILE *fp, const char *filename)
{
	int ch;
	unsigned lineno = 1;

	if (fp != NULL) {
		mimeReset(m);

		if (debug)
			syslog(LOG_DEBUG, "file=%s line=%u", filename, lineno);
		do {
			ch = fgetc(fp);
			if (debug && ch == '\n') {
				lineno++;
				syslog(LOG_DEBUG, "file=%s line=%u", filename, lineno);
			}
			(void) mimeNextCh(m, ch);
		} while (ch != EOF);

		(void) fflush(stdout);
	}

	return 0;
}

int
process_file(const char *filename)
{
	int rc;
	FILE *fp;
	Mime *mime;
	UriMime *hold;

	rc = -1;

	/* Check for standard input. */
	if (filename[0] == '-' && filename[1] == '\0')
		fp = stdin;

	/* Otherwise open the file. */
	else if ((fp = fopen(filename, "r")) == NULL)
		goto error0;

	if ((mime = mimeCreate()) == NULL)
		goto error1;

	if ((hold = uriMimeInit(process_uri, headers_and_body, (void *)filename)) == NULL)
		goto error2;

	mimeHooksAdd(mime, (MimeHooks *)hold);
	mimeHeadersFirst(mime, headers_and_body);
	rc = process_input(mime, fp, filename);
error2:
	mimeFree(mime);
error1:
	fclose(fp);
error0:
	return rc;
}

void
process_string(const char *s)
{
	Mime *mime;
	UriMime *hold;

	if ((mime = mimeCreate()) == NULL)
		return;

	mimeHeadersFirst(mime, 0);

	if ((hold = uriMimeInit(process_uri, 1, (void *) "(arg)")) != NULL) {
		mimeHooksAdd(mime, (MimeHooks *)hold);
		for ( ; *s != '\0'; s++)
			(void) mimeNextCh(mime, *s);
		(void) mimeNextCh(mime, EOF);
		(void) fflush(stdout);
	}

	mimeFree(mime);
}

int
main(int argc, char **argv)
{
	int i, ch;

	while ((ch = getopt(argc, argv, "fabA:d:m:M:i:n:N:u:UlLmpP:qQ:RsT:t:v")) != -1) {
		switch (ch) {
		case 'f':
			/* Place holder for old -f option for AlexB. */
			break;
		case 'a':
			headers_and_body = 1;
			break;
		case 'b':
			headers_and_body = 0;
			break;
		case 'A':
			at_sign_delim = *optarg;
			break;
		case 'd':
			dBlOption = optarg;
			break;
		/* case 'D': reserved for possible future domain exception list. */

		case 'i':
			ipBlOption = optarg;
			break;
		case 'n':
			nsBlOption = optarg;
			break;
		case 'N':
			nsIpBlOption = optarg;
			break;
		case 'u':
			uriBlOption = optarg;
			break;
		case 'm':
			mailBlOption = optarg;
			break;
		case 'M':
			mailBlDomains = optarg;
			break;
		case 'U':
			check_subdomains = 1;
			break;
		case 'l':
			check_link = 1;
			break;
		case 'L':
			dnsListSetWaitAll(1);
			break;;
		case 'P':
			print_uri_ports = TextSplit(optarg, ",", 0);
			break;
		case 'p':
			print_uri_parse = 1;
			break;
		case 'q':
			check_query = 1;
			break;
		case 'Q':
			name_servers = optarg;
			break;
		case 's':
			check_soa = 1;
			break;
		case 't':
			uriSetTimeout(strtol(optarg, NULL, 10) * 1000);
			break;
		case 'T':
			pdqMaxTimeout(strtol(optarg, NULL, 10));
			break;

		case 'R':
			pdqSetRoundRobin(1);
			break;
		case 'v':
#ifdef VAR_LOG_DEBUG
			openlog("uri", LOG_PID, LOG_USER);
#else
			LogOpen("(standard error)");
			LogSetProgramName("uri");
#endif
			dnsListSetDebug(1);
			socketSetDebug(1);
			uriSetDebug(4);
			pdqSetDebug(1);
			debug++;
			break;
		default:
			(void) fputs(usage, stderr);
			return EX_USAGE;
		}
	}

	if (pdqInit()) {
		fprintf(stderr, "pdqInit() failed\n");
		exit(EX_SOFTWARE);
	}

	if (name_servers != NULL) {
		Vector servers = TextSplit(name_servers, ",", 0);
		if (pdqSetServers(servers)) {
			fprintf(stderr, "pdqSetServers() failed\n");
			exit(EX_SOFTWARE);
		}
		VectorDestroy(servers);
	}

	if ((pdq = pdqOpen()) == NULL) {
		fprintf(stderr, "pdqOpen() failed\n");
		exit(EX_SOFTWARE);
	}

	uri_names_seen = VectorCreate(10);
	VectorSetDestroyEntry(uri_names_seen, free);
	mail_names_seen = VectorCreate(10);
	VectorSetDestroyEntry(mail_names_seen, free);

	d_bl_list = dnsListCreate(dBlOption);
	ip_bl_list = dnsListCreate(ipBlOption);
	ns_bl_list = dnsListCreate(nsBlOption);
	ns_ip_bl_list = dnsListCreate(nsIpBlOption);
	uri_bl_list = dnsListCreate(uriBlOption);
	mail_bl_list = dnsListCreate(mailBlOption);
	mail_bl_domains = TextSplit(mailBlDomains, ",", 0);

	if (0 < VectorLength(print_uri_ports)) {
		uri_ports = malloc(sizeof (long) * (VectorLength(print_uri_ports) + 1));

		for (i = 0; i < VectorLength(print_uri_ports); i++) {
			char *port = VectorGet(print_uri_ports, i);
			uri_ports[i] = strtol(port, NULL, 10);
		}
		uri_ports[i] = -1;
	}

	exit_code = EXIT_SUCCESS;

	if (argc <= optind) {
		(void) process_file("-");
	} else {
		for (i = optind; i < argc; i++) {
			struct stat sb;
			if ((argv[i][0] == '-' && argv[i][1] == '\0') || stat(argv[i], &sb) == 0) {
				(void) process_file(argv[i]);
			} else {
				process_string(argv[i]);
			}
		}
	}

	VectorDestroy(mail_bl_domains);
	VectorDestroy(mail_names_seen);
	VectorDestroy(uri_names_seen);
	dnsListFree(mail_bl_list);
	dnsListFree(uri_bl_list);
	dnsListFree(ns_ip_bl_list);
	dnsListFree(ns_bl_list);
	dnsListFree(ip_bl_list);
	dnsListFree(d_bl_list);
	pdqClose(pdq);
	pdqFini();

	return exit_code;
}
#endif
