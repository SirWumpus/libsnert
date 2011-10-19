/*
 * mime.c
 *
 * RFC 2045, 2046, 2047
 *
 * Copyright 2007, 2011 by Anthony Howe. All rights reserved.
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
 *** MIME Decoders & States
 ***********************************************************************/

static int mimeStateHdr(Mime *m, int ch);
static int mimeStateBdy(Mime *m, int ch);
static int mimeStateQpEqual(Mime *m, int ch);

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

void
mimeDecodeFlush(Mime *m)
{
	MimeHooks *hook;

	if (0 < m->decode.length) {
		if (m->state.source_state != mimeStateHdr) {
			for (hook = m->mime_hook; hook != NULL; hook = hook->next) {
				if (hook->decode_flush != NULL)
					(*hook->decode_flush)(m, hook->data);
			}
		}
		MEMSET(m->decode.buffer, 0, sizeof (m->decode.buffer));
		m->decode.length = 0;
	}
}

static void
mimeDecodeAppend(Mime *m, int ch)
{
	MimeHooks *hook;

	for (hook = m->mime_hook; hook != NULL; hook = hook->next) {
		if (hook->decoded_octet != NULL)
			(*hook->decoded_octet)(m, ch, hook->data);
	}

	if (ch != EOF) {
		m->mime_body_decoded_length++;
		m->decode.buffer[m->decode.length++] = ch;
	}

	/* Flush the decode buffer on a line unit or when full. */
	if (ch == ASCII_LF || ch == EOF || sizeof (m->decode.buffer)-1 <= m->decode.length)
		mimeDecodeFlush(m);
}

int
mimeDecodeAdd(Mime *m, int ch)
{
	if (ch == ASCII_CR && !m->state.decode_state_cr) {
		m->state.decode_state_cr = 1;
		return ch;
	}

	if (m->state.decode_state_cr) {
		mimeDecodeAppend(m, ASCII_CR);
		m->state.decode_state_cr = 0;
	}

	mimeDecodeAppend(m, ch);
	m->decode.buffer[m->decode.length] = '\0';

	return ch;
}

static int
mimeStateBase64(Mime *m, int ch)
{
	ch = b64Decode(&m->state.b64, ch);

	/* Ignore intermediate base64 states until an octet is decoded. */
	if (BASE64_IS_OCTET(ch) || ch == BASE64_EOF)
		(void) mimeDecodeAdd(m, ch);

	return 0;
}

static int
mimeStateQpLiteral(Mime *m, int ch)
{
	if (ch == '=')
		m->state.decode_state = mimeStateQpEqual;
	else
		mimeDecodeAdd(m, ch);

	return 0;
}

static int
mimeStateQpSoftLine(Mime *m, int ch)
{
	return mimeStateQpLiteral(m, ch);
}

static int
mimeStateQpDecode(Mime *m, int ch)
{
	unsigned value;

	value = m->decode.buffer[m->decode.length];

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
		value = qpHexDigit(value) * 16 + qpHexDigit(ch);
		mimeDecodeAdd(m, value);
	} else {
		/* Invalid quoted printable sequence. Treat as literal. */
		mimeDecodeAdd(m, '=');
		mimeDecodeAdd(m, value);
		mimeDecodeAdd(m, ch);
	}

	m->state.decode_state = mimeStateQpLiteral;

	return 0;
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
		m->state.decode_state = mimeStateQpSoftLine;
	} else if (isxdigit(ch)) {
		/* Initial part of a quoted printable. The tail
		 * of the decode buffer will contain "=X".
		 */
		m->decode.buffer[m->decode.length] = ch;
		m->state.decode_state = mimeStateQpDecode;
	} else {
		/* Invalid quoted-printable sequence. Treat the
		 * leading equals-sign as a literal. The decode
		 * buffer will contain "=C".
		 */
		m->state.decode_state = mimeStateQpLiteral;
		mimeDecodeAdd(m, '=');
		mimeDecodeAdd(m, ch);
	}

	return 0;
}

