/*
 * cgi.h
 *
 * RFC 3875 The Common Gateway Interface (CGI) Version 1.1
 *
 * Copyright 2004, 2012 by Anthony Howe. All rights reserved.
 */

#ifndef __com_snert_lib_util_cgi_h__
#define __com_snert_lib_util_cgi_h__	1

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdarg.h>

#include <com/snert/lib/io/socket2.h>
#include <com/snert/lib/net/http.h>
#include <com/snert/lib/util/Buf.h>
#include <com/snert/lib/util/option.h>

/***********************************************************************
 ***
 ***********************************************************************/

typedef struct {
	char *name;
	char *value;
} CgiMap;

typedef struct {
	int port;
	int is_nph;
	const char *content_type;
	const char *content_length;
	const char *document_root;
	const char *path_info;
	const char *path_translated;
	const char *query_string;
	const char *remote_addr;
	const char *request_uri;
	const char *request_method;
	const char *script_filename;
	const char *script_name;
	const char *server_name;
	const char *server_port;
	const char *server_protocol;

	Buf *_RAW;
	FILE *out;
	CgiMap *_GET;
	CgiMap *_POST;
	CgiMap *_HTTP;
	CgiMap *headers;		/* Output headers. */
	HttpCode status;
} CGI;

/**
 * @param tp
 *	A pointer to pointer of char. Decoded bytes from the source
 *	string are copied to this destination. The destination buffer
 *	must be as large as the source. The copied string is '\0'
 *	terminated and the pointer passed back points to next byte
 *	after the terminating '\0'.
 *
 * @param sp
 * 	A pointer to pointer of char. The URL encoded bytes are copied
 *	from this source buffer to the destination. The copying stops
 *	after an equals-sign, ampersand, or on a terminating '\0' and
 *	this pointer is passed back.
 */
extern void cgiUrlDecode(char **tp, const char **sp);

/**
 * @param urlencoded
 *	A URL encoded string such as the query string portion of an HTTP
 *	request or HTML form data ie. application/x-www-form-urlencoded.
 *
 * @return
 *	A pointer to array 2 of pointer to char. The first column of the
 *	table are the field names and the second column their associated
 *	values. The array is NULL terminated. The array pointer returned
 *	must be released with a single call to free().
 */
extern CgiMap *cgiParseForm(const char *urlencoded);

extern void cgiMapFree(CgiMap *map);

extern int cgiMapAdd(CgiMap **map, const char *name, const char *fmt, ...);

extern int cgiMapFind(CgiMap *map, char *prefix);

extern void cgiSendV(CGI *cgi, HttpCode code, const char *response, const char *fmt, va_list args);

extern void cgiSend(CGI *cgi, HttpCode code, const char *response, const char *fmt, ...);

extern void cgiSendOk(CGI *cgi, const char *fmt, ...);

extern void cgiSendNoContent(CGI *cgi);

extern void cgiSendSeeOther(CGI *cgi, const char *url, const char *fmt, ...);

extern void cgiSendBadRequest(CGI *cgi, const char *fmt, ...);

extern void cgiSendNotFound(CGI *cgi, const char *fmt, ...);

extern void cgiSendInternalServerError(CGI *cgi, const char *fmt, ...);

extern int cgiSetOptions(CGI *cgi, CgiMap *array, Option *table[]);

extern int cgiRawInit(CGI *cgi, Socket2 *client, int is_nph);

extern int cgiInit(CGI *cgi);

extern void cgiFree(CGI *cgi);

/***********************************************************************
 ***
 ***********************************************************************/

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_util_cgi_h__ */
