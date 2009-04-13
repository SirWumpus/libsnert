/*
 * mime.c
 *
 * RFC 2045, 2046, 2047
 *
 * Copyright 2007 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#define _REENTRANT	1

#include <com/snert/lib/version.h>

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <com/snert/lib/mail/mime.h>
#include <com/snert/lib/util/Text.h>

/***********************************************************************
 *** MIME Parse States
 ***********************************************************************/

static void mimeBoundary(Mime *m);
static int mimeStateCR(Mime *m, int ch);
static int mimeStateCRLF(Mime *m, int ch);
static int mimeStateCRLF1(Mime *m, int ch);
static int mimeStateCRLF2(Mime *m, int ch);
static int mimeStateBoundary(Mime *m, int ch);
static int mimeStateContent(Mime *m, int ch);
static int mimeStateBase64(Mime *m, int ch);
static int mimeStateQpLiteral(Mime *m, int ch);
static int mimeStateQpSoftLine(Mime *m, int ch);
static int mimeStateQpEqual(Mime *m, int ch);
static int mimeStateQpDecode(Mime *m, int ch);
static int mimeStateHeader(Mime *m, int ch);
static int mimeStateHeaderLF(Mime *m, int ch);

static int
mimeIsBoundaryChar(int ch)
{
	if (isalnum(ch))
		return 1;

	return strchr("-=_'()+,./:?", ch) != NULL;
}

void
mimeDecodeAdd(Mime *m, int ch)
{
	m->mime_body_decoded_length++;

	/* Flush the decode buffer when full. */
	if (sizeof (m->decode.buffer)-1 <= m->decode.length) {
		if (m->mime_decode_line != NULL)
			(*m->mime_decode_line)(m);
		mimeDecodeFlush(m);
	}

	m->decode.buffer[m->decode.length++] = ch;
	m->decode.buffer[m->decode.length  ] = '\0';
}

void
mimeDecodeBodyAdd(Mime *m, int ch)
{
	mimeDecodeAdd(m, ch);

	if (m->mime_decoded_octet != NULL)
		(*m->mime_decoded_octet)(m, ch);
}

void
mimeDecodeHeaderAdd(Mime *m, int ch)
{
	mimeDecodeAdd(m, ch);

	if (m->mime_header_octet != NULL)
		(*m->mime_header_octet)(m, ch);
}

static void
mimeDecodeCR(Mime *m)
{
	if (m->state != mimeStateBase64) {
		mimeDecodeBodyAdd(m, ASCII_CR);
		m->state_newline = NULL;
	}
}

static void
mimeDecodeLF(Mime *m)
{
	if (m->state != mimeStateBase64) {
		mimeDecodeBodyAdd(m, ASCII_LF);
		m->state_newline = NULL;
	}
}

static void
mimeDecodeCRLF(Mime *m)
{
	/* The CRLF are not saved to the decode buffer, since
	 * the source buffer for which the CRLF relates to will
	 * have already been replaced while we were looking
	 * ahead at the source for a MIME boundary.
	 *
	 * The CRLF are not part of the base64 codeset, thus
	 * the call-back can be skipped.
	 */
	if (m->state_newline != NULL) {
		/* Was this part of a MIME conforming CRLF newline? */
		if (m->state_newline == mimeStateCR)
			mimeDecodeCR(m);
		mimeDecodeLF(m);
	}
}

static int
mimeIsMultipartCRLF(Mime *m, int ch)
{
	if (m->is_multipart) {
		/* RFC 2046 section 5.1.1. Common Syntax states that the CRLF
		 * preceeding the boundary line is conceptually part of the
		 * boundary, such that it is possible to have a MIME part
		 * that does not end with a CRLF.
		 */
		if (ch == ASCII_CR) {
			/* Conforming CRLF newline. */
			m->state = m->state_newline = mimeStateCR;
			return 1;
		} else if (ch == ASCII_LF) {
			/* Non-conforming LF newline, common to unix files. */
			m->state = m->state_newline = mimeStateCRLF;
			return 1;
		}

		/* Within MIME part content. */
	}

	return 0;
}