void
mimeSourceFlush(Mime *m)
{
	MimeHooks *hook;

	if (0 < m->source.length) {
		if (m->state.source_state != mimeStateHdr) {
			m->source.buffer[m->source.length] = '\0';
			for (hook = m->mime_hook; hook != NULL; hook = hook->next) {
				if (hook->source_flush != NULL)
					(*hook->source_flush)(m, hook->data);
			}
		}
		MEMSET(m->source.buffer, 0, sizeof (m->source.buffer));
		m->source.length = 0;
	}
}

void
mimeBodyFinish(Mime *m)
{
	MimeHooks *hook;

	if (m->state.source_state != mimeStateHdr) {
		for (hook = m->mime_hook; hook != NULL; hook = hook->next) {
			if (hook->body_finish != NULL)
				(*hook->body_finish)(m, hook->data);
		}
	}
}

static int
mimeIsBoundaryChar(int ch)
{
	return isalnum(ch) || strchr("-=_'()+,./:?", ch) != NULL;
}

static int
mimeStateBoundary(Mime *m, int ch)
{
	/* Accumulate the boundary line unitl LF or buffer full. */
	if (ch == ASCII_LF || sizeof (m->source.buffer) <= m->source.length) {
		unsigned i, has_qp = 0, all_hyphen = 1;

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

			/* Treat ----\r\n, ====\r\n, -=-=-=-\r\n, ----=\r\n
			 * as a run. Assumes CRLF is at the end of the line.
			 */
			all_hyphen = all_hyphen && strchr("-=\r\n", m->source.buffer[i]);

			if (isxdigit(m->source.buffer[i]) && m->source.buffer[i-1] == '=')
				has_qp = 1;
		}

		/* If the source buffer contains valid boundary characters
		 * upto the end of the buffer, then assume it is a boundary
		 * line.
		 *
		 * a) Look out for quoted-printable split line of hyphens, eg.
		 *
		 * 	----------------------------------------=\r\n
   		 *	-------\r\n
   		 *
   		 * These should not be treated as two boundaries.
   		 *
   		 * b) Look out for signature line separator that should not
   		 * be treated as a boundary even though it is valid, when
   		 * using quoted printable.
   		 *
   		 *	--=20\r\n
   		 *
   		 * c) Take care with a valid boundary line, looks like a
   		 * quoted printable soft line break.
   		 *
   		 *	--=__PartF0D8505D.0__=\r\n
   		 *
   		 * d) Take care with valid boundary line that might look
   		 * like a line containing a quoted printable value.
   		 *
   		 *	--=====002_Dragon527425384786_=====\r\n
   		 *
   		 * The =00 should not be considered quoted printable. The
   		 * current solution is to only check for quoted printable
   		 * if we're expecting quoted printable in a given MIME part.
   		 * This solution is not ideal when the MIME part is quoted
   		 * printable and the boundary has the appearance of being
   		 * quoted printable.
		 */
		if (!all_hyphen && (m->state.encoding != MIME_QUOTED_PRINTABLE || !has_qp) && i == m->source.length) {
			/* Terminate decoding for this body part */
			m->state.decode_state_cr = 0;
			m->state.decode_state = mimeDecodeAdd;
			(void) (*m->state.decode_state)(m, EOF);

			/* Discard the boundary without flushing. */
			m->mime_part_length -= m->source.length;
			m->mime_body_length -= m->source.length;
			m->source.length = 0;
			m->source.buffer[0] = '\0';

			mimeBodyFinish(m);

			m->mime_part_number++;
			m->mime_part_length = 0;
			m->state.has_content_type = 0;
			m->state.is_message_rfc822 = 0;
			m->state.encoding = MIME_NONE;
			m->state.source_state = mimeStateHdr;
		} else {
			/* Process the source stream before being flushed. */
			m->state.decode_state_cr = 0;
			m->state.source_state = mimeStateBdy;
			for (i = 0; i < m->source.length; i++) {
				(void) (*m->state.decode_state)(m, m->source.buffer[i]);
			}
			if (ch != ASCII_LF)
				(void) (*m->state.decode_state)(m, ch);
		}
	}

	return ch;
}

