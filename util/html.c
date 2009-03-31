/*
 * html.c
 *
 * Copyright 2009 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <com/snert/lib/util/html.h>

/***********************************************************************
 ***
 ***********************************************************************/

/**
 * <p>
 * Parse the string for the next HTML token range. A token consists of
 * either a text block (non-HTML tag) or an HTML tag from opening left
 * angle bracket (&lt;) to closing right angle bracket (&gt;) inclusive,
 * taking into account single and double quoted attribute strings, which
 * may contain backslash-escape sequences. No conversion is done.
 * </p>
 *
 * @param start
 *	A pointer to a C string pointer where the scan is to start. It
 *	is updated to where text block begins or start of an HTML tag
 *	after skipping leading whitespace.
 *
 * @param stop
 *	A pointer to a C string pointer. The point were the scan stops
 *	is passed back through this argument.
 *
 * @param state
 *	A pointer to an int type used to hold parsing state. Initialise
 *	to zero when parsing the start of a chunk of HTML.
 *
 * @return
 *	True if a text block or (partial) HTML tag was found. Otherwise
 *	false if any of the pointers are NULL or the start of string
 *	points to the end of string NUL byte.
 */
int
htmlTokenRange(const char **start, const char **stop, int *state)
{
	const char *s;
	int quote = 0, escape = 0;

	if (start == NULL || stop == NULL || state == NULL || **start == '\0')
		return 0;

	s = *start;

	if (*state == 0) {
		/* Skip leading white space. */
		for ( ; isspace(*s); s++)
			;

		/* Start of text block? */
		if (*s != '<') {
			/* Find start of next tag or end of string. */
			*stop = s + strcspn(s, "<");
			return 1;
		}
	}

	/* Start of an HTML tag. */
	escape = *state & 0x4000;
	quote = *state & 0xFF;
	*state = 0x8000;
	*start = s;

	/* Find end of tag or end of string. */
	for (s++ ; *s != '\0'; s++) {
		if (escape) {
			escape = 0;
			continue;
		}

		if (quote == 0 && *s == '>') {
			*state = 0;
			s++;
			break;
		}

		switch (*s) {
		case '"': case '\'':
			if (quote == 0)
				quote = *s;
			else if (*s == quote)
				quote = 0;
			continue;

		case '\\':
			/* Skip escape lead-in within quoted string. */
			escape = 0x4000;
			continue;
		}

		/* Skip escaped character within quoted string. */
		if (quote != 0 && escape)
			escape = 0;
	}

	/* Remember the parsing state in case we reached the end of
	 * string before finding the end of the HTML tag.
	 */
	*state |= (escape | quote);
	*stop = s;

	return 1;
}

/**
 * <p>
 * Parse the string for the next HTML token. A token consists of
 * either a text block (non-HTML tag) or an HTML tag from opening left
 * angle bracket (&lt;) to closing right angle bracket (&gt;) inclusive,
 * taking into account single and double quoted attribute strings, which
 * may contain backslash-escape sequences. No conversion is done.
 * </p>
 *
 * @param start
 *	A C string pointer where the scan is to start.
 *
 * @param stop
 *	A pointer to a C string pointer. The point were the scan stops
 *	is passed back through this argument.
 *
 * @param state
 *	A pointer to an int type used to hold parsing state. Initialise
 *	to zero when parsing the start of a chunk of HTML.
 *
 * @return
 *	An allocated C string contain a text block or HTML tag from
 *	left angle to right angle bracket inclusive. NULL on error or
 *	end of parse string.
 */
char *
htmlTokenNext(const char *start, const char **stop, int *state)
{
	size_t length;
	char *token = NULL;
	int local_state = 0;

	if (state == NULL)
		state = &local_state;

	if (htmlTokenRange(&start, stop, state)
	&& (token = malloc((length = *stop - start) + 1)) != NULL) {
		(void) memcpy(token, start, length);
		token[length] = '\0';
	}

	return token;
}

/***********************************************************************
 ***
 ***********************************************************************/
#ifdef TEST