static void
mimeSourceLine(Mime *m, int ch)
{
	/* When the source buffer contains a line unit or is full, reset it.
	 * Also reset the decode buffer. The decode buffer should reflect
	 * the current contents of the source buffer.
	 */
	if (ch == EOF
	|| (ch == ASCII_LF && m->state != mimeStateHeaderLF
	  && m->state != mimeStateQpEqual && m->state != mimeStateQpSoftLine)
	|| sizeof (m->source.buffer)-1 <= m->source.length) {
		/* Source line call-back. */
		if (m->mime_source_line != NULL) {
			m->source.buffer[m->source.length] = '\0';
			(*m->mime_source_line)(m);
		}

		if (m->state == mimeStateQpDecode) {
			/* If a quoted printable character sequence
			 * "straddles across" the buffer end, ie. "=X"
			 * are the last things in the buffer and the
			 * state is mimeStateQpDecode, then we have to
			 * "carry over" the information related to that
			 * state as part of the buffer reset.
			 */
			m->source.buffer[0] = m->source.buffer[m->source.length-2];
			m->source.buffer[1] = m->source.buffer[m->source.length-1];
			m->source.length = 2;
			m->decode.length = 0;
		} else {
			m->start_of_line = (ch == ASCII_LF);
			mimeSourceFlush(m);
		}
	}
}

static void
mimeDecodeLine(Mime *m, int ch)
{
	if (ch == EOF

	/* Flush the decode buffer if it was a line unit. */
	|| (0 < m->decode.length && m->decode.buffer[m->decode.length-1] == '\n')) {
		if (m->mime_decode_line != NULL)
			(*m->mime_decode_line)(m);
		mimeDecodeFlush(m);
	}
}

static int
mimeStateContent(Mime *m, int ch)
{
	if (!mimeIsMultipartCRLF(m, ch))
		mimeDecodeBodyAdd(m, ch);

	return 0;
}

static int
mimeStateCR(Mime *m, int ch)
{
	if (ch == ASCII_LF) {
		m->state = mimeStateCRLF;
	} else {
		m->state = m->state_next_part;
		mimeDecodeCR(m);
		return (*m->state)(m, ch);
	}

	return 0;
}

static int
mimeStateCRLF(Mime *m, int ch)
{
	/* First hyphen of boundary line? */
	if (ch == '-') {
		m->state = mimeStateCRLF1;
	} else {
		m->state = m->state_next_part;
		mimeDecodeCRLF(m);
		return (*m->state)(m, ch);
	}

	return 0;
}

static int
mimeStateCRLF1(Mime *m, int ch)
{
	/* Second hyphen of boundary line? */
	if (ch == '-') {
		m->state = mimeStateCRLF2;
	} else {
		m->state = m->state_next_part;
		mimeDecodeCRLF(m);
		(void) (*m->state)(m, '-');
		return (*m->state)(m, ch);
	}

	return 0;
}

static int
mimeStateCRLF2(Mime *m, int ch)
{
	/* Is the boundary line lead-in (--) followed by CRLF or LF?
	 *
	 * This lazy MIME boundary parsing excludes a boundary marker
	 * that is of zero length or starts with whitespace, eg.
	 *
	 *	Content-Type: multipart/mixed; boundary=""
 	 *
	 *	Content-Type: multipart/mixed; boundary=" foo bar"
	 *
	 * Ignoring zero length boundary markers ensures that email
	 * signatures, often delimited by a simple "--CRLF" (see
	 * Thunderbird MUA), remain part of the MIME part.
	 */
	if (isspace(ch)) {
		/* Not a MIME boundary. */
		m->state = m->state_next_part;
		mimeDecodeCRLF(m);
		(void) (*m->state)(m, '-');
		(void) (*m->state)(m, '-');
		return (*m->state)(m, ch);
	} else {
		m->state = mimeStateBoundary;
	}

	return 0;
}

