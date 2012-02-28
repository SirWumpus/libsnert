/*
 * uri.c
 *
 * RFC 2821, 2396 Support Routines
 *
 * Copyright 2006, 2010 by Anthony Howe. All rights reserved.
 */

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
#include <com/snert/lib/net/http.h>
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
	/* Throw away the ASCII controls, space, and high-bit octets.
	 *
	 *** AlexB reports instances of spam with URLs containing 8-bit
	 *** octets in some character encoding, like "big5". RFC 3986
	 *** section 2 "Characters" appears to allow for this.
	 ***
	 *** 	"http://cheng-xia5¡Cinfo/"	0xA143	dot
	 ***	"http://cheng-xia5¡Dinfo/"	0xA144	dot
	 ***	"http://cheng-xia5¡Oinfo/"	0xA14F	dot
	 ***/
	if (octet <= 0x20 /* || 0x7F <= octet */)
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
 * Removed alt_* code and moved the changes to domain names into
 * the common functions in order to avoid duplicate code issues.
 */
#define alt_spanDomain		spanDomain
#define alt_spanHost		spanHost

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
		 *	$B%Q%=%3%s!&7HBS!!(Bhttp://www.c-evian.com
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
	Vector mail_bl_headers;
	Vector uri_bl_headers;
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
		}

		/* Look for Big5 0xA143, 0xA144, 0xA14F representations of dot. */
		else if (0 < hold->length && (ch == 'C' || ch == 'D' || ch == 'O') && hold->buffer[hold->length-1] == (char) 0xA1) {
			hold->buffer[hold->length-1] = '.';
		}

		else {
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

	if (2 < uriDebug)
		syslog(LOG_DEBUG, "header [%s]", m->source.buffer);

	if (TextMatch((char *) m->source.buffer, "Content-Type:*text/*", m->source.length, 1)
	||  TextMatch((char *) m->source.buffer, "Content-Type:*application/*;*name=*.txt*", m->source.length, 1)
	||  TextMatch((char *) m->source.buffer, "Content-Type:*application/*;*name=*.htm*", m->source.length, 1))
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
 *** Common CLI, CGI and daemon.
 ***********************************************************************/
#if defined(TEST) || defined(DAEMON)

#include <stdio.h>
#include <stdlib.h>

#ifdef HAVE_LIMITS_H
# include <limits.h>
#endif
#ifdef HAVE_SYS_STAT_H
# include <sys/stat.h>
#endif

#include <com/snert/lib/net/pdq.h>
#include <com/snert/lib/util/Buf.h>
#include <com/snert/lib/util/cgi.h>
#include <com/snert/lib/util/option.h>
#include <com/snert/lib/sys/sysexits.h>

#ifndef LINE_WRAP
#define LINE_WRAP			72
#endif

# if defined(DAEMON)
#  define _NAME				"urid"
# else
#  define _NAME			 	"uri"
#endif

#if !defined(CF_DIR)
# if defined(__WIN32__)
#  define CF_DIR			"."
# else
#  define CF_DIR			"/etc/" _NAME
# endif
#endif

#if !defined(CF_FILE)
# define CF_FILE			CF_DIR "/" _NAME ".cf"
#endif

typedef struct {
	const char *file;
	unsigned line;
	unsigned hits;
	unsigned found;
} FileLine;

typedef struct {
	CGI cgi;
	PDQ *pdq;
	FILE *out;
	int cgi_mode;
	long max_hits;
	FileLine source;
	int print_uri_parse;
	int headers_and_body;
	Vector uri_names_seen;
	Vector mail_names_seen;
} UriWorker;

static const char usage_title[] =
  "\n"
"# " _NAME "\n"
"# \n"
"# " LIBSNERT_COPYRIGHT "\n"
"#"
;
Option opt_title		= { "",				NULL,		usage_title };

static const char usage_info[] =
  "Write the configuration and compile time options to standard output\n"
"# and exit.\n"
"#"
;
Option opt_info			= { "info", 			NULL,		usage_info };

static const char usage_syntax[] =
  "Option Syntax\n"
"# \n"
"# Options can be expressed in four different ways. Boolean options\n"
"# are expressed as +option or -option to turn the option on or off\n"
"# respectively. Numeric, string, and list options are expressed as\n"
"# option=value to set the option or option+=value to append to a\n"
"# list. Note that the +option and -option syntax are equivalent to\n"
"# option=1 and option=0 respectively. String values containing white\n"
"# space must be quoted using single (') or double quotes (\"). Option\n"
"# names are case insensitive.\n"
"# \n"
"# Some options, like +help or -help, are treated as immediate\n"
"# actions or commands. Unknown options are ignored and not reported.\n"
"# The first command-line argument is that which does not adhere to\n"
"# the above option syntax. The special command-line argument -- can\n"
"# be used to explicitly signal an end to the list of options.\n"
"# \n"
"# The default options, as shown below, can be altered by specifying\n"
"# them on the command-line or within an option file, which simply\n"
"# contains command-line options one or more per line and/or on\n"
"# multiple lines. Comments are allowed and are denoted by a line\n"
"# starting with a hash (#) character. If the file option is defined\n"
"# and not empty, then it is parsed first, followed by the command\n"
"# line options.\n"
;
Option opt_syntax		= { "",				NULL,		usage_syntax };

static const char usage_help[] =
  "Write the option summary to standard output and exit. The output\n"
"# is suitable for use as an option file. For Windows this option\n"
"# can be assigned a file path string to save the output to a file,\n"
"# eg. help=./" CF_FILE "\n"
"#"
;
Option opt_help			= { "help", 			NULL,		usage_help };

static Option opt_version	= { "version",		NULL,	"Show version and copyright." };

static const char usage_domain_bl[] =
  "A list of domain black list suffixes to consult, like .dbl.spamhaus.org.\n"
"# The host or domain name found in a URI is checked against these DNS black\n"
"# lists. These black lists are assumed to use wildcards entries, so only a\n"
"# single lookup is done. IP-as-domain in a URI are ignored.\n"
"#"
;
static Option opt_domain_bl	= { "domain-bl",	"",	usage_domain_bl };

static const char usage_mail_bl[] =
  "A list of mail address black list suffixes to consult. The MAIL FROM:\n"
"# address and mail addresses found in select headers and the message are MD5\n"
"# hashed, which are then checked against these black lists. Aggregate lists\n"
"# are supported using suffix/mask. Without a /mask, suffix is the same as\n"
"# suffix/0x00FFFFFE.\n"
"# "
;
Option opt_mail_bl		= { "mail-bl",		"",	usage_mail_bl };

static const char usage_mail_bl_headers[] =
  "A list of mail headers to parse for mail addresses and check against\n"
"# one or more mail address black lists. Specify the empty list to disable.\n"
"#"
;
Option opt_mail_bl_headers	= { "mail-bl-headers",	"From;Reply-To;Sender",	usage_mail_bl_headers };

static const char usage_mail_bl_domains[] =
  "A list of domain glob-like patterns for which to test against mail-bl,\n"
"# typically free mail services. This reduces the load on public BLs.\n"
"# Specify * to test all domains, empty list to disable.\n"
"#"
;
Option opt_mail_bl_domains	= {
	"mail-bl-domains",

	 "gmail.*"
	";hotmail.*"
	";live.*"
	";yahoo.*"
	";aol.*"
	";aim.com"
	";cantv.net"
	";centrum.cz"
	";centrum.sk"
	";googlemail.com"
	";gmx.*"
	";inmail24.com"
	";jmail.co.za"
	";libero.it"
	";luckymail.com"
	";mail2world.com"
	";msn.com"
	";rediff.com"
	";rediffmail.com"
	";rocketmail.com"
	";she.com"
	";shuf.com"
	";sify.com"
	";terra.*"
	";tiscali.it"
	";tom.com"
	";ubbi.com"
	";virgilio.it"
	";voila.fr"
	";vsnl.*"
	";walla.com"
	";wanadoo.*"
	";windowslive.com"
	";y7mail.com"
	";yeah.net"
	";ymail.com"

	, usage_mail_bl_domains
};

static const char usage_uri_bl[] =
  "A list of domain name black list suffixes to consult, like .multi.surbl.org.\n"
"# The domain name found in a URI is checked against these DNS black lists.\n"
"# Aggregate lists are supported using suffix/mask. Without a /mask, suffix\n"
"# is the same as suffix/0x00FFFFFE.\n"
"#"
;
static Option opt_uri_bl	= { "uri-bl",		"",	usage_uri_bl };

static const char usage_uri_a_bl[] =
  "A list of IP black list suffixes to consult, like zen.spamhaus.org.\n"
"# The host or domain name found in a URI is used to find its DNS A record\n"
"# and IP address, which is then checked against these IP DNS black lists.\n"
"# Aggregate lists are supported using suffix/mask. Without a /mask, suffix\n"
"# is the same as suffix/0x00FFFFFE.\n"
"#"
;
static Option opt_uri_a_bl	= { "uri-a-bl",		"",	usage_uri_a_bl };

static const char usage_uri_ns_bl[] =
  "A list of host name and/or domain name black list suffixes to consult. The\n"
"# domain name found in a URI is used to find its DNS NS records; the NS host\n"
"# names are checked against these host name and/or domain name DNS black\n"
"# lists. Aggregate lists are supported using suffix/mask. Without a /mask,\n"
"# suffix is the same as suffix/0x00FFFFFE.\n"
"#"
;
static Option opt_uri_ns_bl	= { "uri-ns-bl",	"",	usage_uri_ns_bl };

static const char usage_uri_ns_a_bl[] =
  "A comma or semi-colon separated list of IP black list suffixes to consult.\n"
"# The host or domain name found in a URI is used to find its DNS NS records\n"
"# and IP address, which are then checked against these IP black lists.\n"
"# Aggregate lists are supported using suffix/mask. Without a /mask, suffix\n"
"# is the same as suffix/0x00FFFFFE.\n"
"#"
;
static Option opt_uri_ns_a_bl	= { "uri-ns-a-bl",	"",	usage_uri_ns_a_bl };

static const char usage_uri_bl_headers[] =
  "A list of mail headers to parse for URI and check using the uri-bl,\n"
"# uri-a-bl, and uri-ns-bl options. Specify the empty list to disable.\n"
"#"
;
static Option opt_uri_bl_headers = { "uri-bl-headers",	"X-Originating-IP",	usage_uri_bl_headers };

static int debug;
static int check_soa;
static int exit_code;
static int check_link;
static int check_query;
static int check_subdomains;

static long *uri_ports;

static DnsList *d_bl_list;
static DnsList *mail_bl_list;
static DnsList *uri_bl_list;
static DnsList *uri_a_bl_list;
static DnsList *uri_ns_bl_list;
static DnsList *uri_ns_a_bl_list;

static Vector mail_bl_domains;
static Vector mail_bl_headers;
static Vector uri_bl_headers;

#define LOG_LINE		__FILE__, __LINE__

static const char log_init[] = "initialisation error %s.%d: %s (%d)";
static const char log_oom[] = "out of memory %s.%d";
static const char log_internal[] = "internal error %s.%d";
static const char log_buffer[] = "buffer overflow %s.%d";
static const char log_err[] = "error %s.%d: %s (%d)";

#if ! defined(__MINGW32__)
#undef syslog
void
syslog(int level, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	if (logFile == NULL) {
		vsyslog(level, fmt, args);
	} else {
		fflush(stdout);
		LogV(level, fmt, args);
		fflush(stderr);
	}
	va_end(args);
}
#endif

void
printVersion(void)
{
	printf("%s, a LibSnert tool\n", _NAME);
	printf(LIBSNERT_STRING " " LIBSNERT_COPYRIGHT "\n");
#ifdef LIBSNERT_BUILT
	printf("Built on " LIBSNERT_BUILT "\n");
#endif
}

void printVar(int columns, const char *name, const char *value);

void
printInfo(void)
{
	printVar(0, "NAME", _NAME);
#ifdef LIBSNERT_VERSION
	printVar(0, "LIBSNERT_VERSION", LIBSNERT_VERSION);
#endif
#ifdef LIBSNERT_CONFIGURE
	printVar(LINE_WRAP, "LIBSNERT_CONFIGURE", LIBSNERT_CONFIGURE);
#endif
#ifdef LIBSNERT_BUILT
	printVar(LINE_WRAP, "LIBSNERT_BUILT", LIBSNERT_BUILT);
#endif
#ifdef LIBSNERT_CFLAGS
	printVar(LINE_WRAP, "CFLAGS", CFLAGS_PTHREAD " " LIBSNERT_CFLAGS);
#endif
#ifdef LIBSNERT_LDFLAGS
	printVar(LINE_WRAP, "LDFLAGS", LDFLAGS_PTHREAD " " LIBSNERT_LDFLAGS);
#endif
#ifdef LIBSNERT_LIBS
	printVar(LINE_WRAP, "LIBS", LIBSNERT_LIBS " " HAVE_LIB_PTHREAD);
#endif
}

void
write_result(URI *uri, UriWorker *uw, const char *list_name, const char *fmt, ...)
{
	va_list args;

	if (uw->cgi_mode) {
		if (list_name == NULL) {
			cgiMapAdd(&uw->cgi.headers, "URI-Found", "%u %s", uw->source.line, uri->uri);
		} else {
			uw->source.hits++;
			cgiMapAdd(&uw->cgi.headers, "URI-Found", "%u %s ; %s", uw->source.line, uri->uri, list_name);
		}
	} else if (fmt != NULL && list_name != NULL) {
		fprintf(uw->out, "%s %u: ", uw->source.file, uw->source.line);
		uw->source.hits++;

		va_start(args, fmt);
		vfprintf(uw->out, fmt, args);
		va_end(args);
	}

	exit_code = EXIT_FAILURE;
}

void
test_uri(URI *uri, UriWorker *uw)
{
	unsigned hits;
	PDQ_valid_soa code;
	const char *list_name = NULL;
	const char **seen;
	char *copy;

	hits = uw->source.hits;

	for (seen = (const char **) VectorBase(uw->uri_names_seen); *seen != NULL; seen++) {
		if (TextInsensitiveCompare(uri->host, *seen) == 0)
			return;
	}

	uw->source.found++;

	if (VectorAdd(uw->uri_names_seen, copy = strdup(uri->host)))
		free(copy);

	if (uw->source.hits < uw->max_hits
	&& (list_name = dnsListQueryName(d_bl_list, uw->pdq, NULL, uri->host)) != NULL)
		write_result(uri, uw, list_name, "%s domain blacklisted %s\r\n", uri->host, list_name);

	if (uw->source.hits < uw->max_hits
	&& (list_name = dnsListQueryDomain(uri_bl_list, uw->pdq, NULL, check_subdomains, uri->host)) != NULL)
		write_result(uri, uw, list_name, "%s domain blacklisted %s\r\n", uri->host, list_name);

	if (uw->source.hits < uw->max_hits
	&& (list_name = dnsListQueryNs(uri_ns_bl_list, uri_ns_a_bl_list, uw->pdq, NULL, uri->host)) != NULL)
		write_result(uri, uw, list_name, "%s NS blacklisted %s\r\n", uri->host, list_name);

	if (uw->source.hits < uw->max_hits
	&& (list_name = dnsListQueryIP(uri_a_bl_list, uw->pdq, NULL, uri->host)) != NULL)
		write_result(uri, uw, list_name, "%s IP blacklisted %s\r\n", uri->host, list_name);

	if (uriGetSchemePort(uri) == SMTP_PORT && uw->source.hits < uw->max_hits
	&& (list_name = dnsListQueryMail(mail_bl_list, uw->pdq, mail_bl_domains, uw->mail_names_seen, uri->uriDecoded)) != NULL)
		write_result(uri, uw, list_name, "%s mail blacklisted %s\r\n", uri->uriDecoded, list_name);

	if (check_soa && (code = pdqTestSOA(uw->pdq, PDQ_CLASS_IN, uri->host, NULL)) != PDQ_SOA_OK) {
		fprintf(uw->out, "%s %u: ", uw->source.file, uw->source.line);
		fprintf(uw->out, "%s bad SOA %s (%d)\r\n", uri->host, pdqSoaName(code), code);
		exit_code = EXIT_FAILURE;
	}

	if (hits == uw->source.hits)
		write_result(uri, uw, NULL, NULL);
}

void
process(URI *uri, UriWorker *uw)
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

	if (uw->print_uri_parse) {
		fprintf(uw->out, "%s %u:\r\n", uw->source.file, uw->source.line);
		fprintf(uw->out, "\turi=%s\r\n", uri->uri);
		fprintf(uw->out, "\turiDecoded=%s\r\n", TextNull(uri->uriDecoded));
		fprintf(uw->out, "\tscheme=%s\r\n", TextNull(uri->scheme));
		fprintf(uw->out, "\tschemeInfo=%s\r\n", TextNull(uri->schemeInfo));
		fprintf(uw->out, "\tuserInfo=%s\r\n", TextNull(uri->userInfo));
		fprintf(uw->out, "\thost=%s\r\n", TextNull(uri->host));
		fprintf(uw->out, "\tport=%d\r\n", uriGetSchemePort(uri));
		fprintf(uw->out, "\tpath=%s\r\n", TextNull(uri->path));
		fprintf(uw->out, "\tquery=%s\r\n", TextNull(uri->query));
		fprintf(uw->out, "\tfragment=%s\r\n", TextNull(uri->fragment));
	} else if (uri_ports != NULL) {
		fputs(uri->uriDecoded, uw->out);
		fputc('\n', uw->out);
	}

	if (uri->host != NULL)
		test_uri(uri, uw);

	if (check_link && uri->host != NULL) {
		error = uriHttpOrigin(uri->uriDecoded, &origin);
		fprintf(uw->out, "%s %u: ", uw->source.file, uw->source.line);
		fprintf(uw->out, "\t%s -> ", uri->uriDecoded);
		fprintf(uw->out, "%s\r\n", error == NULL ? origin->uri : error);
		if (error == uriErrorLoop)
			exit_code = EXIT_FAILURE;
	}

	if (origin != NULL && origin->host != NULL && strcmp(uri->host, origin->host) != 0) {
		test_uri(origin, uw);
		free(origin);
	}
}

