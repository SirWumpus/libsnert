/*
 * uri.c
 *
 * RFC 2821, 2396 Support Routines
 *
 * Copyright 2002, 2006 by Anthony Howe. All rights reserved.
 */

#ifndef HTTP_BUFFER_SIZE
#define HTTP_BUFFER_SIZE		(4 * 1024)
#endif

#ifndef IMPLICIT_DOMAIN_MIN_DOTS
#define IMPLICIT_DOMAIN_MIN_DOTS	2
#endif

#define TEXT_VS_INLINE
#define TEST_MESSAGE_PARTS

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
#include <com/snert/lib/mail/parsePath.h>
#include <com/snert/lib/util/b64.h>
#include <com/snert/lib/util/uri.h>
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

int uriDebug;

static char at_sign_delim = '@';
static char uri_excluded[] = " #<>%\"{}|\\^[]`";
static char uri_reserved[] = ";/?:@&=+$,";
static char uri_unreserved[] = "-_.!~*'()";
static long socket_timeout = SOCKET_CONNECT_TIMEOUT;

/***********************************************************************
 *** URI related support routines.
 ***********************************************************************/

int
isCharURI(int octet)
{
	if (octet <= 32 || 0x7F <= octet)
		return 0;

	/* RFC 2396 2.4.3. Excluded US-ASCII Characters states that
	 * "#" separates the URI from a "fragment" and "%" is used
	 * as the lead-in for escaped characters.
	 *
	 * ASCII space is also excluded, but its already tested for
	 * just above, so skip it here.
	 */
	if (strchr(uri_excluded+1, octet) != NULL)
		return 0;

	return 1;
}

/*
 * RFC 2396
 */