static int
mimeStateBoundary(Mime *m, int ch)
{
	/* End of line or buffer full? */
	if (ch == ASCII_LF || sizeof (m->source.buffer) <= m->source.length) {
		int i, all_hyphen = 1;

		/* RFC 2046 section 5.1.1 states a boundary delimiter line
		 * is the two hyphen lead-in characters followed by 0 to 69
		 * boundary characters. We treat it as 1 to 69 for reasons
		 * stated in mimeStateCRLF2.
		 */

		/* RFC 2046 section 5.1.1
		 *
		 *   The boundary delimiter line is then defined as a line
		 *   consisting entirely of two hyphen characters ("-",
		 *   decimal value 45) followed by the boundary parameter
		 *   value from the Content-Type header field, optional
		 *   linear whitespace, and a terminating CRLF.
		 *
		 *
		 *   boundary := 0*69<bchars> bcharsnospace
		 *
		 *   bchars := bcharsnospace / " "
		 *
		 *   bcharsnospace := DIGIT / ALPHA / "'" / "(" / ")" /
		 *		      "+" / "_" / "," / "-" / "." /
		 *		      "/" / ":" / "=" / "?"
		 */
		for (i = 2; i < m->source.length; i++) {
			if (!mimeIsBoundaryChar(m->source.buffer[i]) && !isspace(m->source.buffer[i]))
				break;
			if (m->source.buffer[i] != '-')
				all_hyphen = 0;
		}

		/* If the source buffer contains valid boundary characters
		 * upto the end of the buffer, then assume it is a boundary
		 * line.
		 *
		 * Look out for quoted-printable split line of hyphens, eg.
		 *
		 * 	----------------------------------------=\r\n
   		 *	-------\r\n
   		 *
   		 * These should not be treated as two boundaries.
		 */
		if (!all_hyphen && i == m->source.length
		/* If quoted-printable and not a soft-line break (=CRLF, =LF)
		 * to handle split line of hyphens.
		 */
		&& (m->state_next_part != mimeStateQpLiteral
		|| (m->source.buffer[m->source.length-3] != '=' && m->source.buffer[m->source.length-2] != '='))) {
			m->source.buffer[m->source.length] = '\0';
			if (m->mime_body_finish != NULL)
				(*m->mime_body_finish)(m);
			m->mime_part_number++;
			mimeBoundary(m);
		} else {
			/* Process the source stream before being flushed. */
			m->state = m->state_next_part;
			mimeDecodeCRLF(m);
			for (i = 0; i < m->source.length; i++) {
				if (m->state != mimeStateBase64 || !isspace(m->source.buffer[i]))
					(void)(*m->state)(m, m->source.buffer[i]);
			}
		}
	}

	return 0;
}

static int
mimeStateBase64(Mime *m, int ch)
{
	if (!mimeIsMultipartCRLF(m, ch)) {
		ch = b64Decode(&m->b64, ch);

		/* Ignore intermediate base64 states until an octet is decoded. */
		if (BASE64_IS_OCTET(ch))
			mimeDecodeBodyAdd(m, ch);
	}

	return 0;
}

static int
mimeStateQpLiteral(Mime *m, int ch)
{
	if (!mimeIsMultipartCRLF(m, ch)) {
		if (ch == '=')
			m->state = mimeStateQpEqual;
		else
			mimeDecodeBodyAdd(m, ch);
	}

	return 0;
}

static int
mimeStateQpSoftLine(Mime *m, int ch)
{
	return mimeStateQpLiteral(m, ch);
}

static int
mimeStateQpEqual(Mime *m, int ch)
{
	if (ch == ASCII_CR) {
		/* Ignore soft-line break, ie. =CRLF.
		 */
		;
	} else if (ch == ASCII_LF) {
		/* Ignore soft-line break, ie. =CRLF or =LF.
		 */
		m->state = mimeStateQpSoftLine;
	} else if (isxdigit(ch)) {
		/* Initial part of a quoted printable. The tail
		 * of the source buffer will contain "=X".
		 */
		m->state = mimeStateQpDecode;
	} else {
		/* Invalid quoted-printable sequence. Treat the
		 * leading equals-sign as a literal. The decode
		 * buffer will contain "=C".
		 */
		m->state = mimeStateQpLiteral;
		mimeDecodeBodyAdd(m, '=');
		mimeDecodeBodyAdd(m, ch);
	}

	return 0;
}

/**
 * @param octet
 *	A quoted-printable hexadecimal digit character, which are
 *	defined to be upper case only.
 *
 * @return
 *	The value of the hexadecimal digit; otherwise -1 if the
 *	character is not a hexadecimal digit.
 *
 * @see
 *	RFC 2396
 */
int
qpHexDigit(int x)
{
	/* In both EBCDIC and US-ASCII the codings for 0..9 and A..F
	 * are sequential, so its safe to subtract '0' and 'A' from
	 * a character octet to get the digit's value.
	 */
	if (isdigit(x))
		return x - '0';

	if (isxdigit(x))
		return 10 + toupper(x) - 'A';

	return -1;
}

