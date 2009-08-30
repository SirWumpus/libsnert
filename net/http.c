/*
 * http.c
 *
 * RFC 2616 HTTP/1.1 Support Functions
 *
 * Copyright 2009 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <stdlib.h>
#include <string.h>

#if defined(HAVE_SYSLOG_H) && ! defined(__MINGW32__)
# include <syslog.h>
#endif

#include <com/snert/lib/io/Log.h>
#include <com/snert/lib/net/http.h>
#include <com/snert/lib/util/Text.h>
#include <com/snert/lib/util/convertDate.h>

/***********************************************************************
 ***
 ***********************************************************************/

static int httpDebug;

void
httpSetDebug(int level)
{
	httpDebug = level;
}

void
httpResponseInit(HttpResponse *response)
{
	memset(response, 0, sizeof (*response));
}

void
httpResponseFree(HttpResponse *response)
{
	if (response != NULL) {
		BufDestroy(response->content);
		free(response->content_type);
		free(response->content_encoding);
	}
}

Socket2 *
httpSend(HttpRequest *request)
{
	Buf *req;
	struct tm gmt;
	char stamp[40];
	Socket2 *socket;
	long length, offset = 0;

	if (request == NULL)
		goto error0;

	if ((req = BufCreate(HTTP_BUFFER_SIZE)) == NULL)
		goto error0;

	/* Build the request buffer. */
	(void) BufAddString(req, request->method);
	(void) BufAddByte(req, ' ');
	(void) BufAddString(req, request->url->path);
	(void) BufAddString(req, " HTTP/1.0\r\n");

	if (0 < httpDebug) {
		syslog(LOG_DEBUG, "> %lu:%s", BufLength(req)-offset, BufBytes(req)+offset);
		offset = BufLength(req);
	}

	(void) BufAddString(req, "Host: ");
	(void) BufAddString(req, request->url->host);

	if (request->url->port != NULL) {
		(void) BufAddByte(req, ':');
		(void) BufAddString(req, request->url->port);
	}
	(void) BufAddBytes(req, (unsigned char *) "\r\n", sizeof ("\r\n")-1);

	if (0 < httpDebug) {
		syslog(LOG_DEBUG, "> %lu:%s", BufLength(req)-offset, BufBytes(req)+offset);
		offset = BufLength(req);
	}

	if (request->from != NULL) {
		(void) BufAddString(req, "From: ");
		(void) BufAddString(req, request->from);
		(void) BufAddBytes(req, (unsigned char *) "\r\n", sizeof ("\r\n")-1);

		if (0 < httpDebug) {
			syslog(LOG_DEBUG, "> %lu:%s", BufLength(req)-offset, BufBytes(req)+offset);
			offset = BufLength(req);
		}
	}

	if (request->accept_language != NULL) {
		(void) BufAddString(req, "Accept-Language: ");
		(void) BufAddString(req, request->accept_language);
		(void) BufAddBytes(req, (unsigned char *) "\r\n", sizeof ("\r\n")-1);

		if (0 < httpDebug) {
			syslog(LOG_DEBUG, "> %lu:%s", BufLength(req)-offset, BufBytes(req)+offset);
			offset = BufLength(req);
		}
	}

	if (request->credentials != NULL) {
		(void) BufAddString(req, "Authorization: ");
		(void) BufAddString(req, request->credentials);
		(void) BufAddBytes(req, (unsigned char *) "\r\n", sizeof ("\r\n")-1);

		if (0 < httpDebug) {
			syslog(LOG_DEBUG, "> %lu:%s", BufLength(req)-offset, BufBytes(req)+offset);
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

		if (0 < httpDebug) {
			syslog(LOG_DEBUG, "> %lu:%s", BufLength(req)-offset, BufBytes(req)+offset);
			offset = BufLength(req);
		}
	}

	if (request->content_length != NULL) {
		(void) snprintf(stamp, sizeof (stamp), "%lu", request->content_length);
		(void) BufAddString(req, "Content-Length: ");
		(void) BufAddString(req, stamp);
		(void) BufAddBytes(req, (unsigned char *) "\r\n", sizeof ("\r\n")-1);

		if (0 < httpDebug) {
			syslog(LOG_DEBUG, "> %lu:%s", BufLength(req)-offset, BufBytes(req)+offset);
			offset = BufLength(req);
		}
	}

	/* End of headers. */
	(void) BufAddBytes(req, (unsigned char *) "\r\n", sizeof ("\r\n")-1);

	if (0 < httpDebug) {
		syslog(LOG_DEBUG, "> %lu:%s", BufLength(req)-offset, BufBytes(req)+offset);
		offset = BufLength(req);
	}

	/* Open connection to web server. */
	if (socketOpenClient(request->url->host, HTTP_PORT, request->timeout, NULL, &socket) == SOCKET_ERROR)
		goto error1;

	socketSetTimeout(socket, request->timeout);

	if (socketWrite(socket, BufBytes(req), BufLength(req)) != BufLength(req))
		goto error2;

	if (request->post_buffer != NULL) {
		if (0 < httpDebug)
			syslog(LOG_DEBUG, "> (%lu bytes sent)", (unsigned long) request->post_size);

		if (socketWrite(socket, request->post_buffer, request->post_size) != request->post_size)
			goto error2;
	}

	BufDestroy(req);

	return socket;
error2:
	socketClose(socket);
error1:
	BufDestroy(req);
error0:
	return NULL;
}

static long
httpReadLine(Socket2 *socket, Buf *buf)
{
	long length;
	size_t offset;
	unsigned char line[HTTP_LINE_SIZE];

	offset = BufLength(buf);

	do {
		length = socketReadLine2(socket, (char *) line, sizeof (line), 1);
		if (length < 0)
			return length;

		if (0 < httpDebug)
			syslog(LOG_DEBUG, "< %ld:%s", length, line);

		if (BufAddBytes(buf, line, length))
			return SOCKET_ERROR;
	} while (length == sizeof (line));

	return offset;
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

HttpCode
httpRead(Socket2 *socket, HttpResponse *response)
{
	Buf *buf;
	char *string;
	size_t offset;
	HttpCode code = HTTP_INTERNAL;

	if (socket == NULL)
		goto error0;

	if (response == NULL)
		goto error1;

	if (response->content == NULL
	&& (response->content = BufCreate(HTTP_BUFFER_SIZE)) == NULL)
		goto error1;
	buf = response->content;

	/* Read HTTP response line and headers. */
	BufSetLength(buf, 0);

	do {
		if ((offset = httpReadLine(socket, buf)) < 0)
			goto error1;
	} while (buf->length - offset != 2 || buf->bytes[offset] != '\r' || buf->bytes[offset+1] != '\n');

	/* Parse headers. */
	if (sscanf((char *) buf->bytes, "HTTP/%*s %u", (unsigned int *) &code) != 1)
		goto error1;
	response->result = code;

	if (0 < httpDebug)
		syslog(LOG_DEBUG, "http-code=%d", code);

	response->expires = 0;
	response->last_modified = 0;
	response->content_length = 0;
	response->content_type = httpGetHeader(buf, "*Content-Type:*", sizeof ("Content-Type:")-1);
	response->content_encoding = httpGetHeader(buf, "*Content-Encoding:*", sizeof ("Content-Encoding:")-1);

	if (httpDebug && response->content_type != NULL)
		syslog(LOG_DEBUG, "content-type=%s", response->content_type);
	if (httpDebug && response->content_encoding != NULL)
		syslog(LOG_DEBUG, "content-encoding=%s", response->content_encoding);

	if ((string = httpGetHeader(buf, "*Content-Length:*", sizeof ("Content-Length:")-1)) != NULL) {
		response->content_length = strtol(string, NULL, 10);
		if (0 < httpDebug)
			syslog(LOG_DEBUG, "content-length=%s", string);
		free(string);
	}

	if ((string = httpGetHeader(buf, "*Last-Modified:*", sizeof ("Last-Modified:")-1)) != NULL) {
		/* NOTE that this is in GMT. */
		(void) convertDate(string, &response->last_modified, NULL);
		if (0 < httpDebug)
			syslog(LOG_DEBUG, "last-modified=%s", string);
		free(string);
	}

	if ((string = httpGetHeader(buf, "*Expires:*", sizeof ("Expires:")-1)) != NULL) {
		/* NOTE that this is in GMT. */
		(void) convertDate(string, &response->expires, NULL);
		if (0 < httpDebug)
			syslog(LOG_DEBUG, "expires=%s", string);
		free(string);
	}

	if ((string = httpGetHeader(buf, "*Date:*", sizeof ("Date:")-1)) != NULL) {
		/* NOTE that this is in GMT. */
		(void) convertDate(string, &response->date, NULL);
		if (0 < httpDebug)
			syslog(LOG_DEBUG, "date=%s", string);
		free(string);
	}

	/* Read body content. */
	while (0 < httpReadLine(socket, buf))
		;
error1:
	socketClose(socket);
error0:
	return code;
}

HttpCode
httpDo(const char *method, const char *url, time_t modified_since, unsigned char *post, size_t size, HttpResponse *response)
{
	Socket2 *socket;
	HttpRequest request;

	memset(&request, 0, sizeof (request));

	if ((request.url = uriParse(url, -1)) == NULL)
		return HTTP_INTERNAL;

	request.method = method;
	request.timeout = HTTP_TIMEOUT_MS;
	request.if_modified_since = modified_since;
	request.post_buffer = post;
	request.post_size = size;

	socket = httpSend(&request);
	free(request.url);

	return httpRead(socket, response);
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

time_t if_modified_since;
const char *http_method = "GET";

const char usage[] =
"usage: geturl [-hv][-s seconds] url ...\n"
"\n"
"-h\t\tperform a HEAD request instead of GET\n"
"-s seconds\tcheck if modified since timestamp seconds\n"
"-v\t\tverbose logging to standard output\n"
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

void
getURL(const char *url)
{
	HttpResponse response;

	httpResponseInit(&response);

	if (httpDo(http_method, url, if_modified_since, NULL, 0, &response) == HTTP_INTERNAL) {
		fprintf(stderr, "%s: %d internal error\n", url, HTTP_INTERNAL);
		return;
	}

	fputs(BufBytes(response.content), stdout);

	httpResponseFree(&response);
}

int
main(int argc, char **argv)
{
	int argi, ch;

	while ((ch = getopt(argc, argv, "hvs:")) != -1) {
		switch (ch) {
		case 'h':
			http_method = "HEAD";
			break;

		case 's':
			if_modified_since = (time_t) strtol(optarg, NULL, 10);
			break;

		case 'v':
#ifdef VAR_LOG_DEBUG
			openlog("geturl", LOG_PID, LOG_USER);
			setlogmask(LOG_UPTO(LOG_DEBUG));
#else
			LogOpen("(standard error)");
			LogSetProgramName("geturl");
			LogSetLevel(LOG_DEBUG);
#endif
			httpSetDebug(1);
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

	for (argi = optind; argi < argc; argi++) {
		getURL(argv[argi]);
	}

	return EXIT_SUCCESS;
}
#endif /* TEST */