int
spanURI(const char *uri, const char *stop)
{
	int a, b;
	const char *start;

	if (uri == NULL)
		return 0;

	for (start = uri; uri < stop || *uri != '\0'; uri++) {
		/* I include %HH within a URI since you typically
		 * want to extract a URI before you can unescape
		 * any characters.
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

/*
 * RFC 2396
 */
int
spanScheme(const char *scheme)
{
	const char *start;

	if (scheme == NULL)
		return 0;

	for (start = scheme; *scheme != '\0'; scheme++) {
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
int
spanLocalPart(const char *s)
{
	const char *t;
	/*@-type@*/
	char x[2] = { 0, 0 };
	/*@-type@*/

	if (*s == '"') {
		/* Quoted-string = DQUOTE *qcontent DQUOTE */
		for (t = s+1; *t != '\0' && *t != '"'; t++) {
			switch (*t) {
			case '\\':
				if (t[1] != '\0')
					t++;
				break;
			case '\t':
			case '\r':
			case '\n':
			case '#':
				return t - s;
			}
		}

		if (*t == '"')
			t++;

		return t - s;
	}

	/* Dot-string = Atom *("." Atom) */
	for (t = s; *t != '\0'; t++) {
		if (isalnum(*t))
			continue;

		if (*t == '\\' && t[1] != '\0') {
			t++;
			continue;
		}

		/* atext does not include '.', but we do here to
		 * simplify scanning dot-atom.
		 */
		*x = *t;
		if (strspn(x, "!#$%&'*+-/=?^_`{|}~.") != 1)
			break;
	}

	return t - s;
}

/*
 * RFC 2821 domain syntax excluding address-literal.
 *
 * Note that RFC 1035 section 2.3.1 indicates that domain labels
 * should begin with an alpha character and end with an alpha-
 * numeric character. However, all numeric domains do exist, such
 * as 123.com, so are permitted.
 */
int
spanDomain(const char *domain, int minDots)
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
			/* A domain segment must end with an alpha-numeric. */
			if (!isalnum(previous))
				goto stop;

			/* Double dots are illegal. */
			if (domain[1] == '.')
				return 0;

			/* Count only internal dots, not the trailing root dot. */
			if (domain[1] != '\0') {
				label_is_alpha = 1;
				dots++;
			}
			break;
		case '-': case '_':
			/* A domain segment cannot start with a hyphen. */
			if (previous == '.')
				goto stop;
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


int
spanHost(const char *host, int minDots)
{
	int span;

	if (0 < (span = spanDomain(host, minDots)))
		return span;

	return spanIP(host);
}

/*
 * RFC 2821
 */
int
spanFQDN(const char *host)
{
	if (host == NULL)
		return 0;

	/* RFC 2821 Address Literal */
	if (*host == '[') {
		int span = spanIP(host+1);
		return host[1+span] == ']' ? span : 0;
	}

	return spanHost(host, 2);
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
	{ "cid",	sizeof ("cid")-1, 	0 },
	{ "ftp",	sizeof ("ftp")-1, 	21 },
	{ "gopher",	sizeof ("gopher")-1, 	70 },
	{ "http",	sizeof ("http")-1, 	80 },
	{ "https",	sizeof ("https")-1, 	443 },
	{ "imap",	sizeof ("imap")-1, 	143 },
	{ "ldap",	sizeof ("ldap")-1, 	389 },
	{ "mailto",	sizeof ("mailto")-1, 	25 },
	{ "smtp",	sizeof ("smtp")-1, 	25 },
	{ "nntp",	sizeof ("nntp")-1, 	119 },
	{ "pop3",	sizeof ("pop3")-1, 	110 },
	{ "telnet",	sizeof ("telnet")-1, 	23 },
	{ "rtsp",	sizeof ("rtsp")-1, 	554 },
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
 *	RFC 2396.
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
	int span;
	URI *uri;
	struct mapping *t;
	char *value, *mark;

	if (u == NULL)
		goto error0;

	if (3 < uriDebug)
		syslog(LOG_DEBUG, "uriParse2(\"%.60s...\", %d, %d)", u, length, implicit_domain_min_dots);

	if (length < 0)
		length = (int) strlen(u);

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

	if ((uri->fragment = strchr(value, '#')) != NULL)
		*uri->fragment++ = '\0';

	if ((uri->query = strchr(value, '?')) != NULL)
		*uri->query++ = '\0';

	if (value[0] == '/' && value[1] == '/') {
		value += 2;

		if ((uri->host = strchr(value, '@')) != NULL) {
			uri->userInfo = value;
			*uri->host++ = '\0';
		} else {
			uri->host = value;
		}

		uriDecodeSelf(uri->host);

		if (0 < (span = spanHost(uri->host, 0))) {
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

			if (uri->host[span] == ':') {
				uri->port = uri->host + span;
				*uri->port++ = '\0';
				if (uriGetSchemePort(uri) == -1)
					goto error1;
			}

			if (strlen(uri->host) != span)
				goto error1;
		} else {
			uri->host = NULL;
		}
	} else if (value[0] == '/') {
		uri->path = value;
	}

	/* This used to be spanDomain, but it is useful to also
	 * try and find either IPv4 or IPv6 addresses.
	 */
	else if (0 < (span = spanHost(value, implicit_domain_min_dots)) && value[span] == '\0'
	&& 0 < TextInsensitiveStartsWith(value, "www.") && 0 < indexValidTLD(value)) {
		uri->scheme = "http";
		uri->host = value;
	}

	else if ((uri->scheme == NULL || uriGetSchemePort(uri) == 25) && (uri->host = strchr(value, '@')) != NULL && value < uri->host) {
		*uri->host++ = '\0';

		for (uri->userInfo = value; *uri->userInfo != '\0' && spanLocalPart(uri->userInfo) <= 0; uri->userInfo++)
			;

		if (*uri->userInfo == '\0' || (span = spanDomain(uri->host, 1)) <= 0)
			goto error1;

		uri->host[span] = '\0';
		uri->scheme = "mailto";
		snprintf(uri->uriDecoded, length+1, "%s%c%s", uri->userInfo, at_sign_delim, uri->host);
	}

	if (uri->scheme != NULL && uri->host != NULL) {
#ifdef NOT_YET
		/* Convience information. */
		uri->reservedTLD = isRFC2606(uri->host);
		uri->offsetTLD = indexValidTLD(uri->host);
#endif
		return uri;
	}
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
	URI *uri;
	Vector visited;
	Socket2 *socket;
	const char *error;
	int port, n, redirect_count;
	char *buffer, *freeurl, *visited_uri;

	uri = NULL;
	error = NULL;

	if (url == NULL && origin == NULL) {
		error = uriErrorNullArgument;
		errno = EFAULT;
		goto error0;
	}

	if ((buffer = malloc(HTTP_BUFFER_SIZE)) == NULL) {
		error = uriErrorMemory;
		syslog(LOG_ERR, "uriHttpOrigin(): %s", error);
		goto error1;
	}

	if ((visited = VectorCreate(10)) == NULL) {
		error = uriErrorMemory;
		syslog(LOG_ERR, "uriHttpOrigin(): %s", error);
		goto error2;
	}

	VectorSetDestroyEntry(visited, free);

	redirect_count = 0;
	for (socket = NULL, freeurl = NULL; ; ) {
		for (n = 0; n < VectorLength(visited); n++) {
			if ((visited_uri = VectorGet(visited, n)) == NULL)
				continue;

			if (TextInsensitiveCompare(url, visited_uri) == 0) {
				error = uriErrorLoop;
				if (0 < uriDebug)
					syslog(LOG_DEBUG, "%s: %s", error, url);
				goto error3;
			}
		}

		if (VectorAdd(visited, strdup(url))) {
			error = uriErrorMemory;
			syslog(LOG_ERR, "uriHttpOrigin(): %s", error);
			goto error3;
		}

		if ((uri = uriParse2(url, -1, IMPLICIT_DOMAIN_MIN_DOTS)) == NULL) {
			error = uriErrorParse;
			if (0 < uriDebug)
				syslog(LOG_DEBUG, "%s: %s", error, url);
			goto error3;
		}

		if ((port = uriGetSchemePort(uri)) == -1) {
			error = uriErrorPort;
			if (0 < uriDebug)
				syslog(LOG_DEBUG, "%s: %s", error, uri->uriDecoded);
			goto error3;
		}

		/* We won't bother with https: nor anything that didn't
		 * default to the http: port 80 nor explicitly specify
		 * a port.
		 */
		if (port == 443 || (port != 80 && uri->port == NULL)) {
			error = uriErrorNotHttp;
			if (0 < uriDebug)
				syslog(LOG_DEBUG, "%s: %s", error, uri->uriDecoded);
			goto error3;
		}

		if (0 < uriDebug)
			syslog(LOG_DEBUG, "connect %s:%d", uri->host, port);

		socketClose(socket);

		if (socketOpenClient(uri->host, port, socket_timeout, NULL, &socket) == SOCKET_ERROR) {
			error = uriErrorConnect;
			if (0 < uriDebug)
				syslog(LOG_DEBUG, "%s: %s:%d", error, uri->host, port);
			goto error3;
		}
#ifdef __unix__
		(void) fileSetCloseOnExec(socketGetFd(socket), 1);
#endif
		socketSetTimeout(socket, socket_timeout);

		/* NOTE the query string is not added in order to
		 * minimize  identifying / confirming anything.
		 */
		n = snprintf(
			buffer, HTTP_BUFFER_SIZE, "HEAD %s%s%s%s HTTP/1.0\r\nHost: %s:%d\r\n\r\n",
			(uri->path == NULL || *uri->path != '/') ? "/" : "",
			uri->path == NULL ? "" : uri->path,
			(0 < redirect_count && uri->query != NULL) ? "?" : "",
			(0 < redirect_count && uri->query != NULL) ? uri->query : "",
			uri->host, port
		);

		if (0 < uriDebug)
			syslog(LOG_DEBUG, "> %s", buffer);

		if (socketWrite(socket, (unsigned char *) buffer, n) != n) {
			error = uriErrorWrite;
			if (0 < uriDebug)
				syslog(LOG_DEBUG, "%s: %s:%d", error, uri->host, port);
			goto error3;
		}

		if ((n = (int) socketReadLine(socket, buffer, HTTP_BUFFER_SIZE)) < 0) {
			error = uriErrorRead;
			if (0 < uriDebug)
				syslog(LOG_DEBUG, "%s (%d): %s:%d", error, n, uri->host, port);
			goto error3;
		}

		if (0 < uriDebug)
			syslog(LOG_DEBUG, "< %s", buffer);

		if (sscanf(buffer, "HTTP/%*s %d", &uri->status) != 1) {
			error = uriErrorHttpResponse;
			if (0 < uriDebug)
				syslog(LOG_DEBUG, "%s: %s", error, buffer);
			goto error3;
		}

		if (400 <= uri->status) {
			error = uriErrorNoOrigin;
			if (0 < uriDebug)
				syslog(LOG_DEBUG, "%s: %s", error, uri->uriDecoded);
			goto error3;
		}

		/* Have we found the origin server? */
		if (200 <= uri->status && uri->status < 300) {
			if (0 < uriDebug)
				syslog(LOG_DEBUG, "found HTTP origin %s:%d", uri->host, uriGetSchemePort(uri));
			goto origin_found;
		}

		/* Assume that we have a 301, 302, 303, or 307 response.
		 * Read and parse the Location header. If not found then
		 * it was different 3xx response code and an error.
		 */

		while (0 < (n = socketReadLine(socket, buffer, HTTP_BUFFER_SIZE))) {
			if (0 < uriDebug)
				syslog(LOG_DEBUG, "< %s", buffer);
			if (0 < TextInsensitiveStartsWith(buffer, "Location:")) {
				url = buffer + sizeof("Location:")-1;
				url += strspn(url, " \t");

				free(freeurl);
				freeurl = NULL;

				/* Is it a redirection on the same server,
				 * ie. no http://host:port given?
				 */
				if (*url == '/') {
					size_t size = 7 + strlen(uri->host) + 16 + strlen(url) + 1;
					if ((freeurl = malloc(size)) == NULL) {
						error = uriErrorMemory;
						syslog(LOG_ERR, "uriHttpOrigin(): %s", error);
						goto error3;
					}
					snprintf(freeurl, size, "http://%s:%d%s", uri->host, port, url);
					url = freeurl;
				}

				free(uri);
				uri = NULL;
				redirect_count++;
				break;
			}
		}

		if (n <= 0) {
			error = uriErrorNoLocation;
			if (0 < uriDebug)
				syslog(LOG_DEBUG, "%s: %s:%d", error, uri->host, port);
			goto error3;
		}
	}

	/*@notreached@*/

error3:
	free(uri);
	uri = NULL;
origin_found:
	VectorDestroy(visited);
	socketClose(socket);
	free(freeurl);
error2:
	free(buffer);
error1:
	*origin = uri;
error0:
	return error;
}

/***********************************************************************
 *** Parsing URI in MIME parts
 ***********************************************************************/

#ifndef URI_MIME_BUFFER_SIZE
#define URI_MIME_BUFFER_SIZE	1024
#endif

typedef struct {
	URI *uri;
	int length;
	char buffer[URI_MIME_BUFFER_SIZE];
} UriMime;

void
uriMimeBodyStart(Mime *m)
{
	UriMime *hold = m->mime_data;

	free(hold->uri);
	hold->uri = NULL;
	hold->length = 0;
}

void
uriMimeDecodedOctet(Mime *m, int ch)
{
	UriMime *hold;

	hold = m->mime_data;

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
	if (ch == '%' || isCharURI(ch) || (0 < hold->length && hold->buffer[hold->length-1] == '&' && ch == '#')) {
		/* Look for HTML numerical entities &#NNN; or &#xHHHH; */
		if (0 < hold->length && ch == ';') {
			int offset;

			hold->buffer[hold->length] = '\0';
			offset = strlrcspn(hold->buffer, hold->length, "&");

			if (0 < offset && hold->buffer[offset] == '#') {
				if (hold->buffer[offset+1] == 'x')
					ch = strtol(hold->buffer+offset+2, NULL, 16);
				else
					ch = strtol(hold->buffer+offset+1, NULL, 10);

				/* Rewind to the ampersand. */
				hold->length = offset - 1;
			} else if (0 < offset && strcmp(hold->buffer+offset, "lt") == 0) {
				ch = '<';
				hold->length = offset - 1;
			} else if (0 < offset && strcmp(hold->buffer+offset, "gt") == 0) {
				ch = '>';
				hold->length = offset - 1;
			} else if (0 < offset && strcmp(hold->buffer+offset, "amp") == 0) {
				ch = '&';
				hold->length = offset - 1;
			}
		}

		hold->buffer[hold->length++] = ch;
		hold->uri = NULL;

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
		 * strip them off.
		 */
		if ((hold->buffer[value] == '('  && hold->buffer[hold->length-1] == ')')
		||  (hold->buffer[value] == '\'' && hold->buffer[hold->length-1] == '\'')) {
			hold->length--;
			value++;
		}

		hold->uri = uriParse2(hold->buffer+value, hold->length-value, IMPLICIT_DOMAIN_MIN_DOTS);
		if (2 < uriDebug && hold->uri != NULL)
			syslog(LOG_DEBUG, "found URL \"%s\"", hold->uri->uri);

		hold->length = 0;
	}
}

void
uriMimeFlush(Mime *m)
{
	/* Force parse of hold before flush. */
	uriMimeDecodedOctet(m, '\n');
}

/**
 * @param include_headers
 *	When true, parse both the message headers and body for URI.
 *	Otherwise only parse the body for URI.
 *
 * @return
 *	A pointer to a Mime object. The handlers for URI processing
 *	will already be defined. See mail/mime.h.
 */
Mime *
uriMimeCreate(int include_headers)
{
	Mime *mime;
	UriMime *hold;

	if ((hold = calloc(1, sizeof (UriMime))) == NULL)
		return NULL;

	if ((mime = mimeCreate(hold)) == NULL) {
		free(hold);
		return NULL;
	}

	mime->mime_flush = uriMimeFlush;
	mime->mime_body_start = uriMimeBodyStart;
	mime->mime_body_finish = uriMimeBodyStart;
	mime->mime_decoded_octet = uriMimeDecodedOctet;

	if (include_headers)
		mime->mime_header_octet = uriMimeDecodedOctet;

	return mime;
}

/**
 * Free the current URI object.
 *
 * @param _m
 *	A pointer to a Mime object, previously obtained from uriMimeCreate().
 */
void
uriMimeFreeUri(Mime *m)
{
	UriMime *hold;

	if (m != NULL && m->mime_data != NULL) {
		hold = m->mime_data;
		free(hold->uri);
		hold->uri = NULL;
	}
}

/**
 * Free the Mime object used for URI processing.
 *
 * @param _m
 *	A pointer to a Mime object, previously obtained from uriMimeCreate().
 */
void
uriMimeFree(void *_m)
{
	Mime *m = (Mime *) _m;

	if (m != NULL) {
		uriMimeFreeUri(m);
		free(m->mime_data);
		mimeFree(m);
	}
}

/**
 * @return
 *	A pointer to a URI object if there is a URI ready for testing,
 *	otherwise NULL.
 */
URI *
uriMimeGetUri(Mime *m)
{
	if (m == NULL || m->mime_data == NULL)
		return NULL;
	return ((UriMime *) m->mime_data)->uri;
}

/***********************************************************************
 ***
 ***********************************************************************/

#ifdef TEST
#include <stdio.h>
#include <stdlib.h>

#include <com/snert/lib/sys/sysexits.h>
#include <com/snert/lib/util/getopt.h>
#include <com/snert/lib/net/pdq.h>

static int exit_code;
static int check_all;
static int check_link;
static int check_files;
static int check_query;
static int check_subdomains;
static int print_uri_parse;
static char *nsBlOption;
static char *uriBlOption;
static Vector print_uri_ports;
static long *uri_ports;

static char usage[] =
"usage: uri [-aflpqrsv][-A delim][-n ns-bl,...][-u uri-bl,...]\n"
"           [-P ports][-t sec][-T sec][arg ...]\n"
"\n"
"-a\t\tcheck all (headers & body), otherwise assume body only\n"
"-A delim\tan alternative delimiter to replace the at-sign (@)\n"
"-D\t\tcheck sub-domains segments of URI domains\n"
"-f\t\tcommand line arguments are file names\n"
"-l\t\tcheck HTTP links are valid & find origin server\n"
"-n ns-bl,...\tDNS suffix[/mask] list to apply. Without the /mask\n"
"\t\ta suffix would be equivalent to suffix/0x00fffffe\n"
"-p\t\tprint each URI parsed\n"
"-P ports\tselect only the URI corresponding to the comma\n"
"\t\tseparated list of port numbers to print and/or test\n"
"-q\t\tcheck URL query part for embedded URLs\n"
"-R\t\tenable DNS round robin mode, default parallel mode\n"
"-s\t\tcheck URI domain has valid SOA\n"
"-t sec\t\tHTTP socket timeout in seconds, default 60\n"
"-T sec\t\tDNS timeout in seconds, default 45\n"
"-u uri-bl,...\tDNS suffix[/mask] list to apply. Without the /mask\n"
"\t\ta suffix would be equivalent to suffix/0x00fffffe\n"
"-v\t\tverbose logging to system's user log\n"
"\n"
"Each argument is a URI to be parsed and tested. If -f is specified\n"
"then each argument is a filename (use \"-\" for standard input) to be\n"
"searched for URIs, parsed, and tested.\n"
"\n"
"Exit Codes\n"
QUOTE(EXIT_SUCCESS) "\t\tall URI tested are OK\n"
QUOTE(EXIT_FAILURE) "\t\tone or more URI are blacklisted\n"
QUOTE(EX_USAGE) "\t\tusage error\n"
QUOTE(EX_SOFTWARE) "\t\tinternal error\n"
"\n"
LIBSNERT_COPYRIGHT "\n"
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
int check_soa;
DnsList *ns_bl_list;
DnsList *uri_bl_list;
Vector ns_names_seen;

void
process(URI *uri, const char *filename)
{
	long *p;
	const char *error;
	URI *origin = NULL;

	if (uri == NULL || uri->host == NULL)
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

	if (check_link) {
		error = uriHttpOrigin(uri->uriDecoded, &origin);
		if (filename != NULL)
			printf("%s: ", filename);
		printf("%s -> ", uri->uriDecoded);
		printf("%s\n", error == NULL ? origin->uri : error);
		if (error != NULL)
			exit_code = EXIT_FAILURE;
	}

	if (uri->host != NULL) {
		PDQ_valid_soa code;
		const char *list_name = NULL;
		if ((list_name = dnsListQuery(uri_bl_list, pdq, NULL, check_subdomains, uri->host)) != NULL) {
			if (filename != NULL)
				printf("%s: ", filename);
			printf("%s domain blacklisted %s\n", uri->host, list_name);
			exit_code = EXIT_FAILURE;
		}

		if ((list_name = dnsListQueryNs(ns_bl_list, pdq, ns_names_seen, uri->host)) != NULL) {
			if (filename != NULL)
				printf("%s: ", filename);
			printf("%s NS blacklisted %s\n", uri->host, list_name);
			exit_code = EXIT_FAILURE;
		}

		if (check_soa && (code = pdqTestSOA(pdq, PDQ_CLASS_IN, uri->host, NULL)) != PDQ_SOA_OK) {
			if (filename != NULL)
				printf("%s: ", filename);
			printf("%s bad SOA %s (%d)\n", uri->host, pdqSoaName(code), code);
			exit_code = EXIT_FAILURE;
		}
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

int
process_file(const char *filename)
{
	URI *uri;
	FILE *fp;
	Mime *mime;
	int ch, rc;

	rc = -1;

	/* Check for standard input. */
	if (filename[0] == '-' && filename[1] == '\0')
		fp = stdin;

	/* Otherwise open the file. */
	else if ((fp = fopen(filename, "r")) == NULL)
		goto error0;

	if ((mime = uriMimeCreate(check_all)) == NULL)
		goto error1;

	while ((ch = fgetc(fp)) != EOF) {
		if (mimeNextCh(mime, ch))
			goto error2;

		/* Is there a URI ready to check? */
		if ((uri = uriMimeGetUri(mime)) != NULL) {
			process(uri, filename);
			if (check_query) {
				process_list(uri->query, "&", filename);
				process_list(uri->query, "/", filename);
				process_list(uri->path, "/", filename);
			}
			uriMimeFreeUri(mime);
		}
	}

	rc = 0;
error2:
	uriMimeFree(mime);
error1:
	fclose(fp);
error0:
	return rc;
}

int
main(int argc, char **argv)
{
	URI *uri;
	int i, ch;

	while ((ch = getopt(argc, argv, "aA:Dn:u:flmpP:qRsT:t:v")) != -1) {
		switch (ch) {
		case 'a':
			check_all = 1;
			break;
		case 'A':
			at_sign_delim = *optarg;
			break;
		case 'n':
			nsBlOption = optarg;
			break;
		case 'u':
			uriBlOption = optarg;
			break;
		case 'D':
			check_subdomains = 1;
			break;
		case 'f':
			check_files = 1;
			break;
		case 'l':
			check_link = 1;
			break;
		case 'P':
			print_uri_ports = TextSplit(optarg, ",", 0);
			break;
		case 'p':
			print_uri_parse = 1;
			break;
		case 'q':
			check_query = 1;
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
			setlogmask(LOG_UPTO(LOG_DEBUG));
#else
			LogOpen("(standard error)");
			LogSetProgramName("uri");
			LogSetLevel(LOG_DEBUG);
#endif
			dnsListSetDebug(1);
			socketSetDebug(1);
			uriSetDebug(4);
			pdqSetDebug(1);
			break;
		default:
			(void) fprintf(stderr, usage);
			return EX_USAGE;
		}
	}

	if (argc <= optind) {
		(void) fprintf(stderr, usage);
		return EX_USAGE;
	}

	if (pdqInit()) {
		fprintf(stderr, "pdqInit() failed\n");
		exit(EX_SOFTWARE);
	}

	if ((pdq = pdqOpen()) == NULL) {
		fprintf(stderr, "pdqOpen() failed\n");
		exit(EX_SOFTWARE);
	}

	ns_names_seen = VectorCreate(10);
	VectorSetDestroyEntry(ns_names_seen, free);
	ns_bl_list = dnsListCreate(nsBlOption);
	uri_bl_list = dnsListCreate(uriBlOption);

	if (0 < VectorLength(print_uri_ports)) {
		uri_ports = malloc(sizeof (long) * (VectorLength(print_uri_ports) + 1));

		for (i = 0; i < VectorLength(print_uri_ports); i++) {
			char *port = VectorGet(print_uri_ports, i);
			uri_ports[i] = strtol(port, NULL, 10);
		}
		uri_ports[i] = -1;
	}

	exit_code = EXIT_SUCCESS;

	if (argv[optind][0] == '-' && argv[optind][1] == '\0')
		check_files = 1;

	for (i = optind; i < argc; i++) {
		if (check_files) {
			if (process_file((const char *) argv[i]))
				break;
		} else if ((uri = uriParse2((const char *) argv[i], -1, 1)) != NULL) {
			process(uri, NULL);
			if (check_query) {
				process_list(uri->query, "&", NULL);
				process_list(uri->query, "/", NULL);
				process_list(uri->path, "/", NULL);
			}
			free(uri);
		}
	}

	VectorDestroy(ns_names_seen);
	dnsListFree(uri_bl_list);
	dnsListFree(ns_bl_list);
	pdqClose(pdq);

	return exit_code;
}
#endif
