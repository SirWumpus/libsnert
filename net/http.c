/*
 * http.c
 *
 * RFC 2616 HTTP/1.1 Support Functions
 *
 * Copyright 2009, 2011 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#if defined(HAVE_UNISTD_H)
# include <unistd.h>
#endif
#if defined(HAVE_SYSLOG_H) && ! defined(__MINGW32__)
# include <syslog.h>
#endif

#include <com/snert/lib/io/Log.h>
#include <com/snert/lib/io/file.h>
#include <com/snert/lib/net/http.h>
#include <com/snert/lib/util/Text.h>
#include <com/snert/lib/util/time62.h>
#include <com/snert/lib/util/convertDate.h>

/***********************************************************************
 ***
 ***********************************************************************/

static int httpDebug;
static unsigned short http_counter;

void
httpSetDebug(int level)
{
	httpDebug = level;
}

char *
httpGetHeader(Buf *buf, const char *hdr_pat, size_t hdr_len)
{
	long offset;
	int span, ch;
	char *string = NULL;

	if (0 < (offset = TextFind((char *) buf->bytes, hdr_pat, -1, 1))) {
		offset += hdr_len;
		offset += strspn((char *) buf->bytes+offset, " \t");
		span = strcspn((char *) buf->bytes+offset, ";\r\n");
		ch = buf->bytes[offset+span];
		buf->bytes[offset+span] = '\0';
		string = strdup((char *) buf->bytes+offset);
		buf->bytes[offset+span] = ch;
	}

	return string;
}

int
httpResponseInit(HttpResponse *response)
{
	time_t now;

	memset(response, 0, sizeof (*response));

	if (++http_counter == 0)
		http_counter = 1;

	(void) time(&now);
	time62Encode(now, response->id_log);
	(void) snprintf(
		response->id_log+TIME62_BUFFER_SIZE,
		sizeof (response->id_log)-TIME62_BUFFER_SIZE,
		"%05u%05u00", getpid(), http_counter
	);

	response->debug = httpDebug;
	response->content = BufCreate(HTTP_BUFFER_SIZE);

	return -(response->content == NULL);
}

void
httpResponseFree(HttpResponse *response)
{
	if (response != NULL) {
		BufDestroy(response->content);
		free(response->url);
	}
}

static HttpCode
httpParseHeaderEnd(HttpResponse *response, unsigned char *input, size_t length)
{
	char *string;
	Buf *buf = response->content;
	HttpContent *content = response->data;

	content->expires = 0;
	content->last_modified = 0;
	content->content_length = 0;
	content->content_type = httpGetHeader(buf, "*Content-Type:*", sizeof ("Content-Type:")-1);
	content->content_encoding = httpGetHeader(buf, "*Content-Encoding:*", sizeof ("Content-Encoding:")-1);

	if (0 < response->debug && content->content_type != NULL)
		syslog(LOG_DEBUG, "%s content-type=%s", response->id_log, content->content_type);
	if (0 < response->debug && content->content_encoding != NULL)
		syslog(LOG_DEBUG, "%s content-encoding=%s", response->id_log, content->content_encoding);

	if ((string = httpGetHeader(buf, "*Content-Length:*", sizeof ("Content-Length:")-1)) != NULL) {
		content->content_length = strtol(string, NULL, 10);

		if (0 < response->debug)
			syslog(LOG_DEBUG, "%s content-length=%s", response->id_log, string);

		free(string);
	}

	if ((string = httpGetHeader(buf, "*Last-Modified:*", sizeof ("Last-Modified:")-1)) != NULL) {
		/* NOTE that this is in GMT. */
		(void) convertDate(string, &content->last_modified, NULL);

		if (0 < response->debug)
			syslog(LOG_DEBUG, "%s last-modified=%s", response->id_log, string);

		free(string);
	}

	if ((string = httpGetHeader(buf, "*Expires:*", sizeof ("Expires:")-1)) != NULL) {
		/* NOTE that this is in GMT. */
		(void) convertDate(string, &content->expires, NULL);

		if (0 < response->debug)
			syslog(LOG_DEBUG, "%s expires=%s", response->id_log, string);

		free(string);
	}

	if ((string = httpGetHeader(buf, "*Date:*", sizeof ("Date:")-1)) != NULL) {
		/* NOTE that this is in GMT. */
		(void) convertDate(string, &content->date, NULL);

		if (0 < response->debug)
			syslog(LOG_DEBUG, "%s date=%s", response->id_log, string);

		free(string);
	}

	return HTTP_CONTINUE;
}