static int
mimeStateDash2(Mime *m, int ch)
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
		(void) (*m->state.decode_state)(m, ASCII_LF);
		(void) (*m->state.decode_state)(m, '-');
		(void) (*m->state.decode_state)(m, '-');
		(void) (*m->state.decode_state)(m, ch);
		m->state.source_state = mimeStateBdy;
	} else {
		m->state.source_state = mimeStateBoundary;
	}

	return ch;
}

static int
mimeStateDash1(Mime *m, int ch)
{
	if (ch == '-') {
		/* Second hyphen of boundary line? */
		m->state.source_state = mimeStateDash2;
	} else {
		(void) (*m->state.decode_state)(m, ASCII_LF);
		(void) (*m->state.decode_state)(m, '-');
		(void) (*m->state.decode_state)(m, ch);
		m->state.source_state = mimeStateBdy;
	}

	return ch;
}

static int
mimeStateBdyLF(Mime *m, int ch)
{
	if (m->state.is_multipart && ch == '-') {
		/* First hyphen of boundary line? */
		m->state.source_state = mimeStateDash1;
        } else if (ch == ASCII_LF) {
		/* We have a LF LF pair. MIME message with LF newlines?
		 * Flush the first LF and keep the second LF.
		 */
		(void) (*m->state.decode_state)(m, ASCII_LF);
		(void) mimeStateBdy(m, ASCII_LF);
	} else {
		/* Flush upto, but excluding the trailing CR. */
		if (ch == ASCII_CR)
			/* We have a LF CR sequence, eg. multiple blank lines. */
			m->source.length--;

		mimeSourceFlush(m);

		/* Restore the newline, in case the MIME boundary
		 * check fails to find a boundary line.
		 */
		if (ch == ASCII_CR)
			m->source.buffer[m->source.length++] = ASCII_CR;

		(void) (*m->state.decode_state)(m, ASCII_LF);
		(void) (*m->state.decode_state)(m, ch);
		m->state.source_state = mimeStateBdy;
	}

	return ch;
}

static int
mimeStateBdy(Mime *m, int ch)
{
	if (ch == ASCII_LF) {
		int has_cr = 0;

		/* Flush upto, but excluding the trailing CRLF or LF. */
		m->source.length--;
		if (0 < m->source.length && m->source.buffer[m->source.length-1] == ASCII_CR) {
			m->source.length--;
			has_cr = 1;
		}
		m->source.buffer[m->source.length] = '\0';
		mimeSourceFlush(m);

		/* Restore the newline, in case the MIME boundary
		 * check fails to find a boundary line.
		 */
		if (has_cr)
			m->source.buffer[m->source.length++] = ASCII_CR;
		m->source.buffer[m->source.length++] = ASCII_LF;

		if (m->state.is_multipart) {
			/* Look for MIME boundary line. */
			m->state.source_state = mimeStateBdyLF;
		} else {
			(void) (*m->state.decode_state)(m, ASCII_LF);
		}
	} else {
		(void) (*m->state.decode_state)(m, ch);
	}

	return ch;
}


static int
mimeStateHdrBdy(Mime *m, int ch)
{
	m->state.source_state = mimeStateBdy;

	/* End of header CRLF might be lead in to boundary
	 * when there is no preamble text and/or newline
	 * before the boundary.
	 */
	if (ch == '-')
		return mimeStateBdyLF(m, ch);

	return mimeStateBdy(m, ch);
}