void
process_list(const char *list, const char *delim, UriWorker *uw)
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
		process(uri, uw);
		free(uri);
	}

	VectorDestroy(args);
}

void
process_query(URI *uri,  UriWorker *uw)
{
	if (uri->query == NULL) {
		process_list(uri->path, "&", uw);
	} else {
		process_list(uri->query, "&", uw);
		process_list(uri->query, "/", uw);
	}
	process_list(uri->path, "/", uw);
}

void
process_uri(URI *uri, void *data)
{
	process(uri, data);
	if (check_query)
		process_query(uri, data);
}

int
process_input(Mime *m, FILE *fp, UriWorker *uw)
{
	int ch;

	if (fp != NULL) {
		mimeReset(m);
		uw->source.line = 1;
		uw->source.hits = 0;
		uw->source.found = 0;

		if (debug)
			syslog(LOG_DEBUG, "file=%s line=%u", uw->source.file, uw->source.line);
		do {
			ch = fgetc(fp);
			(void) mimeNextCh(m, ch);
			if (ch == '\n') {
				uw->source.line++;
				if (debug)
					syslog(LOG_DEBUG, "file=%s line=%u", uw->source.file, uw->source.line);
			}
		} while (ch != EOF);

		(void) fflush(uw->out);
	}

	return 0;
}