int
httpContentInit(HttpContent *content)
{
	memset(content, 0, sizeof (*content));

	if (httpResponseInit(&content->response))
		return -1;

	content->response.data = content;
	content->response.hook.header_end = httpParseHeaderEnd;

	return 0;
}

void
httpContentFree(HttpContent *content)
{
	if (content != NULL) {
		free(content->content_type);
		free(content->content_encoding);
		httpResponseFree(&content->response);
	}
}

SOCKET
httpSend(HttpRequest *request)
{
	Buf *req;
	struct tm gmt;
	char stamp[40];
	SOCKET socket;
	long length, offset = 0;

	if (request == NULL)
		goto error0;

	if ((req = BufCreate(HTTP_BUFFER_SIZE)) == NULL)
		goto error0;

	/* Build the request buffer. */
	(void) BufAddString(req, request->method);
	(void) BufAddByte(req, ' ');
	(void) BufAddString(req, request->url->path == NULL ? "/" : request->url->path);
	(void) BufAddString(req, " HTTP/1.0\r\n");

	if (0 < request->debug) {
		syslog(LOG_DEBUG, "%s > %lu:%s", request->id_log, BufLength(req)-offset, BufBytes(req)+offset);
		offset = BufLength(req);
	}

	(void) BufAddString(req, "Host: ");
	(void) BufAddString(req, request->url->host);

	if (request->url->port != NULL) {
		(void) BufAddByte(req, ':');
		(void) BufAddString(req, request->url->port);
	}
	(void) BufAddBytes(req, (unsigned char *) "\r\n", sizeof ("\r\n")-1);

	if (0 < request->debug) {
		syslog(LOG_DEBUG, "%s > %lu:%s", request->id_log, BufLength(req)-offset, BufBytes(req)+offset);
		offset = BufLength(req);
	}

	if (request->from != NULL) {
		(void) BufAddString(req, "From: ");
		(void) BufAddString(req, request->from);
		(void) BufAddBytes(req, (unsigned char *) "\r\n", sizeof ("\r\n")-1);

		if (0 < request->debug) {
			syslog(LOG_DEBUG, "%s > %lu:%s", request->id_log, BufLength(req)-offset, BufBytes(req)+offset);
			offset = BufLength(req);
		}
	}

	if (request->accept_language != NULL) {
		(void) BufAddString(req, "Accept-Language: ");
		(void) BufAddString(req, request->accept_language);
		(void) BufAddBytes(req, (unsigned char *) "\r\n", sizeof ("\r\n")-1);

		if (0 < request->debug) {
			syslog(LOG_DEBUG, "%s > %lu:%s", request->id_log, BufLength(req)-offset, BufBytes(req)+offset);
			offset = BufLength(req);
		}
	}

	if (request->credentials != NULL) {
		(void) BufAddString(req, "Authorization: ");
		(void) BufAddString(req, request->credentials);
		(void) BufAddBytes(req, (unsigned char *) "\r\n", sizeof ("\r\n")-1);

		if (0 < request->debug) {
			syslog(LOG_DEBUG, "%s > %lu:%s", request->id_log, BufLength(req)-offset, BufBytes(req)+offset);
			offset = BufLength(req);
		}
	}

	if (request->if_modified_since != 0) {
		if (gmtime_r(&request->if_modified_since, &gmt) == NULL)
			goto error1;
		length = strftime(stamp, sizeof (stamp), "%a, %d %b %Y %H:%M:%S GMT", &gmt);

		(void) BufAddString(req, "If-Modified-Since: ");
		(void) BufAddBytes(req, (unsigned char *) stamp, length);
		(void) BufAddBytes(req, (unsigned char *) "\r\n", sizeof ("\r\n")-1);

		if (0 < request->debug) {
			syslog(LOG_DEBUG, "%s > %lu:%s", request->id_log, BufLength(req)-offset, BufBytes(req)+offset);
			offset = BufLength(req);
		}
	}

	if (0 < request->content_length) {
		(void) snprintf(stamp, sizeof (stamp), "%lu", (unsigned long) request->content_length);
		(void) BufAddString(req, "Content-Length: ");
		(void) BufAddString(req, stamp);
		(void) BufAddBytes(req, (unsigned char *) "\r\n", sizeof ("\r\n")-1);

		if (0 < request->debug) {
			syslog(LOG_DEBUG, "%s > %lu:%s", request->id_log, BufLength(req)-offset, BufBytes(req)+offset);
			offset = BufLength(req);
		}
	}

	/* End of headers. */
	(void) BufAddBytes(req, (unsigned char *) "\r\n", sizeof ("\r\n")-1);

	if (0 < request->debug) {
		syslog(LOG_DEBUG, "%s > %lu:%s", request->id_log, BufLength(req)-offset, BufBytes(req)+offset);
		offset = BufLength(req);
	}

	/* Open connection to web server. */
	if ((socket = socket3_connect(request->url->host, uriGetSchemePort(request->url), request->timeout)) < 0)
		goto error1;

	(void) fileSetCloseOnExec(socket, 1);
	(void) socket3_set_linger(socket, 0);
	(void) socket3_set_nonblocking(socket, 1);

	if (socket3_write(socket, BufBytes(req), BufLength(req), NULL) != BufLength(req))
		goto error2;

	if (request->post_buffer != NULL) {
		if (0 < request->debug)
			syslog(LOG_DEBUG, "%s > (%lu bytes sent)", request->id_log, (unsigned long) request->post_size);

		if (socket3_write(socket, request->post_buffer, request->post_size, NULL) != request->post_size)
			goto error2;
	}

	BufDestroy(req);

	return socket;
error2:
	socket3_close(socket);
error1:
	BufDestroy(req);
error0:
	return SOCKET_ERROR;
}

