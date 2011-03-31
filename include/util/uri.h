/*
 * uri.h
 *
 * RFC 2396
 *
 * Copyright 2006, 2011 by Anthony Howe. All rights reserved.
 */

#ifndef __com_snert_lib_util_uri_h__
#define __com_snert_lib_util_uri_h__	1

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <com/snert/lib/util/b64.h>
#include <com/snert/lib/util/option.h>
#include <com/snert/lib/mail/mime.h>

typedef struct {
	/* Complete URI */
	char *uri;
	char *uriDecoded;	/* URL decoded */

	/* <scheme>:<scheme-specific> */
	char *scheme;
	char *schemeInfo;

	/* <scheme>://[<userinfo>@]<host>[:<port>][/<path>][?<query>][#<fragment>]
	 * <scheme>:/<path>[?<query>][#<fragment>]
	 */
	char *userInfo;
	char *host;		/* URL decoded */
	char *port;		/* URL decoded */
	char *path;
	char *query;
	char *fragment;
	int status;
#ifdef NOT_YET
	int offsetTLD;		/* TLD offset in uri, or -1 if not found. */
	int reservedTLD;
#endif
} URI;

typedef void (*UriMimeHook)(URI *, void *);
typedef struct uri_mime UriMime;

/**
 * @param octet
 *	An octet to check.
 *
 * @return
 *	True if the octet is in the set of characters for a URI.
 */
extern int isCharURI(int octet);

/**
 * @param arg
 *	A pointer to a C string.
 *
 * @return
 *	The length of the syntactically valid portion of the string upto,
 *	but excluding, the first invalid character following it; otherwise
 *	zero (0) for a parse error.
 */
extern int spanQuery(const char *query);
extern int spanFragment(const char *fragment);
extern int spanScheme(const char *scheme);
extern int spanURI(const char *uri);

extern void uriSetDebug(int flag);

extern void uriSetTimeout(long ms);

/**
 * @param u
 *	A pointer to a C string representing a URI as described by
 *	RFC 2396.
 *
 * @param length
 *	The maximum length of the URI string to parse or -1 for the
 *	whole string.
 *
 * @return
 *	A pointer to a URI structure. Its the caller's responsibility
 *	to free() this pointer.
 *
 * @see
 *	uri-implicit-domain-min-dots=2
 */
extern URI *uriParse(const char *u, int length);

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
extern URI *uriParse2(const char *u, int length, int implicit_domain_min_dots);

/**
 * @param uri
 *	A pointer to a URI structure.
 *
 * @return
 *	A positive port number or -1 if the scheme is unknown.
 */
extern int uriGetSchemePort(URI *uri);

/**
 * @param s
 *	A pointer to a URI encoded C string.
 *
 * @return
 *	A pointer to an allocated C string containing the decode URI.
 *	Its the caller's responsibility to free() this pointer.
 */
extern char *uriDecode(const char *s);

/**
 * @param s
 *	A pointer to a URI encoded C string, which is to be decoded
 *	in place.
 */
extern void uriDecodeSelf(char *s);

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
extern const char *uriHttpOrigin(const char *uri, URI **result);

extern const char uriErrorConnect[];
extern const char uriErrorHttpResponse[];
extern const char uriErrorLoop[];
extern const char uriErrorMemory[];
extern const char uriErrorNoLocation[];
extern const char uriErrorNoOrigin[];
extern const char uriErrorNotHttp[];
extern const char uriErrorNullArgument[];
extern const char uriErrorParse[];
extern const char uriErrorPort[];
extern const char uriErrorRead[];
extern const char uriErrorWrite[];
extern const char uriErrorOverflow[];

/**
 * @param host
 *	A pointer to a C string containing a FQDN or IP address.
 *
 * @param dnsbl_suffix
 *	A pointer to a C string of a DNSBL suffix to be appended to
 *	the query lookup.
 *
 * @param mask
 *	A 32-bit unsigned bit mask of used for aggregate lists. Set
 *	the bits that you are interested in. Specify ~0L for all.
 *	Note that bits 1 and 25..32 are ignored due to the way
 *	most blacklists are implemented in DNS.
 *
 * @param lookup_subdomains
 *	If true and host is not an IP address, then first check the
 *	registered domain and then subsequent sub-domains working
 *	from right to left.
 *
 * @return
 *	True if the host is black listed.
 *
 * @see
 *	http://www.surbl.org/implementation.html
 */
extern int uriIsDomainBL(const char *host, const char *dnsbl_suffix, unsigned long mask, int lookup_subdomains);

/**
 * @param host
 *	A pointer to a C string containing a FQDN or IP address. Lookup
 *	the host's A or AAAA record and check each of its IPs against
 *	the DNSBL.
 *
 * @param dnsbl_suffix
 *	A pointer to a C string of DNSBL suffix to be appended to
 *	the query lookup.
 *
 * @param mask
 *	A 32-bit unsigned bit mask of used for aggregate lists. Set
 *	the bits that you are interested in. Specify ~0L for all.
 *	Note that bits 1 and 25..32 are ignored due to the way
 *	most blacklists are implemented in DNS.
 *
 * @param dummy
 *	Not used. Present for function signature compatibility with
 *	uriIsDomainBL().
 *
 * @return
 *	True if the host is black listed.
 *
 * @see
 *	http://www.spamhaus.org/sbl/howtouse.html
 */
extern int uriIsHostBL(const char *host, const char *dnsbl_suffix, unsigned long mask, int dummy);

/**
 * @param uri_found_cb
 *	A call-back function when a URI is found.
 *
 * @param data
 *	Application data to be passed to URI call-backs.
 *
 * @return
 *	A pointer to a UriMime structure suitable for passing to
 *	mimeHooksAdd(). The UriMime * will have to cast to MimeHooks *.
 *	This structure and data are freed by mimeFree().
 */
extern UriMime *uriMimeInit(UriMimeHook uri_found_cb, void *data);

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_util_uri_h__ */
