/*
 * URI.h
 *
 * RFC 2396 - Uniform Resource Identifier (URI)
 *
 * Copyright 2001, 2004 by Anthony Howe. All rights reserved.
 */

#ifndef __com_snert_lib_io_URI_h__
#define __com_snert_lib_io_URI_h__	1

typedef struct uri {
	/* Raw URI provided. */
	char *uri;

	/* <scheme>:<value> */
	char *scheme;
	char *value;

	/* Two common URI formats used:
	 *
	 *	<scheme>://[<userinfo>@]<host>[:<port>][/<path>][?<query>][#<fragment>]
	 *	<scheme>:/<path>[?<query>][#<fragment>]
	 */
	char *userinfo;
	char *host;
	char *port;
	char *path;
	char *query;
	char *fragment;
} *URI;

/**
 * This class parses a Uniform Resource Identifier (URI) as per RFC 2396.
 * It is similar to java.net.URL, without the IO methods. This class
 * is ment to be only a parser and informational value object (nothing
 * else).
 *
 * @see "RFC 2396"
 * @see java.net.URL
 */
#ifdef __cplusplus
extern "C" {
#endif

extern URI uriCreate(const char *);
extern void uriFree(URI);
extern long uriHashCode(URI);
extern int uriIsValidScheme(const char *);
extern int uriIsValidHost(const char *);
extern int uriIsValidDomain(const char *);
extern int uriIsValidIP(const char *);
extern int uriDecode(unsigned char *t, long tsize, unsigned char *s);
extern int uriEquals(URI, URI);
extern int uriGetPort(URI);

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_io_URI_h__ */