static
PT_THREAD(http_read(pt_t *pt, SOCKET socket, long ms, Buf *buf))
{
	int rc;
	long length;
	unsigned char line[HTTP_LINE_SIZE];

	PT_BEGIN(pt);

	PT_WAIT_UNTIL(pt, (rc = socket3_has_input(socket, ms)) == 0 || rc != EINTR);

	if (rc != 0) {
		if (0 < httpDebug)
			syslog(LOG_DEBUG, "%s.%d rc=%d %s", __FUNCTION__, __LINE__, rc, strerror(rc));
		PT_EXIT(pt);
	}

	length = socket3_read(socket, line, sizeof (line), NULL);
	if (length == 0)
		PT_EXIT(pt);
	if (length < 0) {
		if (0 < httpDebug)
			syslog(LOG_DEBUG, "%s.%d errno=%d %s", __FUNCTION__, __LINE__, errno, strerror(errno));
		PT_EXIT(pt);
	}
	line[length] = '\0';

	if (BufAddBytes(buf, line, length)) {
		if (0 < httpDebug)
			syslog(LOG_DEBUG, "%s.%d errno=%d %s", __FUNCTION__, __LINE__, errno, strerror(errno));
		PT_EXIT(pt);
	}

	PT_END(pt);
}

PT_THREAD(httpReadPt(HttpResponse *response))
{
	Buf *buf;
	int span, is_crlf;

	if (socket < 0)
		goto error0;
	if (response == NULL)
		goto error1;

	buf = response->content;

	PT_BEGIN(&response->pt);

	BufSetLength(buf, 0);

	/* Read HTTP response line. */
	PT_SPAWN(&response->pt, &response->pt_read, http_read(&response->pt_read, response->socket, response->timeout, buf));

	span = 0;
	response->result = HTTP_INTERNAL;

	/*** Note that response->result is an "enum HttpCode *" and enum
	 *** types are equivalent to an int (ANSI C89). Therefore we can
	 *** ignore compiler warning from gcc 3+:
	 ***
	 *** warning: dereferencing type-prunned pointer will break strict-aliasing rules
	 ***
	 *** Removing the cast to fix the warning generates a different
	 *** compiler warning seen in gcc 2+:
	 ***
	 *** warning: format '%d' expects type 'int *', but argument 3 has type 'enum HttpCode *'
	 ***
	 *** So either way gcc complains about something that it should
	 *** be able to understand and ignore.
	 ***/
	(void) sscanf((char *) buf->bytes, "HTTP/%*s %d %*[^\r\n] %n", (int *) &response->result, &span);

	if (0 < response->debug)
		syslog(LOG_DEBUG, "%s http-code=%d", response->id_log, response->result);
	if (1 < response->debug)
		syslog(LOG_DEBUG, "%s < %d:%.*s", response->id_log, span, span, buf->bytes);

	if (response->hook.status != NULL
	&& (*response->hook.status)(response, buf->bytes, span) != HTTP_CONTINUE)
		goto error1;

	/* Read HTTP response headers. */
	for (buf->offset = span; ; buf->offset += span) {
		if (buf->length <= buf->offset) {
			PT_WAIT_THREAD(&response->pt, http_read(&response->pt_read, response->socket, response->timeout, buf));
			if (errno != 0) {
				if (0 < response->debug)
					syslog(LOG_DEBUG, "%s %s.%d errno=%d %s", response->id_log, __FUNCTION__, __LINE__, errno, strerror(errno));
				goto error1;
			}
			/* EOF? */
			if (buf->length <= buf->offset)
				break;
		}

		/* Find end of header line. */
		for (span = 0; ; span++) {
			span += strcspn((char *)buf->bytes+buf->offset+span, "\n");
			if (buf->length <= buf->offset+span)
				break;
			if (!isblank(buf->bytes[buf->offset+span+1]))
				break;
		}

		/* Stopped on a LF and not a NUL? */
		if ((is_crlf  = (buf->bytes[buf->offset+span] == '\n'))) {
			/* Is it a CRLF pair? */
			is_crlf += (0 < span && buf->bytes[buf->offset+span-1] == '\r');

			/* Add the LF to the span. */
			span++;
		}

		if (1 < response->debug)
			syslog(LOG_DEBUG, "%s < %d:%.*s", response->id_log, span, span, buf->bytes+buf->offset);

		/* End-of-headers found? */
		if (span == is_crlf) {
			buf->offset += span;
			break;
		}

		if (response->hook.header != NULL
		&& (*response->hook.header)(response, buf->bytes+buf->offset, span) != HTTP_CONTINUE)
			goto error1;
	}

	response->eoh = buf->offset;
	if (0 < response->debug)
		syslog(LOG_DEBUG, "%s eoh=%u", response->id_log, (unsigned) response->eoh);

	if (response->hook.header_end != NULL
	&& (*response->hook.header_end)(response, buf->bytes, response->eoh) != HTTP_CONTINUE)
		goto error1;

	/* Read HTTP body content. */
	for ( ; ; buf->offset += span) {
		if (buf->length <= buf->offset) {
			PT_WAIT_THREAD(&response->pt, http_read(&response->pt_read, response->socket, response->timeout, buf));

			/* EOF? */
			if (buf->length <= buf->offset)
				break;
		}

		/* Find end of line. */
		span = strcspn((char *)buf->bytes+buf->offset, "\n");

		/* Stopped on a LF and not a NUL? */
		span += (buf->bytes[buf->offset+span] == '\n');

		if (1 < response->debug)
			syslog(LOG_DEBUG, "%s < %d:%.*s", response->id_log, span, span, buf->bytes+buf->offset);

		if (response->hook.body != NULL
		&& (*response->hook.body)(response, buf->bytes+buf->offset, span) != HTTP_CONTINUE)
			goto error1;
	}

	if (0 < response->debug)
		syslog(LOG_DEBUG, "%s content-length=%lu", response->id_log, (unsigned long) buf->length-response->eoh);

	if (response->hook.body_end != NULL
	&& (*response->hook.body_end)(response, buf->bytes+response->eoh, buf->length-response->eoh) != HTTP_CONTINUE)
		goto error1;
error1:
	socket3_close(response->socket);
error0:
	PT_END(&response->pt);
}