#include <errno.h>
#include <string.h>

#if defined(HAVE_SYSLOG_H) && ! defined(__MINGW32__)
# include <syslog.h>
#endif

#include <com/snert/lib/io/Log.h>
#include <com/snert/lib/mail/mime.h>
#include <com/snert/lib/type/Vector.h>
#include <com/snert/lib/util/getopt.h>
#include <com/snert/lib/util/Text.h>
#include <com/snert/lib/sys/sysexits.h>

typedef struct {
	Mime *mime;
	int part_length;
	int text_html;
	int strip_tag;
	int html_state;
	int close_tag;
	const char *tag;
} StripMime;

int debug;
int all_tags;
int redact_html;
Vector tags;
Vector headers;
StripMime strip;

const char *closed_tags[] = {
	"!DOCTYPE",
	"!--",
	"AREA",
	"BASE",
	"BR",
	"HR",
	"IMG",
	"INPUT",
	"ISINDEX",
	"LINK",
	"META",
	NULL
};

static char usage[] =
"usage: htmlstrip [-vX][-h header,...][-t tag,...] < message\n"
"\n"
"-h header,...\tlist of message headers to strip \n"
"-t tag,...\tlist of HTML tag names to strip, or \"all\"\n"
"-v\t\tverbose logging to standard error\n"
"-X\t\tredact HTML in place of stripping\n"
"\n"
LIBSNERT_COPYRIGHT "\n"
;

/***********************************************************************
 ***
 ***********************************************************************/

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

static void
stripMimeHeader(Mime *m)
{
	int length;
	const char **hdr;
	StripMime *ctx = m->mime_data;

	if (0 <= TextFind(m->source.buffer, "Content-Type:*text/html*", m->source.length, 1)) {
		ctx->text_html++;
		ctx->tag = NULL;
	}

	/* When we decode a text/html section, change the
	 * Content-Transfer-Encoding to reflect the defcoded
	 * output.
	 */
	if (ctx->text_html && 0 < TextInsensitiveStartsWith(m->source.buffer, "Content-Transfer-Encoding:")) {
		printf("Content-Transfer-Encoding: 8bit\r\n");
		return;
	}

	if (headers != NULL) {
		for (hdr = (const char **) VectorBase(headers); *hdr != NULL; hdr++) {
			if (0 < (length = TextInsensitiveStartsWith(m->source.buffer, *hdr)) && (*hdr)[length] == ':') {
				mimeBuffersFlush(m);
				return;
			}
		}
	}

	printf("%s\r\n", m->source.buffer);
}

static void
stripMimeHeaderOctet(Mime *m, int ch)
{
	StripMime *ctx = m->mime_data;

	/* If the first character is the start of a HTML tag, then
	 * we can't be processing an email message, therefore switch
	 * the state from headers to content.
	 */
	if (ch == '<') {
		mimeNoHeaders(m);
		m->mime_header = NULL;
		ctx->text_html = 1;
	}

	m->mime_header_octet = NULL;
}