static int
mimeStateEOH(Mime *m, int ch)
{
	MimeHooks *hook;

	/* End of headers. */
	mimeDecodeFlush(m);
	m->mime_body_length = 0;
	m->mime_body_decoded_length = 0;
	m->source.buffer[0] = '\0';
	m->source.length = 0;

	for (hook = m->mime_hook; hook != NULL; hook = hook->next) {
		if (hook->body_start != NULL)
			(*hook->body_start)(m, hook->data);
	}

	/* HACK for uri.c:
	 *
	 * When crossing from MIME part headers to a body
	 * message/rfc822, we want to continue parsing
	 * the embedded message headers so we can properly
	 * URI parse the embedded message.
	 *
	 * Ideally the URI MIME hooks should handle this,
	 * but there is currently no MIME API mechanism to
	 * say "no state change" or to change the state
	 * since the state functions are private.
	 */
	if (m->state.is_message_rfc822)
		/* Remain in the header parse state. */
		m->state.is_message_rfc822 = 0;
	else
		/* Normal header to body transition. */
		m->state.source_state = mimeStateHdrBdy;

	return ch;
}

static int
mimeStateHdrLF(Mime *m, int ch)
{
	MimeHooks *hook;

	if (ch == ASCII_CR) {
		/* For single CR, remain in this state waiting for the
		 * LF that marks end-of-headers. If we see CRCR, then
		 * treat as a folded header; though illegal, this
		 * should be safer.
		 */
		m->source.length--;
	} else if (ch == ASCII_SPACE || ch == ASCII_TAB) {
		/* Folded header, resume header gathering. */
		m->state.source_state = mimeStateHdr;
	} else {
		if (0 < m->source.length) {
			/* End of unfolded header line less newline. */
			m->state.source_state = mimeStateHdr;
			m->source.buffer[--m->source.length] = '\0';

			/* Check the header for MIME behaviour. */
			if (0 <= TextFind((char *) m->source.buffer, "Content-Type:*multipart/*", m->source.length, 1)) {
				m->state.is_multipart = 1;
				m->state.has_content_type = 1;
			} else if (0 <= TextFind((char *) m->source.buffer, "Content-Type:*message/rfc822", m->source.length, 1)) {
				m->state.has_content_type = 1;
				m->state.is_message_rfc822 = 1;
			} else if (0 <= TextFind((char *) m->source.buffer, "Content-Type:*/*", m->source.length, 1)) {
				/* Simply skip decoding this content. Look
				 * only for the MIME boundary line.
				 */
				m->state.has_content_type = 1;

				/* If the state.encoding has not yet been determined,
				 * then set the default literal decoding.
				 */
				if (m->state.encoding == MIME_NONE)
					m->state.decode_state = mimeDecodeAdd;
			} else if (0 <= TextFind((char *) m->source.buffer, "Content-Transfer-Encoding:*quoted-printable*", m->source.length, 1)) {
				m->state.decode_state = mimeStateQpLiteral;
				m->state.encoding = MIME_QUOTED_PRINTABLE;
			} else if (0 <= TextFind((char *) m->source.buffer, "Content-Transfer-Encoding:*base64*", m->source.length, 1)) {
				m->state.decode_state = mimeStateBase64;
				m->state.encoding = MIME_BASE64;
				b64Reset(&m->state.b64);
			}

			for (hook = m->mime_hook; hook != NULL; hook = hook->next) {
				if (hook->header != NULL)
					(*hook->header)(m, hook->data);
			}
			mimeSourceFlush(m);

			m->source.buffer[m->source.length++] = ch;

			/* When newlines are LF instead of CRLF, then we have
			 * initiate the End Of Header transition manually.
			 */
		}

		if (ch == ASCII_LF)
			(void) mimeStateEOH(m, ch);
	}

	return ch;
}

static int
mimeStateHdr(Mime *m, int ch)
{
	if (ch == ASCII_LF) {
		/* Remove trailing newline (LF or CRLF). */
		m->source.length--;
		if (0 < m->source.length && m->source.buffer[m->source.length-1] == ASCII_CR)
			m->source.length--;

		/* Check for folded header line, start of next
		 * header line, or end-of-headers with next octet.
		 */
		m->state.source_state = mimeStateHdrLF;
	}

	return ch;
}

/***********************************************************************
 *** MIME API
 ***********************************************************************/

/**
 * @param m
 *	Pointer to a Mime context structure to reset to the start state.
 */
