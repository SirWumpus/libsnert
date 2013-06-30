/*
 * cgi.c
 *
 * RFC 3875 The Common Gateway Interface (CGI) Version 1.1
 *
 * Copyright 2004, 2013 by Anthony Howe. All rights reserved.
 */

#ifndef CGI_CHUNK_SIZE
#define CGI_CHUNK_SIZE		(64 * 1024)
#endif

#ifndef CGI_SEGMENT_SIZE
#define CGI_SEGMENT_SIZE	128
#endif

#include <com/snert/lib/version.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include <com/snert/lib/util/cgi.h>

#ifdef DEBUG_MALLOC
# include <com/snert/lib/util/DebugMalloc.h>
#endif
extern char **environ;

/***********************************************************************
 *** CGI Support Routines
 ***********************************************************************/

static const char empty[] = "";
static const char localhost[] = "127.0.0.1";

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
void
cgiUrlDecode(char **tp, const char **sp)
{
	int hex;
	char *t;
	const char *s;

	for (t = *tp, s = *sp; *s != '\0'; t++, s++) {
		switch (*s) {
		case '=':
		case '&':
			s++;
			break;
		case '+':
			*t = ' ';
			continue;
		case '%':
			if (sscanf(s+1, "%2x", &hex) == 1) {
				*t = (char) hex;
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

	/* Pass back the next unprocessed location.
	 * For the source '\0' byte, we stop on that.
	 */
	*tp = t+1;
	*sp = s;
}

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
CgiMap *
cgiParseForm(const char *urlencoded)
{
	char *t;
	int nfields, i;
	const char *s;
	CgiMap *out;

	if (urlencoded == NULL)
		return NULL;

	nfields = 1;
	for (s = urlencoded; *s != '\0'; s++) {
		if (*s == '&')
			nfields++;
	}

	if ((out = malloc((nfields + 1) * sizeof (*out) + strlen(urlencoded) + 1)) == NULL)
		return NULL;

	s = urlencoded;
	t = (char *) &out[nfields+1];

	for (i = 0; i < nfields; i++) {
		out[i].name = t;
		cgiUrlDecode(&t, &s);

		out[i].value = t;
		if (s[-1] == '=')
			cgiUrlDecode(&t, &s);
		else
			*t++ = '\0';
	}

	out[i].name = NULL;
	out[i].value = NULL;

	return out;
}

static int
hdrncmp(const void *xp, void const *yp, long len)
{
	int diff;
	char *x = (char *) xp;
	char *y = (char *) yp;

	if (x == NULL && y != NULL)
		return 1;
	if (x != NULL && y == NULL)
		return -1;
	if (x == NULL && y == NULL)
		return 0;

	for ( ; *x != '\0' && *y != '\0'; ++x, ++y) {
		if (0 <= len && len-- == 0)
			return 0;

		/* Treat hypen and underscore as equivalent. Allows
		 * for header and environment variable equivalence.
		 */
		if ((*x == '-' && *y == '_') || (*x == '_' && *y == '-'))
			continue;

		if (*x != *y) {
			diff = tolower(*x) - tolower(*y);
			if (diff != 0)
				return diff;
		}
	}

	if (len == 0)
		return 0;

	return *x - *y;
}

void
cgiMapFree(CgiMap *table)
{
	CgiMap *item;

	if (table != NULL) {
		for (item = table; item->name != NULL; item++) {
			free(item->value);
			free(item->name);
		}
		free(table);
	}
}

int
cgiMapAdd(CgiMap **table, const char *name, const char *fmt, ...)
{
	int length;
	va_list args;
	char buffer[1000];
	CgiMap *item;

	length = 0;
	if (*table != NULL) {
		for (item = *table; item->name != NULL; item++)
			length++;
	}

	if ((item = realloc(*table, (length+2) * sizeof (*item))) == NULL)
		return -1;

	va_start(args, fmt);
	(void) vsnprintf(buffer, sizeof (buffer), fmt, args);
	va_end(args);

	*table = item;
	item[length].name = strdup(name);
	item[length].value = strdup(buffer);

	length++;
	item[length].name = NULL;
	item[length].value = NULL;

	return 0;
}

int
cgiMapFind(CgiMap *map, char *prefix)
{
	int i;
	long plength = strlen(prefix);

	if (map == NULL)
		return -1;

	for (i = 0; map[i].name != NULL; i++) {
		if (hdrncmp(map[i].name, prefix, plength) == 0)
			return i;
	}

	return -1;
}

void
cgiSendChunkV(CGI *cgi, const char *fmt, va_list args)
{
	int length;
	va_list args2;

	va_copy(args2, args);
	length = vsnprintf(NULL, 0, fmt, args2);
	(void) fprintf(cgi->out, "%X\r\n", length);
	(void) vfprintf(cgi->out, fmt, args);
	(void) fprintf(cgi->out, "\r\n");
}

void
cgiSendChunk(CGI *cgi, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	cgiSendChunkV(cgi, fmt, args);
	va_end(args);
}

void
cgiSendChunkEnd(CGI *cgi)
{
	if (!cgi->reply_close)
		(void) fprintf(cgi->out, "0\r\n\r\n");
}

void
cgiSendV(CGI *cgi, HttpCode code, const char *response, const char *fmt, va_list args)
{
	CgiMap *hdr;
	va_list args2;
	int length = 0, no_content_type = 1, no_content_length = 1;

	cgi->reply_status = code;
	(void) fprintf(cgi->out, "%s %d %s\r\n", cgi->is_nph ? "HTTP/1.1" : "Status:", code, response);

	if (cgi->reply_headers != NULL) {
		for (hdr = cgi->reply_headers; hdr->name != NULL; hdr++)
			(void) fprintf(cgi->out, "%s: %s\r\n", hdr->name, hdr->value);
		no_content_type = cgiMapFind(cgi->reply_headers, "Content-Type") < 0;
		no_content_length = cgiMapFind(cgi->reply_headers, "Content-Length") < 0
			&& cgiMapFind(cgi->reply_headers, "Transfer-Encoding") < 0;
	}

	if (fmt != NULL) {
		if (no_content_type)
			(void) fprintf(cgi->out, "Content-Type: text/plain\r\n");
		if (no_content_length && *fmt != '\0') {
			va_copy(args2, args);
//			length  = snprintf(NULL, 0, "%s (%d)\r\n", response, code);
			length += vsnprintf(NULL, 0, fmt, args2);

			if (cgi->reply_close)
				(void) fprintf(cgi->out, "Content-Length: %d\r\n", length);
			else
				(void) fprintf(cgi->out, "Transfer-Encoding: chunked\r\n");
		}
	}

	(void) fprintf(cgi->out, "Connection: %s\r\n", cgi->reply_close ? "close" : "persistent");
	(void) fprintf(cgi->out, "\r\n");

	if (fmt != NULL && *fmt != '\0') {
		if (!cgi->reply_close)
			(void) fprintf(cgi->out, "%X\r\n", length);
//		(void) fprintf(cgi->out, "%s (%d)\r\n", response, code);
		(void) vfprintf(cgi->out, fmt, args);
		(void) fprintf(cgi->out, "\r\n");
	}
}

void
cgiSend(CGI *cgi, HttpCode code, const char *response, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	cgiSendV(cgi, code, response, fmt, args);
	va_end(args);
}

void
cgiSendOk(CGI *cgi, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	cgiSendV(cgi, HTTP_OK, "OK", fmt, args);
	va_end(args);
}

void
cgiSendNoContent(CGI *cgi)
{
	cgiSendV(cgi, HTTP_NO_CONTENT, "No Content", NULL, NULL);
}

void
cgiSendSeeOther(CGI *cgi, const char *url, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	cgiMapAdd(&cgi->reply_headers, "Location", "%s", url);
	cgiSendV(cgi, HTTP_SEE_OTHER, "See Other", fmt, args);
	cgiSendChunkEnd(cgi);
	va_end(args);
}

void
cgiSendBadRequest(CGI *cgi, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	cgiSendV(cgi, HTTP_BAD_REQUEST, "Bad Request", fmt, args);
	cgiSendChunkEnd(cgi);
	va_end(args);
}

void
cgiSendNotFound(CGI *cgi, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	cgiSendV(cgi, HTTP_NOT_FOUND, "Not Found", fmt, args);
	cgiSendChunkEnd(cgi);
	va_end(args);
}

void
cgiSendInternalServerError(CGI *cgi, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	cgiSendV(cgi, HTTP_INTERNAL, "Internal Server Error", fmt, args);
	cgiSendChunkEnd(cgi);
	va_end(args);
}

int
cgiSetOptions(CGI *cgi, CgiMap *array, Option *table[])
{
	int argi;
	Option **opt, *o;

	for (opt = table; *opt != NULL; opt++) {
		o = *opt;
		if (*o->name != '\0' && 0 <= (argi = cgiMapFind(array, (char *) o->name))) {
			if (o->initial != o->string)
				free(o->string);
			o->string = strdup(array[argi].value);
		}
	}

	return 0;
}

void
cgiFree(CGI *cgi)
{
	if (cgi != NULL) {
		if (cgi->out != NULL && cgi->out != stdout)
			fclose(cgi->out);
		BufDestroy(cgi->_RAW);
		free(cgi->reply_headers);
		free(cgi->_HTTP);
		free(cgi->_POST);
		free(cgi->_GET);
	}
}

int
cgiInit(CGI *cgi)
{
	size_t size;
	int i, count;
	char **e, *t;
	const char *s;

	memset(cgi, 0, sizeof (*cgi));

	cgi->out = stdout;
	cgi->reply_close = 1;
	cgi->request_close = 1;
	cgi->content_type = getenv("CONTENT_TYPE");
	cgi->content_length = getenv("CONTENT_LENGTH");
	cgi->document_root = getenv("DOCUMENT_ROOT");
	cgi->path_info = getenv("PATH_INFO");
	cgi->path_translated = getenv("PATH_TRANSLATED");
	cgi->query_string = getenv("QUERY_STRING");
	cgi->remote_addr = getenv("REMOTE_ADDR");
	cgi->request_uri = getenv("REQUEST_URI");
	cgi->request_method = getenv("REQUEST_METHOD");
	cgi->script_name = getenv("SCRIPT_NAME");
	cgi->script_filename = getenv("SCRIPT_FILENAME");
	cgi->server_name = getenv("SERVER_NAME");
	cgi->server_port = getenv("SERVER_PORT");
	cgi->server_protocol = getenv("SERVER_PROTOCOL");

	s = getenv("SERVER_SOFTWARE");
	cgi->is_nph = cgi->script_name == NULL || strstr(cgi->script_name, "nph-") != NULL
		|| (s != NULL && strstr(s, "hibachi") != NULL);

	if (cgi->document_root != NULL && chdir(cgi->document_root))
		goto error0;

	if (cgi->remote_addr == NULL)
		cgi->remote_addr = localhost;

	if (cgi->server_name == NULL)
		cgi->server_name = localhost;

	if (cgi->server_port == NULL)
		cgi->server_port = "80";

	cgi->port = strtol(cgi->server_port, NULL, 10);

	cgi->_GET = cgiParseForm(cgi->query_string);

	if (cgi->request_method != NULL && strcmp(cgi->request_method, "POST") == 0) {
		ssize_t content_length, length, n;

		if (cgi->content_length == NULL) {
			cgiSendBadRequest(cgi, "Missing content.\r\n");
			goto error1;
		}

		content_length = strtol(cgi->content_length, NULL, 10);

		if ((cgi->_RAW = BufCreate(content_length+1)) == NULL) {
			cgiSendInternalServerError(cgi, "Out of memory.\r\n");
			goto error1;
		}

		for (length = 0; length < content_length; length += n) {
			if ((n = read(STDIN_FILENO, BufBytes(cgi->_RAW)+length, (size_t)(content_length-length))) < 0) {
				cgiSendInternalServerError(cgi, "Content read error.\r\ncontent-length=%ld length=%ld n=%ld\r\n%s (%d)\r\n", content_length, length, n, strerror(errno), errno);
				goto error2;
			}
		}
		BufSetLength(cgi->_RAW, length);
		BufAddByte(cgi->_RAW, '\0');

		if (cgi->content_type != NULL
		&& strcmp(cgi->content_type, "application/x-www-form-urlencoded") == 0
		&& (cgi->_POST = cgiParseForm((const char *) BufBytes(cgi->_RAW))) == NULL) {
			cgiSendInternalServerError(cgi, "Form POST parse error.\r\n");
			goto error2;
		}
	}

	/* Count all the HTTP_* environment variables. */
	count = size = 0;
	for (e = environ; *e != NULL; e++) {
		if (strstr(*e, "HTTP_") != NULL) {
			size += strlen(*e)+1;
			count++;
		}
	}

	/* Allocate enough entries and string space. */
	if ((cgi->_HTTP = malloc((count+1) * sizeof (*cgi->_HTTP) + size)) == NULL) {
		cgiSendInternalServerError(cgi, "HTTP environment parse error.\r\n");
		goto error3;
	}

	/* Copy the HTTP_* environment variables into _HTTP[]. */
	i = 0;
	t = (char *) &cgi->_HTTP[count+1];
	for (e = environ; *e != NULL; e++) {
		if (strstr(*e, "HTTP_") != NULL) {
			cgi->_HTTP[i].name = t;
			s = (const char *) *e + sizeof ("HTTP_")-1;
			cgiUrlDecode(&t, &s);

			cgi->_HTTP[i].value = t;
			if (s[-1] == '=')
				cgiUrlDecode(&t, &s);
			else
				*t++ = '\0';
			i++;
		}
	}
	cgi->_HTTP[i].name = cgi->_HTTP[i].value = NULL;

	return 0;
error3:
	free(cgi->_POST);
error2:
	BufDestroy(cgi->_RAW);
error1:
	free(cgi->_GET);
error0:
	return -1;
}

int
cgiReadHeader(Socket2 *client, Buf *input)
{
	char *hdr;
	long length;
	size_t offset;
	int ch, no_nl;

	/* Append to the current buffer. */
	offset = BufLength(input);

	/* Read a possibly folded header line. */
	do {
		/* Enlarge the buffer if less than a segment of space left. */
		if (BufSize(input) <= offset + CGI_SEGMENT_SIZE
		&&  BufSetSize(input, offset + CGI_CHUNK_SIZE))
			return -1;

		hdr = (char *) BufBytes(input) + offset;
		if ((length = socketReadLine2(client, hdr, BufSize(input)-offset, 1)) < 0)
			return -1;

		/* Advance to end of segment. */
		offset += length;

		/* Remove newline. */
		no_nl = 0;
		if (2 <= length && hdr[length-2] == '\r' && hdr[length-1] == '\n')
			offset -= 2;
		else if (1 <= length && hdr[length-1] == '\n')
			offset -= 1;
		else
			no_nl++;

		/* Repeat if the next line is a folded header line? */
	} while (no_nl || (ch = socketPeekByte(client)) == ' ' || ch == '\t');

	(void) BufSetLength(input, offset);

	/* Assert string terminator part of buffer data. */
	(void) BufAddByte(input, '\0');

	return 0;
}

int
cgiReadN(Socket2 *client, Buf *input, size_t expect)
{
	long n;
	size_t offset;

	/* Append to buffer. */
	offset = BufLength(input);

	if (BufSetLength(input, offset+expect))
		return -1;

	for ( ; offset < BufLength(input); offset += n) {
		if (!socketHasInput(client, socketGetTimeout(client)))
			return -1;
		if ((n = socketRead(client, BufBytes(input)+offset, BufLength(input)-offset)) < 0)
			return -1;
	}

	return 0;
}

/*
 * RFC 2616 section 3.6.1 Chunked Transfer Coding
 */
int
cgiReadChunk(Socket2 *client, Buf *input)
{
	size_t offset, length;

	/* We might be appending, so save current length. */
	offset = BufLength(input);

	/* Read hex length and any extension parameters. */
	if (cgiReadHeader(client, input))
		return -1;

	length = (size_t) strtol((char *)BufBytes(input), NULL, 16);

	/* Reset buffer length for reuse. */
	(void) BufSetLength(input, offset);

	/* Read and append data plus CRLF. */
	if (cgiReadN(client, input, length + 2))
		return -1;

	length += offset;

	/* Assert that the chunk ended with CRLF. */
	if (BufGetByte(input, length) != '\r' && BufGetByte(input, length+1) != '\n')
		return -1;

	/* Remove the CRLF. */
	(void) BufSetLength(input, length);

	return 0;
}

static int
isPrintableASCII(const char *s)
{
	for ( ; *s != '\0'; s++) {
		switch (*s) {
		case ASCII_CR:
			if (s[1] == ASCII_LF)
				continue;
			return 0;
		case ASCII_LF:
			if (s[1] == '\0')
				break;
			return 0;
		case ASCII_TAB:
			break;
		default:
			if (isprint(*s))
				break;
			return 0;
		}
	}

	return 1;
}

int
cgiReadRequest(CGI *cgi, Socket2 *client)
{
	size_t length;

	/* Read request line and headers. The buffer will contain
	 * a block of strings followed by an empty string.
	 */
	do {
		/* Start of next header. */
		length = BufLength(cgi->_RAW);

		if (cgiReadHeader(client, cgi->_RAW) < 0) {
			cgiSendInternalServerError(cgi, "Read error.\r\n%s %d: %s (%d)\r\n", __FILE__, __LINE__, strerror(errno), errno);
			return -1;
		}

		if (!isPrintableASCII((char *)BufBytes(cgi->_RAW)+length)) {
			cgiSendBadRequest(cgi, "Non-printable ASCII characters in header.\r\n");
			return -1;
		}
	} while (length+1 < BufLength(cgi->_RAW));

	return 0;
}

static int
cgiParseHeaders(CGI *cgi)
{
	char *s, *t;
	int nfields, i;
	CgiMap *out;

	/* Parse HTTP request line. */
	cgi->request_method = s = (char *)BufBytes(cgi->_RAW);
	if ((s = strchr(s, ' ')) == NULL) {
		cgiSendBadRequest(cgi, "Malformed HTTP request line.\r\n");
		return -1;
	}
	*s++ = '\0';

	cgi->request_uri = s;
	if ((s = strchr(s, ' ')) == NULL) {
		cgiSendBadRequest(cgi, "Malformed HTTP request line.\r\n");
		return -1;
	}
	*s++ = '\0';

	cgi->server_protocol = s;
	s += strlen(s)+1;

	if (strncmp(cgi->server_protocol, "HTTP/1.1", sizeof ("HTTP/1.1")-4) != 0) {
		cgiSendBadRequest(cgi, "Unknown protocol \"%s\".\r\n", cgi->server_protocol);
		return -1;
	}
	i = cgi->server_protocol[sizeof ("HTTP/1.1")-2];
	if (cgi->server_protocol[sizeof ("HTTP/1.1")-4] != '1' && i != '0' && i != '1') {
		cgiSend(
			cgi, HTTP_VERSION_NOT_SUPPORTED, "HTTP version not supported",
			"Version \"%s\" not support.\r\n", cgi->server_protocol
		);
		return -1;
	}

	/* Identify the method by testing just one character position of
	 * the method name:
	 *
	 * OPTIONS	O
	 * GET    	G
	 * HEAD   	H
	 * POST   	  S
	 * PUT    	 U
	 * DELETE 	D
	 * TRACE  	T
	 * CONNECT	C
	 */
	if (cgi->request_method[0] != 'G' && cgi->request_method[0] != 'H' && cgi->request_method[2] != 'S') {
		cgiSend(cgi, HTTP_NOT_IMPLEMENTED, "Method not implemented", NULL);
		return -1;
	}

	/* Count remaining HTTP request headers. */
	nfields = 0;
	for (t = s; *t != '\0'; t++) {
		t += strlen(t);
		nfields++;
	}

	if ((cgi->_HTTP = out = malloc((nfields + 2) * sizeof (*out))) == NULL) {
		cgiSendInternalServerError(cgi, "Out of memory.\r\n");
		return -1;
	}

	/* Create leading sentinel entry. */
	out->name = out->value = (char *) empty;
	out++;

	/* Parse HTTP request headers into a CgiMap[]. */
	for (i = 0; i < nfields; i++) {
		out[i].name = s;
		if ((s = strchr(s, ':')) == NULL) {
			cgiSendBadRequest(cgi, "Malformed header.\r\n");
			return -1;
		}
		*s++ = '\0';
		s += strspn(s, " \t");

		out[i].value = s;
		s += strlen(s)+1;
	}

	out[i].name = NULL;
	out[i].value = NULL;

	if (cgiMapFind(out, "Host") < 0) {
		cgiSendBadRequest(cgi, "Missing required Host: header.\r\n");
		return -1;
	}

	/* Collect some frequently referenced headers. */
	if ((cgi->query_string = strchr(cgi->request_uri, '?')) != NULL) {
		cgi->query_string++;
		cgi->_GET = cgiParseForm(cgi->query_string);
	}

	/* Note that if Content-Type and/or Content-Length are
	 * missing, then the empty string is used in their place.
	 * So when Content-Length is missing, then strlen(empty)
	 * is zero (0).
	 */
	cgi->content_type = out[cgiMapFind(out, "Content-Type")].value;
	cgi->content_length = out[cgiMapFind(out, "Content-Length")].value;

	cgi->request_close = strcmp(out[cgiMapFind(out, "Connection")].value, "persistent") != 0;

	return 0;
}

int
cgiReadInit(CGI *cgi, Socket2 *client)
{
	int i;
	size_t content_length;

	memset(cgi, 0, sizeof (*cgi));

	/* Assume HTTP/1.0 style one request per connection. */
	cgi->request_close = 1;
	cgi->reply_close = 1;

	/* Direct reading of HTTP request implies handling HTTP reply. */
	cgi->is_nph = 1;

	if ((cgi->out = fdopen(socketGetFd(client), "wb")) == NULL) {
		cgiSendInternalServerError(cgi, "%s %d: %s (%d)\r\n", __FILE__, __LINE__, strerror(errno), errno);
		goto error0;
	}

	if ((cgi->_RAW = BufCreate(CGI_CHUNK_SIZE)) == NULL) {
		cgiSendInternalServerError(cgi, "%s %d: %s (%d)\r\n", __FILE__, __LINE__, strerror(errno), errno);
		goto error1;
	}

	if (cgiReadRequest(cgi, client) || cgiParseHeaders(cgi))
		goto error1;

	/* Offset points immediately following end of headers. */
	BufSetOffset(cgi->_RAW, BufLength(cgi->_RAW)+1);

	/* RFC 2616 section 4.4 Message Length point 3, when
	 * Transfer-Encoding is present then Content-Length
	 * is ignored.
	 */
	i = cgiMapFind(cgi->_HTTP, "Transfer-Encoding");
	if (cgi->request_method[2] == 'S'
	&& (i < 0 || strcmp(cgi->_HTTP[i].value, "identity") == 0)) {
		/* Collect optional POST data. */
		if (cgi->content_length == empty) {
			cgiSend(cgi, HTTP_LENGTH_REQUIRED, "Length Required", "Missing Content-Length header.");
			goto error1;
		}
		content_length = (size_t) strtol(cgi->content_length, NULL, 10);
		if (cgiReadN(client, cgi->_RAW, content_length)) {
			cgiSendInternalServerError(cgi, "POST read error, Content-Length=%lu\r\n%s %d: %s (%d)\r\n", content_length, __FILE__, __LINE__, strerror(errno), errno);
			goto error1;
		}
	}

	if (cgi->content_type != NULL
	&& strcmp(cgi->content_type, "application/x-www-form-urlencoded") == 0
	&& (cgi->_POST = cgiParseForm((const char *) BufBytes(cgi->_RAW)+BufOffset(cgi->_RAW))) == NULL) {
		cgiSendInternalServerError(cgi, "Form POST parse error.\r\n");
		goto error1;
	}

	return 0;
error1:
	cgiFree(cgi);
error0:
	return -1;
}

/***********************************************************************
 *** END
 ***********************************************************************/