static int
mimeStateQpDecode(Mime *m, int ch)
{
	unsigned value;

	if (isxdigit(ch)) {
		/* Replace =XX with decoded octet.
		 *
		 * NOTE that strictly RFC conforming quoted-printable
		 * uses upper case hex digits only, BUT Thunderbird
		 * for some stupid reason allows for lower case hex
		 * digits and for once Outlook Repress seems to do the
		 * correct thing (though that might be an oversight on
		 * their part).
		 */
		value = qpHexDigit(m->source.buffer[m->source.length-2]) * 16 + qpHexDigit(ch);
		mimeDecodeBodyAdd(m, value);
	} else {
		/* Invalid quoted printable sequence. Treat as literal. */
		mimeDecodeBodyAdd(m, '=');
		mimeDecodeBodyAdd(m, m->source.buffer[m->source.length-2]);
		mimeDecodeBodyAdd(m, ch);
	}

	m->state = mimeStateQpLiteral;

	return 0;
}

static int
mimeStateHeaderLF(Mime *m, int ch)
{
	/* Either a new header or a continue a folded header. */
	m->state = mimeStateHeader;

	if (ch == ASCII_TAB) {
		/* Folded header line, convert TAB to SPACE. */
		m->source.buffer[m->source.length-1] = ASCII_SPACE;
		mimeDecodeHeaderAdd(m, ASCII_SPACE);
	} else if (ch != ASCII_SPACE) {
		/* End of previous header. */
		m->source.buffer[--m->source.length] = '\0';

		/* Check the header for MIME behaviour. */
		if (0 <= TextFind((char *) m->source.buffer, "Content-Type:*multipart/*", m->source.length, 1)) {
			m->is_multipart = 1;
		} else if (0 <= TextFind((char *) m->source.buffer, "Content-Type:*/*", m->source.length, 1)) {
			/* Simply skip decoding this content. Look
			 * only for the MIME boundary line.
			 */
			m->state_next_part = mimeStateContent;
		} else if (0 <= TextFind((char *) m->source.buffer, "Content-Transfer-Encoding:*quoted-printable*", m->source.length, 1)) {
			m->state_next_part = mimeStateQpLiteral;
		} else if (0 <= TextFind((char *) m->source.buffer, "Content-Transfer-Encoding:*base64*", m->source.length, 1)) {
			m->state_next_part = mimeStateBase64;
			b64Reset(&m->b64);
		}

		/* Complete unfolded header line less the CRLF. */
		if (m->mime_header != NULL)
			(*m->mime_header)(m);

		/* Start of next header. */
		mimeBuffersFlush(m);
		m->start_of_line = 1;
		m->source.buffer[m->source.length++] = ch;
		mimeDecodeHeaderAdd(m, ch);

		/* When newlines are LF instead of CRLF, then
		 * we have initiate this transition manually.
		 */
		if (ch == ASCII_LF)
			(void) mimeStateHeader(m, ASCII_LF);
	}

	return 0;
}

static int
mimeStateHeader(Mime *m, int ch)
{
	if (ch == ASCII_LF) {
		m->source.length--;
		if (0 < m->source.length && m->source.buffer[m->source.length-1] == ASCII_CR)
			m->source.length--;
		m->source.buffer[m->source.length] = '\0';

		if (0 < m->decode.length && m->decode.buffer[m->decode.length-1] == ASCII_CR)
			m->decode.length--;
		m->decode.buffer[m->decode.length] = '\0';

		if (0 < m->source.length) {
			/* Check for folded header line on next character. */
			m->state = mimeStateHeaderLF;
		} else {
			/* End of headers. */
			mimeBuffersFlush(m);
			m->mime_body_length = 0;
			m->mime_body_decoded_length = 0;

			if (m->mime_body_start != NULL)
				(*m->mime_body_start)(m);

			if (!mimeIsMultipartCRLF(m, ch))
				m->state = m->state_next_part;

			/* Avoid inserting an extra newline after the end of headers. */
			m->state_newline = NULL;
		}
	} else {
		/* Keep decode buffer in sync with source buffer. */
		mimeDecodeHeaderAdd(m, ch);
	}

	return 0;
}

/***********************************************************************
 *** MIME API
 ***********************************************************************/

void
mimeNoHeaders(Mime *m)
{
	m->state = mimeStateContent;
}

void
mimeSourceFlush(Mime *m)
{
	if (m->mime_source_flush != NULL)
		(*m->mime_source_flush)(m);

	*m->source.buffer = '\0';
	m->source.length = 0;
}

void
mimeDecodeFlush(Mime *m)
{
	if (m->mime_decode_flush != NULL)
		(*m->mime_decode_flush)(m);

	*m->decode.buffer = '\0';
	m->decode.length = 0;
}

void
mimeBuffersFlush(Mime *m)
{
	mimeSourceFlush(m);
	mimeDecodeFlush(m);
}