int
process_file(UriWorker *uw)
{
	int rc;
	FILE *fp;
	Mime *mime;
	UriMime *hold;

	rc = -1;

	/* Check for standard input. */
	if (uw->source.file[0] == '-' && uw->source.file[1] == '\0')
		fp = stdin;

	/* Otherwise open the file. */
	else if ((fp = fopen(uw->source.file, "r")) == NULL)
		goto error0;

	if ((mime = mimeCreate()) == NULL)
		goto error1;

	uw->source.line = 1;
	uw->source.hits = 0;
	uw->source.found = 0;

	if ((hold = uriMimeInit(process_uri, uw->headers_and_body, uw)) == NULL)
		goto error2;

	mimeHeadersFirst(mime, uw->headers_and_body);
	mimeHooksAdd(mime, (MimeHooks *)hold);
	rc = process_input(mime, fp, uw);

	if (uw->cgi_mode)
		cgiMapAdd(&uw->cgi.headers, "Blacklist-Hits", "%u", uw->source.hits);
error2:
	mimeFree(mime);
error1:
	fclose(fp);
error0:
	return rc;
}

void
process_string(UriWorker *uw)
{
	char *s;
	Mime *mime;
	UriMime *hold;

	if ((mime = mimeCreate()) == NULL)
		return;

	uw->source.line = 1;
	uw->source.hits = 0;
	uw->source.found = 0;

	s = (char *) BufBytes(uw->cgi._RAW) + BufOffset(uw->cgi._RAW);

	if ((hold = uriMimeInit(process_uri, uw->headers_and_body, uw)) != NULL) {
		mimeHeadersFirst(mime, !uw->headers_and_body);
		mimeHooksAdd(mime, (MimeHooks *)hold);
		for ( ; *s != '\0'; s++) {
			(void) mimeNextCh(mime, *s);
			if (*s == '\n')
				uw->source.line++;
		}
		(void) mimeNextCh(mime, EOF);
		(void) fflush(uw->out);
	}

	mimeFree(mime);

	if (uw->cgi_mode)
		cgiMapAdd(&uw->cgi.headers, "Blacklist-Hits", "%u", uw->source.hits);
}