HttpCode
httpRead(HttpResponse *response)
{
	PT_INIT(&response->pt);
	while (PT_SCHEDULE(httpReadPt(response)))
		;
	return response->result;
}

HttpCode
httpDo(const char *method, const char *url, time_t modified_since, unsigned char *post, size_t size, HttpResponse *response)
{
	HttpRequest request;

	memset(&request, 0, sizeof (request));

	if ((request.url = uriParse(url, -1)) == NULL)
		return HTTP_INTERNAL;

	request.debug = httpDebug;
	request.method = method;
	request.timeout = HTTP_TIMEOUT_MS;
	request.if_modified_since = modified_since;
	request.post_buffer = post;
	request.post_size = size;
	request.id_log = response->id_log;

	response->socket = httpSend(&request);
	response->timeout = HTTP_TIMEOUT_MS;
	response->url = strdup(url);
	free(request.url);

	return httpRead(response);
}

HttpCode
httpDoHead(const char *url, time_t modified_since, HttpResponse *response)
{
	return httpDo("HEAD", url, modified_since, NULL, 0, response);
}

HttpCode
httpDoGet(const char *url, time_t modified_since, HttpResponse *response)
{
	return httpDo("GET", url, modified_since, NULL, 0, response);
}

HttpCode
httpDoPost(const char *url, time_t modified_since, unsigned char *post, size_t size, HttpResponse *response)
{
	return httpDo("POST", url, modified_since, post, size, response);
}