static void
mimeBoundary(Mime *m)
{
	b64Reset(&m->b64);
	mimeBuffersFlush(m);
	m->state = mimeStateHeader;
	m->state_next_part = mimeStateContent;
	m->start_of_line = 1;
}

/**
 * @param m
 *	Pointer to a Mime context structure to reset to the start state.
 */
void
mimeReset(Mime *m)
{
	if (m != NULL) {
		b64Init();
		mimeBoundary(m);
		m->is_multipart = 0;
		m->mime_part_number = 0;
		m->mime_part_length = 0;
		m->mime_body_length = 0;
		m->mime_body_decoded_length = 0;
	}
}

/**
 * @param data
 *	Pointer to an opaque data structure for use by call-back hooks.
 *
 * @return
 *	Poitner to a Mime context structure or NULL on error.
 */
Mime *
mimeCreate(void *data)
{
	Mime *m;

	if ((m = calloc(1, sizeof (*m))) != NULL) {
		m->mime_data = data;
		mimeReset(m);
	}

	return m;
}

/**
 * @param m
 *	Pointer to a Mime context structre to free.
 */
void
mimeFree(Mime *m)
{
	free(m);
}

/**
 * @param m
 *	Pointer to a Mime context structure.
 *
 * @param ch
 *	Next input octet to parse.
 *
 * @return
 *	Zero to continue, otherwise -1 on error.
 */
int
mimeNextCh(Mime *m, int ch)
{
	unsigned rc;

	if (m == NULL) {
		errno = EFAULT;
		return -1;
	}

	if (255 < ch) {
		errno = EINVAL;
		return -1;
	}

	mimeDecodeLine(m, ch);

	if (ch != EOF) {
		/* Save the current input source byte. */
		m->source.buffer[m->source.length++] = ch;
		m->mime_part_length++;
		m->mime_body_length++;

		rc = (*m->state)(m, ch);
	}

	mimeSourceLine(m, ch);

	return rc;
}

/***********************************************************************
 *** MIME CLI
 ***********************************************************************/

#ifdef TEST
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

#if defined(HAVE_SYSLOG_H) && ! defined(__MINGW32__)
# include <syslog.h>
#endif

#include <com/snert/lib/io/Log.h>
#include <com/snert/lib/util/getopt.h>

int list_parts;
int extract_part = -1;
int enable_decode;

Mime *mime;


static char usage[] =
"usage: mime -l < message\n"
"       mime -p num [-d] < message\n"
"\n"
"-d\t\tdecode base64 or quoted-printable\n"
"-l\t\tlist MIME part headers\n"
"-p num\t\textract MIME part\n"
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
listHeaders(Mime *m)
{
	if (m->source.length == 0
	|| 0 <= TextFind((char *) m->source.buffer, "Content-*", m->source.length, 1)
	|| 0 <= TextFind((char *) m->source.buffer, "X-MD5-*", m->source.length, 1)
	)
		printf("%.3u: %s\r\n", m->mime_part_number, m->source.buffer);
}

void
printSource(Mime *m)
{
	if (m->mime_part_number == extract_part && m->state != mimeStateHeader)
		fwrite(m->source.buffer, 1, m->source.length, stdout);
}

void
printDecode(Mime *m)
{
	if (m->mime_part_number == extract_part && m->state != mimeStateHeader)
		fwrite(m->decode.buffer, 1, m->decode.length, stdout);
}

int
main(int argc, char **argv)
{
	int ch;

	while ((ch = getopt(argc, argv, "dlp:")) != -1) {
		switch (ch) {
		case 'd':
			enable_decode = 1;
			break;
		case 'l':
			list_parts = 1;
			break;
		case 'p':
			extract_part = strtol(optarg, NULL, 10);
			break;
		default:
			(void) fprintf(stderr, usage);
			exit(2);
		}
	}

	if (!list_parts && extract_part < 0) {
		(void) fprintf(stderr, usage);
		exit(2);
	}

	if ((mime = mimeCreate(NULL)) == NULL) {
		fprintf(stderr, "mimeCreate error: %s (%d)\n", strerror(errno), errno);
		exit(1);
	}

	if (list_parts) {
		mime->mime_header = listHeaders;
		mime->mime_body_start = listHeaders;
	} else if (enable_decode)
		mime->mime_source_line = printDecode;
	else
		mime->mime_source_line = printSource;

	while ((ch = fgetc(stdin)) != EOF) {
		if (mimeNextCh(mime, ch))
			break;
	}

	(void) mimeNextCh(mime, EOF);
	mimeFree(mime);

	return 0;
}

#endif /* TEST */