static Option *opt_table[];

void
at_exit_cleanup(void)
{
	optionFreeL(opt_table, NULL);

	VectorDestroy(mail_bl_headers);
	VectorDestroy(mail_bl_domains);
	VectorDestroy(uri_bl_headers);

	dnsListFree(mail_bl_list);
	dnsListFree(uri_ns_a_bl_list);
	dnsListFree(uri_ns_bl_list);
	dnsListFree(uri_a_bl_list);
	dnsListFree(uri_bl_list);
	dnsListFree(d_bl_list);

	pdqFini();
}
#endif /* defined(TEST) || defined(DAEMON) */

/***********************************************************************
 *** CLI / CGI code.
 ***********************************************************************/
#if defined(TEST)

#include <com/snert/lib/util/getopt.h>

static Vector print_uri_ports;
static const char *name_servers;

static Option *opt_table[] = {
	&opt_title,
	&opt_syntax,
	PDQ_OPTIONS_TABLE,
	&opt_help,
	&opt_info,
	&opt_domain_bl,
	&opt_mail_bl,
	&opt_mail_bl_domains,
	&opt_mail_bl_headers,
	&opt_uri_bl,
	&opt_uri_a_bl,
	&opt_uri_ns_bl,
	&opt_uri_ns_a_bl,
	&opt_uri_bl_headers,
	&opt_version,
	NULL
};

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

void
enableDebug(void)
{
	LogOpen("(standard error)");
	LogSetProgramName("uri");
	dnsListSetDebug(1);
	socketSetDebug(1);
	uriSetDebug(4);
	pdqSetDebug(1);
	debug++;
}

void
printVar(int columns, const char *name, const char *value)
{
	int length;
	Vector list;
	const char **args;

	if (columns <= 0)
		printf("%s=\"%s\"\n",  name, value);
	else if ((list = TextSplit(value, " \t", 0)) != NULL && 0 < VectorLength(list)) {
		args = (const char **) VectorBase(list);

		length = printf("%s=\"'%s'", name, *args);
		for (args++; *args != NULL; args++) {
			/* Line wrap. */
			if (columns <= length + strlen(*args) + 4) {
				(void) printf("\n\t");
				length = 8;
			}
			length += printf(" '%s'", *args);
		}
		if (columns <= length + 1) {
			(void) printf("\n");
		}
		(void) printf("\"\n");

		VectorDestroy(list);
	}
}