static void
stripSourceLine(Mime *m)
{
	StripMime *ctx = m->mime_data;
	const char *start, *stop, **tag;

	if (!ctx->text_html) {
		/* Write the source of MIME parts that are not text/html. */
		fwrite(m->source.buffer, 1, m->source.length, stdout);
		return;
	}

	if (all_tags || m->decode.length <= 0)
		return;

	for (start = m->decode.buffer; htmlTokenRange(&start, &stop, &ctx->html_state); start = stop) {
		if (tags != NULL && *start == '<') {
			if (0 < debug) {
				int ch;
				ch = *stop;
				* (char *) stop = '\0';
				fprintf(stderr, "tag=%s\n", start);
				* (char *)stop = ch;
			}
			start++;

			if (ctx->tag != NULL) {
				if (*start == '/' && 0 < TextInsensitiveStartsWith(start+1, ctx->tag)) {
					ctx->close_tag = 1;
				}

				/* Count tag nesting. */
				else if (0 < TextInsensitiveStartsWith(start, ctx->tag)) {
					ctx->strip_tag++;
					if (0 < debug)
						fprintf(stderr, "tag=%s depth=%d\n", ctx->tag, ctx->strip_tag);
				}
			} else {
				for (tag = (const char **) VectorBase(tags); *tag != NULL; tag++) {
					if (0 < TextInsensitiveStartsWith(start, *tag)) {
						ctx->tag = *tag;
						ctx->strip_tag++;
						ctx->close_tag = 0;

						if (0 < debug)
							fprintf(stderr, "tag=%s depth=%d\n", ctx->tag, ctx->strip_tag);

						for (tag = closed_tags; *tag != NULL; tag++) {
							if (TextInsensitiveCompare(ctx->tag, *tag) == 0) {
								ctx->close_tag = 1;
								break;
							}
						}
						break;
					}
				}
			}

			start--;
		}

		if (ctx->strip_tag == 0) {
			for ( ; m->decode.buffer < (unsigned char *) start && isspace(start[-1]); start--)
				;
			fwrite(start, 1, stop - start, stdout);
			ctx->part_length += stop - start;
		} else if (redact_html) {
			for ( ; m->decode.buffer < (unsigned char *) start && isspace(start[-1]); start--)
				;
			for ( ; start < stop; start++) {
				fputc(isspace(*start) ? *start : 'X', stdout);
			}
		}

		if (ctx->close_tag) {
			ctx->strip_tag--;
			if (0 < debug)
				fprintf(stderr, "tag=%s depth=%d\n", ctx->tag, ctx->strip_tag);
			if (ctx->strip_tag == 0)
				ctx->tag = NULL;
			ctx->close_tag = 0;
		}
	}
}

static void
stripMimePartStart(Mime *m)
{
	StripMime *ctx = m->mime_data;
	ctx->part_length = 0;
	ctx->html_state = 0;
	fputs("\r\n", stdout);
}

static void
stripMimePartFinish(Mime *m)
{
	StripMime *ctx = m->mime_data;

	if (all_tags && ctx->text_html)
		printf("<html><body>This HTML content has been removed.</body></html>\r\n");

	if (0 < ctx->part_length)
		fputs("\r\n", stdout);
	fputs(m->source.buffer, stdout);

	ctx->text_html = 0;
	ctx->html_state = 0;
}

int
main(int argc, char **argv)
{
	int ch;

	while ((ch = getopt(argc, argv, "h:t:vX")) != -1) {
		switch (ch) {
		case 'h':
			if ((headers = TextSplit(optarg, ",", 0)) == NULL) {
				fprintf(stderr, "out of memory\n");
				exit(EX_SOFTWARE);
			}
			break;

		case 't':
			if (TextInsensitiveCompare(optarg, "all") == 0) {
				all_tags = 1;
			} else if ((tags = TextSplit(optarg, ",", 0)) == NULL) {
				fprintf(stderr, "out of memory\n");
				exit(EX_SOFTWARE);
			}
			break;

		case 'v':
			debug++;
			break;

		case 'X':
			redact_html++;
			break;

		default:
			(void) fprintf(stderr, usage);
			exit(EX_USAGE);
		}
	}

	if ((strip.mime = mimeCreate(NULL)) == NULL) {
		fprintf(stderr, "mimeCreate error: %s (%d)\n", strerror(errno), errno);
		exit(EX_SOFTWARE);
	}

	strip.mime->mime_data = &strip;
	strip.mime->mime_header = stripMimeHeader;
	strip.mime->mime_body_start = stripMimePartStart;
	strip.mime->mime_body_finish = stripMimePartFinish;
	strip.mime->mime_source_line = stripSourceLine;
	strip.mime->mime_header_octet = stripMimeHeaderOctet;

	while ((ch = fgetc(stdin)) != EOF) {
		if (mimeNextCh(strip.mime, ch))
			break;
	}

	(void) mimeNextCh(strip.mime, EOF);
	mimeFree(strip.mime);

	return EXIT_SUCCESS;
}
#endif /* TEST */