void
mimeReset(Mime *m)
{
	if (m != NULL) {
		b64Init();
		b64Reset(&m->state.b64);

		/* Assume RFC 5322 message format starts with headers. */
		mimeHeadersFirst(m, 1);
		m->state.decode_state_cr = 0;
		m->state.decode_state = mimeDecodeAdd;

		mimeSourceFlush(m);
		mimeDecodeFlush(m);

		m->state.is_multipart = 0;
		m->state.has_content_type = 0;
		m->state.is_message_rfc822 = 0;
		m->state.encoding = MIME_NONE;

		m->mime_part_number = 0;
		m->mime_part_length = 0;
		m->mime_body_length = 0;
		m->mime_body_decoded_length = 0;
		m->mime_message_length = 0;
	}
}

/**
 * @return
 *	Poitner to a Mime context structure or NULL on error.
 */
Mime *
mimeCreate(void)
{
	Mime *m;

	if ((m = calloc(1, sizeof (*m))) != NULL) {
		mimeReset(m);
	}

	return m;
}

void
mimeHooksAdd(Mime *m, MimeHooks *hook)
{
	hook->next = m->mime_hook;
	m->mime_hook = hook;
}

/**
 * @param m
 *	Pointer to a Mime context structre to free.
 */
void
mimeFree(Mime *m)
{
	MimeHooks *hook, *next;

	if (m != NULL) {
		for (hook = m->mime_hook; hook != NULL; hook = next) {
			next = hook->next;
			if (hook->free != NULL)
				(*hook->free)(m, hook);
		}
		free(m);
	}
}

/**
 * @param m
 *	Pointer to a Mime context structure.
 *
 * @param flag
 *	True if input starts with RFC 5322 message headers;
 *	otherwise input begins directly with body content.
 */
void
mimeHeadersFirst(Mime *m, int flag)
{
	m->state.source_state = flag ? mimeStateHdr : mimeStateBdy;
}

/**
 * @param m
 *	Pointer to a Mime context structure.
 *
 * @return
 *	True if the parsing is still in the message headers.
 */
int
mimeIsHeaders(Mime *m)
{
	return m->mime_part_number == 0 && m->state.source_state == mimeStateHdr;
}

/**
 * @param m
 *	Pointer to a Mime context structure.
 *
 * @param ch
 *	Next input octet to parse or EOF.
 *
 * @return
 *	Zero on success; otherwise EOF on error.
 */