int
main(int argc, char **argv)
{
	int i, ch;
	UriWorker uw;

	memset(&uw, 0, sizeof (uw));

	if (atexit(at_exit_cleanup)) {
		fprintf(stderr, log_init, LOG_LINE, strerror(errno), errno);
		exit(EX_SOFTWARE);
	}

	optionInit(opt_table, NULL);

	if (getenv("GATEWAY_INTERFACE") == NULL) {
//		optind = optionArrayL(argc, argv, opt_table, NULL);

		while ((ch = getopt(argc, argv, "fabA:d:m:M:i:n:N:u:UlLmpP:qQ:RsT:t:v")) != -1) {
			switch (ch) {
			case 'f':
				/* Place holder for old -f option for AlexB. */
				break;
			case 'a':
				uw.headers_and_body = 1;
				break;
			case 'b':
				uw.headers_and_body = 0;
				break;
			case 'A':
				at_sign_delim = *optarg;
				break;
			case 'd':
				opt_domain_bl.string = optarg;
				break;
			/* case 'D': reserved for possible future domain exception list. */

			case 'i':
				opt_uri_a_bl.string = optarg;
				break;
			case 'n':
				opt_uri_ns_bl.string = optarg;
				break;
			case 'N':
				opt_uri_ns_a_bl.string = optarg;
				break;
			case 'u':
				opt_uri_bl.string = optarg;
				break;
			case 'm':
				opt_mail_bl.string = optarg;
				break;
			case 'M':
				opt_mail_bl_domains.string = optarg;
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
				uw.print_uri_parse = 1;
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
				enableDebug();
				break;
			default:
				(void) fputs(usage, stderr);
				return EX_USAGE;
			}
		}

		uw.max_hits = LONG_MAX;
	} else {
		struct stat sb;

		uw.cgi_mode = 1;
		if (cgiInit(&uw.cgi))
			exit(EX_SOFTWARE);

		/* Check DOCUMENT_ROOT for .cf file. */
		sb.st_size = 0;
		if (stat(CF_FILE, &sb) && uw.cgi.script_filename != NULL) {
			int length;
			char *path;

			path = strdup(uw.cgi.script_filename);
			/* Find previous slash. */
			length = strlrcspn(path, strlen(path), "/");
			/* Remove basename. */
			path[length] = '\0';

			/* Check dirname(SCRIPT_FILENAME) for .cf. */
			(void) chdir(path);
			(void) stat(CF_FILE, &sb);

			free(path);
		}

		/* Parse an option file followed by the header options. */
		if (0 < sb.st_size)
			(void) optionFile(CF_FILE, opt_table, NULL);
		(void) optionArrayL(argc, argv, opt_table, NULL);
		cgiSetOptions(&uw.cgi, uw.cgi._HTTP, opt_table);

		if (0 <= cgiMapFind(uw.cgi._GET, "q"))
			check_query = 1;
	}

	if (opt_info.string != NULL) {
		printInfo();
		exit(EX_USAGE);
	}
	if (opt_version.string != NULL) {
		printVersion();
		exit(EX_USAGE);
	}
	if (opt_help.string != NULL) {
		/* help=filepath (compatibility with Windows)
		 * equivalent to +help >filepath
		 */
		if (opt_help.string[0] != '-' && opt_help.string[0] != '+')
			(void) freopen(opt_help.string, "w", stdout);
		optionUsageL(opt_table, NULL);
		exit(EX_USAGE);
	}

	if (pdqInit()) {
		fprintf(stderr, log_init, LOG_LINE, strerror(errno), errno);
		exit(EX_SOFTWARE);
	}

	PDQ_OPTIONS_SETTING(debug);

	if (name_servers != NULL) {
		Vector servers = TextSplit(name_servers, ",", 0);
		if (pdqSetServers(servers)) {
			fprintf(stderr, log_init, LOG_LINE, strerror(errno), errno);
			exit(EX_SOFTWARE);
		}
		VectorDestroy(servers);
	}

	if ((uw.pdq = pdqOpen()) == NULL) {
		fprintf(stderr, log_init, LOG_LINE, strerror(errno), errno);
		exit(EX_SOFTWARE);
	}

	uw.out = stdout;
	uw.uri_names_seen = VectorCreate(10);
	VectorSetDestroyEntry(uw.uri_names_seen, free);
	uw.mail_names_seen = VectorCreate(10);
	VectorSetDestroyEntry(uw.mail_names_seen, free);

	d_bl_list = dnsListCreate(opt_domain_bl.string);

	uri_ns_bl_list = dnsListCreate(opt_uri_ns_bl.string);
	uri_ns_a_bl_list = dnsListCreate(opt_uri_ns_a_bl.string);
	uri_bl_list = dnsListCreate(opt_uri_bl.string);
	uri_a_bl_list = dnsListCreate(opt_uri_a_bl.string);
	uri_bl_headers = TextSplit(opt_uri_bl_headers.string, ";, ", 0);

	mail_bl_list = dnsListCreate(opt_mail_bl.string);
	mail_bl_domains = TextSplit(opt_mail_bl_domains.string, ";, ", 0);
	mail_bl_headers = TextSplit(opt_mail_bl_headers.string, ";, ", 0);

	if (0 < VectorLength(print_uri_ports)) {
		uri_ports = malloc(sizeof (long) * (VectorLength(print_uri_ports) + 1));

		for (i = 0; i < VectorLength(print_uri_ports); i++) {
			char *port = VectorGet(print_uri_ports, i);
			uri_ports[i] = strtol(port, NULL, 10);
		}
		uri_ports[i] = -1;
	}

	exit_code = EXIT_SUCCESS;

	if (uw.cgi_mode) {
		int fi, pi;

		uw.max_hits = 1;
		if (0 <= (i = cgiMapFind(uw.cgi._GET, "x")))
			uw.max_hits = strtol(uw.cgi._GET[i].value, NULL, 10);

		uw.headers_and_body = 0;
		if (0 <= (i = cgiMapFind(uw.cgi._GET, "a")) && uw.cgi._GET[i].value[0] != '0')
			uw.headers_and_body = 1;

		if (0 <= (fi = cgiMapFind(uw.cgi._GET, "f"))) {
			uw.source.file = uw.cgi._GET[fi].value;
			(void) process_file(&uw);
		} else {
			uw.source.file = uw.cgi.remote_addr;
			process_string(&uw);
		}

		if (uw.cgi.request_method[0] != 'H'
		&& 0 <= (pi = cgiMapFind(uw.cgi._GET, "p")) && uw.cgi._GET[pi].value[0] != '0') {
			cgiMapAdd(&uw.cgi.headers, "Content-Type", "%s", "text/plain");
			cgiSendOk(&uw.cgi, NULL);

			if (0 <= (i = cgiMapFind(uw.cgi._GET, "v")) && uw.cgi._GET[i].value[0] != '0')
				enableDebug();

			/* Redo parse and dumping found URI. */
			uw.cgi_mode = 0;
			uw.print_uri_parse = 1;
			if (uw.cgi._GET[pi].value[0] == '2') {
				VectorRemoveAll(uw.uri_names_seen);
				VectorRemoveAll(uw.mail_names_seen);
			}
			if (0 <= fi)
				(void) process_file(&uw);
			else
				process_string(&uw);
			uw.cgi_mode = 1;
		} else if (uw.cgi.headers == NULL) {
			cgiSendNotFound(&uw.cgi, NULL);
		} else {
			cgiSendNoContent(&uw.cgi);
		}

		exit_code = EXIT_SUCCESS;
		cgiFree(&uw.cgi);
	} else if (argc <= optind) {
		uw.source.file = "-";
		(void) process_file(&uw);
	} else {
		for (i = optind; i < argc; i++) {
			struct stat sb;
			if ((argv[i][0] == '-' && argv[i][1] == '\0') || stat(argv[i], &sb) == 0) {
				uw.source.file = argv[i];
				(void) process_file(&uw);
			} else {
				uw.cgi._RAW = BufCopyString(argv[i]);
				uw.source.file = "(arg)";
				uw.headers_and_body = 1;
				process_string(&uw);
				BufDestroy(uw.cgi._RAW);
				uw.cgi._RAW  = NULL;
			}
		}
	}

	VectorDestroy(uw.mail_names_seen);
	VectorDestroy(uw.uri_names_seen);
	pdqClose(uw.pdq);

	return exit_code;
}
#endif

/***********************************************************************
 *** Daemon
 ***********************************************************************/
#if defined(DAEMON)

#ifdef HAVE_SYS_RESOURCE_H
# include <sys/resource.h>
#endif

extern void rlimits(void);
#include <com/snert/lib/sys/pid.h>
#include <com/snert/lib/sys/process.h>
#include <com/snert/lib/sys/pthread.h>
#include <com/snert/lib/net/server.h>

#ifndef RUN_AS_USER
#define RUN_AS_USER			"www"
#endif

#ifndef RUN_AS_GROUP
#define RUN_AS_GROUP			"www"
#endif

#ifndef SERVER_ACCEPT_TIMEOUT
#define SERVER_ACCEPT_TIMEOUT		10000
#endif

#ifndef SERVER_READ_TIMEOUT
#define SERVER_READ_TIMEOUT		30000
#endif

#ifndef SERVER_PORT
#define SERVER_PORT			8088
#endif

#ifndef SERVER_DIR
#define SERVER_DIR			"/var/empty"
#endif

