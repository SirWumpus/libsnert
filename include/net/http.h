/**
 * http.h
 *
 * RFC 2616 HTTP/1.1 Support Routines
 *
 * Copyright 2009 by Anthony Howe. All rights reserved.
 */

#ifndef __com_snert_lib_net_http_h__
#define __com_snert_lib_net_http_h__	1

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************************
 ***
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <com/snert/lib/io/socket2.h>
#include <com/snert/lib/sys/Time.h>
#include <com/snert/lib/util/Buf.h>
#include <com/snert/lib/util/uri.h>

/***********************************************************************
 ***
 ***********************************************************************/

#define HTTP_PORT			80
#define HTTPS_PORT			443
#define HTTP_TIMEOUT_MS			30000
#define HTTP_LINE_SIZE			2048
#define HTTP_BUFFER_SIZE		8096

typedef enum {
	HTTP_CONTINUE			= 0,
	HTTP_DROP			= 10,

	HTTP_OK				= 200,
	HTTP_CREATED			= 201,
	HTTP_ACCEPTED			= 202,
	HTTP_NON_AUTH_INFO		= 203,
	HTTP_NO_CONTENT			= 204,
	HTTP_RESET_CONTENT		= 205,
	HTTP_PARTIAL_CONTENT		= 206,

	HTTP_MULTIPLE_CHOICES		= 300,
	HTTP_MOVED_PERMANENTLY		= 301,
	HTTP_FOUND			= 302,
	HTTP_SEE_OTHER			= 303,
	HTTP_NOT_MODIFIED		= 304,
	HTTP_USE_PROXY			= 305,
	HTTP_TEMPORARY_REDIRECT		= 307,

	HTTP_BAD_REQUEST		= 400,
	HTTP_UNAUTHORIZED		= 401,
	HTTP_PAYMENT_REQUIRED		= 402,
	HTTP_FORBIDDEN			= 403,
	HTTP_NOT_FOUND			= 404,
	HTTP_METHOD_NOT_ALLOWED		= 405,
	HTTP_NOT_ACCEPTABLE		= 406,
	HTTP_PROXY_AUTH_REQUIRED	= 407,
	HTTP_REQUEST_TIMEOUT		= 408,
	HTTP_CONFLICT			= 409,
	HTTP_GONE			= 410,
	HTTP_LENGTH_REQUIRED		= 411,
	HTTP_PRECOND_FAILED		= 412,
	HTTP_REQUEST_TOO_LARGE		= 413,
	HTTP_URI_TOO_LONG		= 414,
	HTTP_UNSUPPORTED_MEDIA		= 415,
	HTTP_RANGE_NOT_POSSIBLE		= 416,
	HTTP_EXPECTATION_FAILED		= 417,

	HTTP_INTERNAL			= 500,
	HTTP_NOT_IMPLEMENTED		= 501,
	HTTP_BAD_GATEWAY		= 502,
	HTTP_SERVICE_UNAVAILABLE	= 503,
	HTTP_GATEWAY_TIMEOUT		= 504,
	HTTP_VERSION_NOT_SUPPORTED	= 505,
} HttpCode;

typedef struct {
	URI *url;
	long timeout;
	const char *from;
	const char *method;
	const char *credentials;
	time_t if_modified_since;	/* GMT seconds since epoch. */
	unsigned char *post_buffer;
	size_t post_size;
} HttpRequest;

typedef struct {
	HttpCode result;
	Buf *content;			/* If NULL, Buf * will be created. */
	time_t date;			/* Ignore if zero; else GMT seconds since epoch. */
	time_t expires;			/* Ignore if zero; else GMT seconds since epoch. */
	time_t last_modified;		/* Ignore if zero; else GMT seconds since epoch. */
	size_t content_length;		/* Content-Length: header or length of file. */
	char *content_type;		/* Content-Type: header. */
	char *content_encoding;		/* Content-Encoding: header. */
} HttpResponse;

/***********************************************************************
 ***
 ***********************************************************************/

extern void httpSetDebug(int level);

extern void httpResponseInit(HttpResponse *);
extern void httpResponseFree(HttpResponse *);

extern Socket2 *httpSend(HttpRequest *);
extern HttpCode httpRead(Socket2 *, HttpResponse *);

extern HttpCode httpDoGet(const char *url, time_t modified_since, HttpResponse *response);
extern HttpCode httpDoHead(const char *url, time_t modified_since, HttpResponse *response);
extern HttpCode httpDoPost(const char *url, time_t modified_since, unsigned char *post, size_t size, HttpResponse *response);

/***********************************************************************
 ***
 ***********************************************************************/

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_net_http_h__ */