int
mimeNextCh(Mime *m, int ch)
{
	if (m == NULL) {
		errno = EFAULT;
		return EOF;
	}

	if (ch != EOF) {
		if (ch < 0 || 255 < ch) {
			errno = EINVAL;
			return EOF;
		}

		m->mime_part_length++;
		m->mime_body_length++;
		m->mime_message_length++;
		m->source.buffer[m->source.length++] = ch;

		(void) (*m->state.source_state)(m, ch);
	}

	if (ch == EOF) {
		(void) (*m->state.decode_state)(m, EOF);
		mimeDecodeFlush(m);
	}

	if (ch == EOF || sizeof (m->source.buffer)-1 <= m->source.length)
		mimeSourceFlush(m);

	if (ch == EOF)
		mimeBodyFinish(m);

	return 0;
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
#include <com/snert/lib/util/md5.h>
#include <com/snert/lib/util/getopt.h>

int list_parts;
int extract_part = -1;
int enable_decode;
int generate_md5;

Mime *mime;


static char usage[] =
"usage: mime -l < message\n"
"       mime -p num [-dm] < message\n"
"\n"
"-d\t\tdecode base64 or quoted-printable\n"
"-l\t\tlist MIME part headers\n"
"-m\t\tgenerate MD5s for MIME part\n"
"-p num\t\textract MIME part\n"
"\n"
LIBSNERT_STRING " " LIBSNERT_COPYRIGHT "\n"
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

typedef struct {
	md5_state_t source;
	md5_state_t decode;
} md5_mime_state;

void
md5_body_start(Mime *m, void *data)
{
	md5_mime_state *md5 = data;

	if (m->mime_part_number == extract_part) {
		md5_init(&md5->source);
		md5_init(&md5->decode);
	}
}

void
md5_body_finish(Mime *m, void *data)
{
	md5_mime_state *md5 = data;
	char digest_string[33];
	md5_byte_t digest[16];

	if (m->mime_part_number == extract_part) {
		fputc('\n', stdout);

		md5_finish(&md5->source, digest);
		md5_digest_to_string(digest, digest_string);
		fprintf(stdout, "MD5-Encoded: %s\n", digest_string);

		md5_finish(&md5->decode, digest);
		md5_digest_to_string(digest, digest_string);
		fprintf(stdout, "MD5-Decoded: %s\n", digest_string);

		fprintf(stdout, "part-length: %lu\nbody-length: %lu\n", m->mime_part_length, m->mime_body_length);

		fflush(stdout);
	}
}

void
md5_source_flush(Mime *m, void *data)
{
	md5_mime_state *md5 = data;

	if (m->mime_part_number == extract_part)
		md5_append(&md5->source, m->source.buffer, m->source.length);
}

void
md5_decode_flush(Mime *m, void *data)
{
	md5_mime_state *md5 = data;

	if (m->mime_part_number == extract_part)
		md5_append(&md5->decode, m->decode.buffer, m->decode.length);
}

void
listHeaders(Mime *m, void *_data)
{
	if (m->source.length == 0
	|| 0 <= TextFind((char *) m->source.buffer, "Content-*", m->source.length, 1)
	|| 0 <= TextFind((char *) m->source.buffer, "X-MD5-*", m->source.length, 1)
	)
		printf("%u: %s\r\n", m->mime_part_number, m->source.buffer);
}

void
printSource(Mime *m, void *_data)
{
	if (m->mime_part_number == extract_part)
		fwrite(m->source.buffer, 1, m->source.length, stdout);
}

void
printDecode(Mime *m, void *_data)
{
	if (m->mime_part_number == extract_part)
		fwrite(m->decode.buffer, 1, m->decode.length, stdout);
}

void
processInput(Mime *m, FILE *fp)
{
	int ch;

	if (fp != NULL) {
		mimeReset(m);

		do {
			ch = fgetc(fp);
			(void) mimeNextCh(m, ch);
		} while (ch != EOF);

		(void) fflush(stdout);
	}
}

void
processFile(Mime *m, const char *filename)
{
	FILE *fp;

	if ((fp = fopen(filename, "r")) == NULL) {
		fprintf(stderr, "%s: %s (%d)\n", filename, strerror(errno), errno);
	} else {
		processInput(m, fp);
		(void) fclose(fp);
	}
}

int
main(int argc, char **argv)
{
	int ch;
	MimeHooks hook;
	md5_mime_state md5;
	MimeHooks md5_hook = { (void *)&md5, NULL, NULL, md5_body_start, md5_body_finish, md5_source_flush, md5_decode_flush };

	while ((ch = getopt(argc, argv, "dlmp:")) != -1) {
		switch (ch) {
		case 'd':
			enable_decode = 1;
			break;
		case 'l':
			list_parts = 1;
			break;
		case 'm':
			generate_md5 = 1;
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

	if ((mime = mimeCreate()) == NULL) {
		fprintf(stderr, "mimeCreate error: %s (%d)\n", strerror(errno), errno);
		exit(1);
	}

	memset(&hook, 0, sizeof (hook));

	if (list_parts) {
		hook.header = listHeaders;
		hook.body_start = listHeaders;
	} else if (enable_decode) {
		hook.decode_flush = printDecode;
	} else {
		hook.source_flush = printSource;
	}

	mimeHooksAdd(mime, &hook);

	if (generate_md5)
		mimeHooksAdd(mime, &md5_hook);

	if (optind + 1 == argc) {
		processFile(mime, argv[optind]);
	} else {
		processInput(mime, stdin);
	}

	mimeFree(mime);

	return 0;
}

#endif /* TEST */