#ifndef INTERFACES
#define INTERFACES			"[::]:" QUOTE(SERVER_PORT) ";0.0.0.0:" QUOTE(SERVER_PORT)
#endif

#ifndef THREAD_STACK_SIZE
# define THREAD_STACK_SIZE		(32 * 1024)
# if THREAD_STACK_SIZE < PTHREAD_STACK_MIN
#  undef THREAD_STACK_SIZE
#  define THREAD_STACK_SIZE		PTHREAD_STACK_MIN
# endif
#endif

#ifndef MIN_RAW_SIZE
#define MIN_RAW_SIZE			(64 * 1024)
#endif

#define CRLF		"\r\n"
static const char empty[] = "";

static const char usage_verbose[] =
  "What to write to mail log. Specify a white space separated list of words:"
;
Option opt_verbose	= { "verbose",	"+info", usage_verbose };

/* Verbose levels */
Option verb_info	= { "info",		"-", empty };
Option verb_trace	= { "trace",		"-", empty };
Option verb_debug	= { "debug",		"-", empty };

Option verb_dns		= { "dns",		"-", empty };
Option verb_http	= { "http",		"-", empty };
Option verb_server	= { "server",		"-", empty };
Option verb_socket	= { "socket",		"-", empty };
Option verb_uri		= { "uri",		"-", empty };

Option *verb_table[] = {
	&verb_info,
	&verb_trace,
	&verb_debug,

	&verb_dns,
	&verb_http,
	&verb_server,
	&verb_socket,
	&verb_uri,

	NULL
};

static const char usage_interfaces[] =
  "A semi-colon separared list of interface host names or IP addresses\n"
"# on which to bind and listen for new connections. They can be IPv4\n"
"# and/or IPv6."
"#"
;
Option opt_interfaces 		= { "interfaces", 		INTERFACES,	usage_interfaces };

static const char usage_server_max_threads[] =
  "Maximum number of server threads possible to handle new requests.\n"
"# Specify zero to allow upto the system thread limit.\n"
"#"
;

static const char usage_server_min_threads[] =
  "Minimum number of server threads to keep alive to handle new requests.\n"
"#"
;

static const char usage_server_new_threads[] =
  "Number of new server threads to create when all the existing threads\n"
"# are in use.\n"
"#"
;

static const char usage_server_queue_size[] =
  "Server connection queue size. This setting is OS specific and tells\n"
"# the kernel how many unanswered connections it should queue before\n"
"# refusing connections.\n"
"#"
;

static const char usage_test_mode[] =
  "Used for testing. Run the server in single thread mode and accept\n"
"# client connections sequentionally ie. no concurrency possible.\n"
"#"
;

static Option opt_file			= { "file", 			CF_FILE, 	"Read option file before command line options." };

static Option opt_daemon		= { "daemon",			"+",		"Start as a background daemon or foreground application." };

static Option opt_quit			= { "quit", 			NULL,		"Quit an already running instance and exit." };
static Option opt_restart		= { "restart", 			NULL,		"Terminate an already running instance before starting." };
static Option opt_restart_if		= { "restart-if", 		NULL,		"Only restart when there is a previous instance running." };
static Option opt_service		= { "service",			NULL,		"Add or remove Windows service." };

static Option opt_server_accept_timeout	= { "server-accept-timeout",	QUOTE(SERVER_ACCEPT_TIMEOUT),	"Time in milliseconds a server thread waits for a new connection." };
static Option opt_server_max_threads	= { "server-max-threads",	"0",		usage_server_max_threads };
static Option opt_server_min_threads	= { "server-min-threads",	"10",		usage_server_min_threads };
static Option opt_server_new_threads	= { "server-new-threads",	"10",		usage_server_new_threads };
static Option opt_server_read_timeout	= { "server-read-timeout",	QUOTE(SERVER_READ_TIMEOUT),	"Time in milliseconds the server waits for some input from the client." };
static Option opt_server_queue_size	= { "server-queue-size",	"10",		usage_server_queue_size };

static Option opt_run_group		= { "run-group",		RUN_AS_GROUP,	"Run as this Unix group." };
static Option opt_run_jailed		= { "run-jailed",		"-",		"Run in a chroot jail; run-work-dir used as the new root directory." };
static Option opt_run_open_file_limit	= { "run-open-file-limit",	"1024",		"The maximum open file limit for the process." };
static Option opt_run_pid_file 		= { "run-pid-file", 		"/var/run/" _NAME ".pid",	"The file path of where to save the process-id." };
static Option opt_run_user		= { "run-user",			RUN_AS_USER,	"Run as this Unix user." };
static Option opt_run_work_dir 		= { "run-work-dir", 		SERVER_DIR, 	"The working directory (aka server root) of the process." };

static Option opt_test_mode		= { "test-mode",		"-",		usage_test_mode };

static Option *opt_table[] = {
	&opt_title,
	&opt_syntax,

	&opt_daemon,
	PDQ_OPTIONS_TABLE,
	&opt_domain_bl,
	&opt_file,
	&opt_help,
	&opt_info,
	&opt_interfaces,
	&opt_mail_bl,
	&opt_mail_bl_domains,
	&opt_mail_bl_headers,
	&opt_quit,
	&opt_restart,
	&opt_restart_if,
	&opt_run_group,
#if defined(HAVE_CHROOT)
	&opt_run_jailed,
#endif
#if defined(RLIMIT_NOFILE)
	&opt_run_open_file_limit,
#endif
	&opt_run_pid_file,
	&opt_run_user,
	&opt_run_work_dir,
	&opt_server_accept_timeout,
	&opt_server_max_threads,
	&opt_server_min_threads,
	&opt_server_new_threads,
	&opt_server_queue_size,
	&opt_server_read_timeout,
	&opt_service,
	&opt_test_mode,
	&opt_uri_bl,
	&opt_uri_a_bl,
	&opt_uri_ns_bl,
	&opt_uri_ns_a_bl,
	&opt_uri_bl_headers,
	&opt_verbose,
	&opt_version,

	NULL
};

typedef struct {
	size_t size;
	long length;
	long offset;
	char *data;
} Buffer;

Server server;
ServerSignals signals;

static void
verboseFill(const char *prefix, Buffer *buf)
{
	Option **opt, *o;
	long cols, length;

	if (0 < buf->length)
		buf->length += TextCopy(buf->data+buf->length, buf->size-buf->length, CRLF);
	buf->length += TextCopy(buf->data+buf->length, buf->size-buf->length, prefix);

	cols = 0;
	for (opt = verb_table; *opt != NULL; opt++) {
		o = *opt;

		if (LINE_WRAP <= cols % LINE_WRAP + strlen(o->name) + 2) {
			buf->length += TextCopy(buf->data+buf->length, buf->size-buf->length, CRLF);
			buf->length += TextCopy(buf->data+buf->length, buf->size-buf->length, prefix);
			cols = 0;
		}

		length = snprintf(
			buf->data+buf->length,
			buf->size-buf->length,
			" %c%s", o->value ? '+' : '-', o->name
		);

		buf->length += length;
		cols += length;
	}

	buf->length += TextCopy(buf->data+buf->length, buf->size-buf->length, CRLF);
}