/***********************************************************************
 ***
 ***********************************************************************/

#ifdef TEST

#include <stdio.h>
#include <com/snert/lib/sys/sysexits.h>
#include <com/snert/lib/util/getopt.h>
#include <com/snert/lib/util/md5.h>

int body_only;
time_t if_modified_since;
const char *http_method = "GET";

const char usage[] =
"usage: geturl [-bhmv][-s seconds] url ...\n"
"\n"
"-b\t\toutput body only\n"
"-h\t\toutput headers only; perform a HEAD request instead of GET\n"
"-m\t\tgenerate MD5 hash of returned content\n"
"-s seconds\tcheck if modified since timestamp seconds\n"
"-v\t\tverbose logging to standard output\n"
"\n"
LIBSNERT_COPYRIGHT "\n"
;

#if ! defined(__MINGW32__)
#undef syslog
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

typedef void (*get_url_fn)(const char *);

void
get_url(const char *url)
{
	HttpContent content;

	httpContentInit(&content);

	if (httpDo(http_method, url, if_modified_since, NULL, 0, &content.response) == HTTP_INTERNAL) {
		fprintf(stderr, "%s: %d internal error\n", url, HTTP_INTERNAL);
		return;
	}

	fputs((char *) BufBytes(content.response.content)+(body_only ? content.response.eoh : 0), stdout);

	httpContentFree(&content);
}

HttpCode
response_body_end(HttpResponse *response, unsigned char *input, size_t length)
{
	md5_append((md5_state_t *) response->data, input, length);

	return HTTP_CONTINUE;
}

void
get_url_md5(const char *url)
{
	md5_state_t md5;
	HttpResponse response;
	char digest_string[33];
	unsigned char digest[16];

	httpResponseInit(&response);

	md5_init(&md5);
	response.data = &md5;
	response.hook.body_end = response_body_end;

	if (httpDo(http_method, url, if_modified_since, NULL, 0, &response) == HTTP_INTERNAL) {
		fprintf(stderr, "%s: %d internal error\n", url, HTTP_INTERNAL);
		return;
	}

	md5_finish(&md5, digest);
	md5_digest_to_string(digest, digest_string);
	printf("%s %lu %s\n", digest_string, (unsigned long) response.content->length-response.eoh, url);

	httpResponseFree(&response);
}

int
main(int argc, char **argv)
{
	int argi, ch;
	get_url_fn get_fn = get_url;

	while ((ch = getopt(argc, argv, "bhmvs:")) != -1) {
		switch (ch) {
		case 'b':
			body_only = 1;
			break;

		case 'h':
			http_method = "HEAD";
			break;

		case 'm':
			get_fn = get_url_md5;
			break;

		case 's':
			if_modified_since = (time_t) strtol(optarg, NULL, 10);
			break;

		case 'v':
#ifdef VAR_LOG_DEBUG
			openlog("geturl", LOG_PID, LOG_USER);
#else
			LogOpen("(standard error)");
			LogSetProgramName("geturl");
#endif
			httpSetDebug(2);
			break;

		default:
			fputs(usage, stderr);
			return EX_USAGE;
		}
	}

	if (argc < optind + 1) {
		fputs(usage, stderr);
		return EX_USAGE;
	}

	/* If both -h and -b given, then return both headers and body. */
	if (body_only && *http_method == 'H') {
		body_only = 0;
		http_method = "GET";
	}

	if (socket3_init())
		exit(EX_SOFTWARE);

	for (argi = optind; argi < argc; argi++) {
		(*get_fn)(argv[argi]);
	}

	return EXIT_SUCCESS;
}
#endif /* TEST */