static void
verboseInit(void)
{
	static char buffer[2048];
	static Buffer usage = { sizeof (buffer), 0, 0, buffer };

	opt_verbose.usage = buffer;
	usage.length = TextCopy(usage.data, usage.size, usage_verbose);
	verboseFill("#", &usage);
	usage.length += TextCopy(usage.data+usage.length, usage.size-usage.length, "#");

//	optionInitOption(&opt_verbose);
//	optionString(opt_verbose.string, verb_table, NULL);
}

static void
dns_list_dump_stats(DnsList *dns_list, FILE *out)
{
	int i;
	const char **suffixes;

	if (dns_list != NULL) {
		suffixes = (const char **) VectorBase(dns_list->suffixes);
		for (i = 0; suffixes[i] != NULL; i++) {
			(void) fprintf(
				out, "%05lu\t%s\r\n", dns_list->hits[i],
				suffixes[i] + (*suffixes[i] == '.')
			);
		}
	}
}

static void
dump_bl_stats(UriWorker *uw)
{
	dns_list_dump_stats(d_bl_list, uw->out);
	dns_list_dump_stats(mail_bl_list, uw->out);
	dns_list_dump_stats(uri_bl_list, uw->out);
	dns_list_dump_stats(uri_a_bl_list, uw->out);
	dns_list_dump_stats(uri_ns_bl_list, uw->out);
	dns_list_dump_stats(uri_ns_a_bl_list, uw->out);
}

int
worker_free(ServerWorker *worker)
{
	UriWorker *uw;

	if (worker != NULL && worker->data != NULL) {
		uw = worker->data;

		VectorDestroy(uw->mail_names_seen);
		VectorDestroy(uw->uri_names_seen);
		pdqClose(uw->pdq);
		free(uw);
	}

	return 0;
}

int
worker_create(ServerWorker *worker)
{
	UriWorker *uw;

	if ((uw = malloc(sizeof (*uw))) == NULL)
		goto error0;

	if ((uw->pdq = pdqOpen()) == NULL)
		goto error1;

	if ((uw->uri_names_seen = VectorCreate(10)) == NULL)
		goto error2;
	VectorSetDestroyEntry(uw->uri_names_seen, free);

	if ((uw->mail_names_seen = VectorCreate(10)) == NULL)
		goto error3;
	VectorSetDestroyEntry(uw->mail_names_seen, free);

	uw->source.file = NULL;
	uw->source.line = 1;
	uw->source.hits = 0;

	worker->data = uw;

	return 0;

	VectorDestroy(uw->mail_names_seen);
error3:
	VectorDestroy(uw->uri_names_seen);
error2:
	pdqClose(uw->pdq);
error1:
	free(uw);
error0:
	return -1;
}

int
session_accept(ServerSession *session)
{
	(void) socketSetNonBlocking(session->client, 1);
	socketSetTimeout(session->client, opt_server_read_timeout.value);

	return 0;
}

int
session_process(ServerSession *session)
{
	int i, fi, pi, rc = -1;
	UriWorker *uw = session->worker->data;

	if (cgiRawInit(&uw->cgi, session->client, 1))
		goto error1;

	uw->cgi_mode = 1;
	uw->print_uri_parse = 0;
	uw->out = uw->cgi.out;

	if (0 <= TextSensitiveStartsWith(uw->cgi.request_uri, "/uri/stat")) {
		cgiSendOk(&uw->cgi, empty);
		dump_bl_stats(uw);
		rc = 0;
		goto error1;
	}

	if (TextSensitiveStartsWith(uw->cgi.request_uri, "/uri/") < 0
	&&  TextSensitiveStartsWith(uw->cgi.request_uri, "/weed/") < 0) {
		cgiSendNotFound(&uw->cgi, NULL);
		goto error1;
	}

	/* Reset the session data. */
	VectorRemoveAll(uw->mail_names_seen);
	VectorRemoveAll(uw->uri_names_seen);
	pdqQueryRemoveAll(uw->pdq);

	uw->max_hits = 1;
	if (0 <= (i = cgiMapFind(uw->cgi._GET, "x")))
		uw->max_hits = strtol(uw->cgi._GET[i].value, NULL, 10);

	uw->headers_and_body = 0;
	if (0 <= (i = cgiMapFind(uw->cgi._GET, "a")) && uw->cgi._GET[i].value[0] != '0')
		uw->headers_and_body = 1;

	if (0 <= (fi = cgiMapFind(uw->cgi._GET, "f"))) {
		uw->source.file = uw->cgi._GET[fi].value;
		(void) process_file(uw);
	} else {
		uw->source.file = session->id_log;
		process_string(uw);
	}

	if (uw->cgi.request_method[0] != 'H'
	&& 0 <= (pi = cgiMapFind(uw->cgi._GET, "p")) && uw->cgi._GET[pi].value[0] != '0') {
		cgiMapAdd(&uw->cgi.headers, "Content-Type", "%s", "text/plain");
		cgiSendOk(&uw->cgi, NULL);

		/* Redo parse and dumping found URI. */
		uw->cgi_mode = 0;
		uw->print_uri_parse = 1;
		if (uw->cgi._GET[pi].value[0] == '2') {
			VectorRemoveAll(uw->uri_names_seen);
			VectorRemoveAll(uw->mail_names_seen);
		}
		if (0 <= fi)
			(void) process_file(uw);
		else
			process_string(uw);
		uw->cgi_mode = 1;
	} else if (uw->cgi.headers == NULL) {
		cgiSendNotFound(&uw->cgi, NULL);
	} else {
		cgiSendNoContent(&uw->cgi);
	}
	rc = 0;
error1:
	/* Always log the request. */
	syslog(
		LOG_INFO, "%s %s \"%s %s %s\" %d %u/%u",
		session->id_log, session->address,
		uw->cgi.request_method, uw->cgi.request_uri,
		uw->cgi.server_protocol, uw->cgi.status,
		uw->source.hits, uw->source.found
	);

	cgiFree(&uw->cgi);

	return rc;
}

void
serverOptions(int argc, char **argv)
{
	int argi;

	/* Parse command line options looking for a file= option. */
	optionInit(opt_table, NULL);
	argi = optionArrayL(argc, argv, opt_table, NULL);

	/* Parse the option file followed by the command line options again. */
	if (opt_file.string != NULL && *opt_file.string != '\0') {
		/* Do NOT reset this option. */
		opt_file.initial = opt_file.string;
		opt_file.string = NULL;

		optionInit(opt_table, NULL);
		(void) optionFile(opt_file.string, opt_table, NULL);
		(void) optionArrayL(argc, argv, opt_table, NULL);
	}

	pdqMaxTimeout(optDnsMaxTimeout.value);
	pdqSetRoundRobin(optDnsRoundRobin.value);

	if (opt_server_min_threads.value < 1)
		opt_server_min_threads.value = 1;
	if (opt_server_new_threads.value < 1)
		opt_server_new_threads.value = 1;
	if (opt_server_max_threads.value < 1)
		opt_server_max_threads.value = opt_test_mode.value ? 1 : LONG_MAX;

	optionString(opt_verbose.string, verb_table, NULL);
}

static int
server_init(void)
{
	if (verb_trace.value) {
		syslog(LOG_DEBUG, "process limits now");
		rlimits();
	}
# if defined(RLIMIT_NOFILE)
	/* Compute and/or set the upper limit on the number of
	 * open file descriptors the process can have. See server
	 * accept() loop.
	 */
	if (50 < opt_run_open_file_limit.value) {
		struct rlimit limit;

		if (getrlimit(RLIMIT_NOFILE, &limit) == 0) {
			limit.rlim_cur = (rlim_t) opt_run_open_file_limit.value;
			if (limit.rlim_max < (rlim_t) opt_run_open_file_limit.value)
				limit.rlim_max = limit.rlim_cur;

			(void) setrlimit(RLIMIT_NOFILE, &limit);
		}
	}

	if (verb_trace.value) {
		syslog(LOG_DEBUG, "process limits updated");
		rlimits();
	}
# endif
	if (pdqInit()) {
		syslog(LOG_ERR, log_init, LOG_LINE, strerror(errno), errno);
		return -1;
	}

	PDQ_OPTIONS_SETTING(verb_dns.value);
	dnsListSetDebug(verb_dns.value);
	socketSetDebug(verb_socket.value);
	uriSetDebug(verb_uri.value);

	d_bl_list = dnsListCreate(opt_domain_bl.string);

	uri_ns_bl_list = dnsListCreate(opt_uri_ns_bl.string);
	uri_ns_a_bl_list = dnsListCreate(opt_uri_ns_a_bl.string);
	uri_bl_list = dnsListCreate(opt_uri_bl.string);
	uri_a_bl_list = dnsListCreate(opt_uri_a_bl.string);
	uri_bl_headers = TextSplit(opt_uri_bl_headers.string, ";, ", 0);

	mail_bl_list = dnsListCreate(opt_mail_bl.string);
	mail_bl_domains = TextSplit(opt_mail_bl_domains.string, ";, ", 0);
	mail_bl_headers = TextSplit(opt_mail_bl_headers.string, ";, ", 0);

	server.debug.level = verb_server.value;
	server.option.spare_threads = opt_server_new_threads.value;
	server.option.min_threads = opt_server_min_threads.value;
	server.option.max_threads = opt_server_max_threads.value;
	server.option.queue_size = opt_server_queue_size.value;
	server.option.accept_to = opt_server_accept_timeout.value;
	server.option.read_to = opt_server_read_timeout.value;

	server.hook.worker_create = worker_create;
	server.hook.worker_free = worker_free;

	server.hook.session_create = NULL;
	server.hook.session_accept = session_accept;
	server.hook.session_process = session_process;
	server.hook.session_free = NULL;

	serverSetStackSize(&server, THREAD_STACK_SIZE);

	if (processDropPrivilages(opt_run_user.string, opt_run_group.string, opt_run_work_dir.string, opt_run_jailed.value))
		return -1;
	(void) processDumpCore(1);

	if (verb_trace.value) {
		char path[PATH_MAX];
		(void) getcwd(path, sizeof (path));
		syslog(LOG_INFO, "server cwd=\"%s\"", path);
	}

	return 0;
}

int
serverMain(void)
{
	int rc, signal;

	rc = EXIT_FAILURE;

	syslog(LOG_INFO, _NAME ", a LibSnert tool");
	syslog(LOG_INFO, "LibSnert %s %s", LIBSNERT_VERSION, LIBSNERT_COPYRIGHT);
#ifdef LIBSNERT_BUILT
	syslog(LOG_INFO, "Built on " LIBSNERT_BUILT);
#endif
	if (pthreadInit())
		goto error0;

	if (serverSignalsInit(&signals))
		goto error1;

	if (serverInit(&server, opt_interfaces.string, SERVER_PORT))
		goto error2;

	if (server_init())
		goto error3;

#ifdef VERB_VALGRIND
	if (1 < verb_valgrind.value) {
		VALGRIND_PRINTF("serverMain before serverStart\n");
		VALGRIND_DO_LEAK_CHECK;
	}
#endif
	if (serverStart(&server))
		goto error3;

	syslog(LOG_INFO, "ready");

	signal = serverSignalsLoop(&signals);
	serverStop(&server, signal == SIGQUIT);
	rc = EXIT_SUCCESS;
error3:
	serverFini(&server);
error2:
	serverSignalsFini(&signals);
error1:
	pthreadFini();
error0:
	return rc;
}

int
main(int argc, char **argv)
{
	verboseInit();
	serverOptions(argc, argv);
	if (atexit(at_exit_cleanup))
		exit(EX_SOFTWARE);

	if (opt_version.string != NULL) {
		printVersion();
		exit(EX_USAGE);
	}
	if (opt_info.string != NULL) {
		printInfo();
		exit(EX_USAGE);
	}
	if (opt_help.string != NULL) {
		/* help=filepath (compatibility with Windows)
		 * equivalent to +help >filepath
		 */
		if (opt_help.string[0] != '-' && opt_help.string[0] != '+')
			(void) freopen(opt_help.string, "w", stdout);
		optionUsageL(opt_table, NULL);
		exit(EX_USAGE);
	}
	if (opt_restart.string != NULL || opt_restart_if.string != NULL) {
		if (pidKill(opt_run_pid_file.string, SIGTERM) && opt_restart_if.string != NULL) {
			fprintf(stderr, "no previous instance running\n");
			syslog(LOG_ERR, "no previous instance running");
			exit(1);
		}
		sleep(2);
	}

	if (opt_daemon.value) {
		openlog(_NAME, LOG_PID|LOG_NDELAY, LOG_USER);
		if (daemon(1, 0)) {
			syslog(LOG_ERR, "daemon mode failed");
			exit(EX_SOFTWARE);
		}
	} else {
		LogSetProgramName(_NAME);
		LogOpen("(standard error)");
	}

	return serverMain();
}
#endif /* DAEMON */
