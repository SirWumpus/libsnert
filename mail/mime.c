/*
 * mime.c
 *
 * RFC 2045, 2046, 2047
 *
 * Copyright 2007, 2014 by Anthony Howe. All rights reserved.
 */

/*
 * Define style of headers container.
 */
#define	HDRS_ARRAY_STRINGS	0	/* Ordered */
#define HDRS_ARRAY_OBJECTS	1	/* Ordered */
#define	HDRS_OBJECT		2	/* Excludes Resent-* blocks and unordered headers. */

#ifndef HEADERS_OBJECT
#define HEADERS_OBJECT		HDRS_OBJECT
#endif

#define MIME_STRICT_BOUNDARY	mimeStrictBoundary
#define MIME_NOSPACE_BOUNDARY	mimeNoSpaceBoundary
#define MIME_WEAK_BOUNDARY	mimeWeakBoundary

#ifndef MIME_BOUNDARY
#define MIME_BOUNDARY		MIME_NOSPACE_BOUNDARY
#endif

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <ctype.h>
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_SYSLOG_H
# include <syslog.h>
#endif

#include <com/snert/lib/io/Log.h>
#include <com/snert/lib/mail/mime.h>
#include <com/snert/lib/util/Text.h>
#include <com/snert/lib/sys/sysexits.h>

#ifdef DEBUG_MALLOC
#include <com/snert/lib/util/DebugMalloc.h>
#endif

/***********************************************************************
 *** MIME Decoders & States
 ***********************************************************************/

#define LOGVOL(volume, ...) \
	if (volume < debug) syslog(LOG_DEBUG, __VA_ARGS__)

#define LOGIF(...) \
	LOGVOL(0, __VA_ARGS__)

#define LOGHOOK(m) \
	LOGVOL(1, "%s(0x%lX)", __func__, (long) m)

#define LOGCB(m, data) \
	LOGVOL(1, "%s(0x%lX, 0x%lX)", __func__, (long) m, (long) data)

#define LOGSTATE(m, ch) \
	LOGVOL(2, "%s(0x%lX, 0x%X '%c')", __func__, (long) m, ch, isprint(ch) ? ch : ' ')

#define LOGTRACE() \
	LOGVOL(3, "%s:%d", __func__, __LINE__)

static int debug;

static int mimeStateHdr(Mime *m, int ch);
static int mimeStateBdy(Mime *m, int ch);
static int mimeStateHdrStart(Mime *m, int ch);
static int mimeStateQpEqual(Mime *m, int ch);
static int mimeStateHdrValue(Mime *m, int ch);

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
mimeDebug(int level)
{
	debug = level;
}

static void
mimeDoHook(Mime *m, ptrdiff_t func_off)
{
	MimeHook func;
	ptrdiff_t free_off;
	MimeHooks *hook, *next;

	LOGIF("%s(0x%lX, %ld)", __func__, (long)m, (long)func_off);

	free_off = offsetof(MimeHooks, free_hook);
	for (hook = m->mime_hook; hook != NULL; hook = next) {
		next = hook->next;
		func = *(MimeHook *)((char *)hook + func_off);
		if (func != NULL)
			(*func)(m, func_off == free_off ? hook : hook->data);
	}
}

static void
mimeDoHookOctet(Mime *m, int ch, ptrdiff_t func_off)
{
	MimeHooks *hook;
	MimeHookOctet func;

	LOGIF("%s(0x%lX, %d, %ld)", __func__, (long)m, ch, (long)func_off);

	if (ch == EOF)
		return;
	for (hook = m->mime_hook; hook != NULL; hook = hook->next) {
		func = *(MimeHookOctet *)((char *)hook + func_off);
		if (func != NULL)
			(*func)(m, ch, hook->data);
	}
}

static int
mimeDecodeState(Mime *m, int ch)
{
	mimeDoHookOctet(m, ch, offsetof(MimeHooks, source_octet));
	return (*m->state.decode_state)(m, ch);
}

void
mimeHdrStart(Mime *m)
{
	LOGHOOK(m);
	mimeDoHook(m, offsetof(MimeHooks, hdr_start));
}

void
mimeHdrFinish(Mime *m)
{
	LOGHOOK(m);
	mimeDoHook(m, offsetof(MimeHooks, hdr_finish));
}

void
mimeDecodeFlush(Mime *m)
{
	LOGHOOK(m);
	if (0 < m->decode.length) {
		if (!mimeIsAnyHeader(m)) {
			mimeDoHook(m, offsetof(MimeHooks, decode_flush));
		}
		MEMSET(m->decode.buffer, 0, m->decode.length);
		m->decode.length = 0;
	}
}

static void
mimeDecodeAppend(Mime *m, int ch)
{
	LOGSTATE(m, ch);

	mimeDoHookOctet(m, ch, offsetof(MimeHooks, decoded_octet));

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
	LOGSTATE(m, ch);

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
	LOGSTATE(m, ch);

	ch = b64Decode(&m->state.b64, ch);

	/* Ignore intermediate base64 states until an octet is decoded. */
	if (BASE64_IS_OCTET(ch) || ch == BASE64_EOF)
		(void) mimeDecodeAdd(m, ch);

	return 0;
}

static int
mimeStateQpLiteral(Mime *m, int ch)
{
	LOGSTATE(m, ch);

	if (ch == '=')
		m->state.decode_state = mimeStateQpEqual;
	else
		mimeDecodeAdd(m, ch);

	return 0;
}

static int
mimeStateQpSoftLine(Mime *m, int ch)
{
	LOGSTATE(m, ch);

	return mimeStateQpLiteral(m, ch);
}

static int
mimeStateQpDecode(Mime *m, int ch)
{
	unsigned value;

	LOGSTATE(m, ch);

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
	LOGSTATE(m, ch);

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
	LOGHOOK(m);
	if (0 < m->source.length) {
		if (!mimeIsAnyHeader(m)) {
			m->source.buffer[m->source.length] = '\0';
			mimeDoHook(m, offsetof(MimeHooks, source_flush));
		}
		MEMSET(m->source.buffer, 0, m->source.length);
		m->source.length = 0;
	}
}

void
mimeBodyStart(Mime *m)
{
	LOGHOOK(m);
	mimeDoHook(m, offsetof(MimeHooks, body_start));
}

void
mimeBodyFinish(Mime *m)
{
	LOGHOOK(m);
	if (!mimeIsAnyHeader(m)) {
		mimeDoHook(m, offsetof(MimeHooks, body_finish));
	}
}

/* RFC 2046 section 5.1.1
 *
 *   The boundary delimiter line is then defined as a line
 *   consisting entirely of two hyphen characters ("-",
 *   decimal value 45) followed by the boundary parameter
 *   value from the Content-Type header field, optional
 *   linear whitespace, and a terminating CRLF.
 *
 *   The only mandatory global parameter for the "multipart"
 *   media type is the boundary parameter, which consists of
 *   1 to 70 characters from a set of characters known to be
 *   very robust through mail gateways, and NOT ending with
 *   white space.
 *
 *   boundary := 0*69<bchars> bcharsnospace
 *
 *   bchars := bcharsnospace / " "
 *
 *   bcharsnospace := DIGIT / ALPHA / "'" / "(" / ")" /
 *		      "+" / "_" / "," / "-" / "." /
 *		      "/" / ":" / "=" / "?"
 *
 * There are several ASCII punctuation characters that are not
 * included in the set of boundary characters. Thunderbird MUA
 * allows curly-braces and possibly all the printable ASCII
 * characters in the boundary, which is too permissive.
 */
#define BCHARSNOSPACE		"-=_'()+,./:?"
#define BCHARS_UNUSED		"!#$%&*;<>@[]^{|}~"
#define BCHARS_QUOTE		"\""

typedef int (*MimeIsBoundaryChar)(int);

/* 1.75.19
 *
 * Relaxed the handling of MIME boundaries to allow any printable
 * ASCII character to be used following the initial "--". RFC 2046
 * section 5.1.1 limits the set of boundary characters to those
 * that are safe for mail gateways. However, Thunderbird MTA is
 * permissive about boundary characters and possibly other	MUAs.
 * So milter-link or smtpf failing to parse a message with unusual
 * MIME boundaries could mean failing to detect spam messages that
 * would go on to be displayed by an MUA. Reported by Alex Broens.
 */
static int
mimeWeakBoundary(int ch)
{
	/* Any glyph, execpt SGML/HTML/XML angle brackets. */
 	return isgraph(ch) && ch != '<' && ch != '>';
}

static int
mimeNoSpaceBoundary(int ch)
{
	/* Almost strict, disallowing spaces permitted by the RFC.
	 * The RFC allowing spaces in boundaries seems so wrong
	 * and can't recall seeing them used in the wild in valid
	 * boundaries.
	 */
	return isalnum(ch) || strchr(BCHARSNOSPACE, ch) != NULL;
}

static int
mimeStrictBoundary(int ch)
{
	return isalnum(ch) || strchr(BCHARSNOSPACE " ", ch) != NULL;
}

static MimeIsBoundaryChar mimeIsBoundaryChar = MIME_BOUNDARY;

static int
mimeStateBoundary(Mime *m, int ch)
{
	LOGSTATE(m, ch);

	/* Accumulate the boundary line unitl LF or buffer full. */
	if (ch == ASCII_LF || sizeof (m->source.buffer) <= m->source.length) {
		unsigned i, has_qp = 0, all_hyphen = 1;

		/* RFC 2046 section 5.1.1
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
			if (!(*mimeIsBoundaryChar)(m->source.buffer[i])
			&& strchr("\r\n", m->source.buffer[i]) == NULL)
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
		 *
		 * e) Take care with delimiter lines.
		 *
		 *	Reply text here.
		 *
		 *	---- Original Message ----
		 *	Subject: Hello World!
		 *
		 * Some MUA embed the original message below the reply and
		 * a delimiter line, instead of the traditional ">" indenting.
		 */
		if (!all_hyphen && (m->state.encoding != MIME_QUOTED_PRINTABLE || !has_qp) && i == m->source.length) {
			LOGVOL(1, "boundary found");

			/* Terminate decoding for this body part */
			m->state.decode_state_cr = 0;
			m->state.decode_state = mimeDecodeAdd;
			(void) mimeDecodeState(m, EOF);

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
			m->state.source_state = mimeStateHdrStart;
		} else {
			LOGVOL(1, "not a boundary");

			/* Process the source stream before being flushed. */
			m->state.decode_state_cr = 0;
			m->state.source_state = mimeStateBdy;
			for (i = 0; i < m->source.length; i++) {
				(void) mimeDecodeState(m, m->source.buffer[i]);
			}
			if (ch != ASCII_LF)
				(void) mimeDecodeState(m, ch);
		}
	}

	return ch;
}

static int
mimeStateDash2(Mime *m, int ch)
{
	LOGSTATE(m, ch);

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
	if (isspace(ch) || strchr(BCHARS_UNUSED, ch) != NULL) {
		/* Not a MIME boundary. */
		(void) mimeDecodeState(m, ASCII_LF);
		(void) mimeDecodeState(m, '-');
		(void) mimeDecodeState(m, '-');
		(void) mimeDecodeState(m, ch);
		m->state.source_state = mimeStateBdy;
	} else {
		m->state.source_state = mimeStateBoundary;
	}

	return ch;
}

static int
mimeStateDash1(Mime *m, int ch)
{
	LOGSTATE(m, ch);

	if (ch == '-') {
		/* Second hyphen of boundary line? */
		m->state.source_state = mimeStateDash2;
	} else {
		(void) mimeDecodeState(m, ASCII_LF);
		(void) mimeDecodeState(m, '-');
		(void) mimeDecodeState(m, ch);
		m->state.source_state = mimeStateBdy;
	}

	return ch;
}

static int
mimeStateBdyLF(Mime *m, int ch)
{
	LOGSTATE(m, ch);

	if (m->state.is_multipart && ch == '-') {
		/* First hyphen of boundary line? */
		m->state.source_state = mimeStateDash1;
        } else if (ch == ASCII_LF) {
		/* We have a LF LF pair. MIME message with LF newlines?
		 * Flush the first LF and keep the second LF.
		 */
		(void) mimeDecodeState(m, ASCII_LF);
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

		(void) mimeDecodeState(m, ASCII_LF);
		(void) mimeDecodeState(m, ch);
		m->state.source_state = mimeStateBdy;
	}

	return ch;
}

static int
mimeStateBdy(Mime *m, int ch)
{
	LOGSTATE(m, ch);

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
			(void) mimeDecodeState(m, ASCII_LF);
		}
	} else {
		(void) mimeDecodeState(m, ch);
	}

	return ch;
}


static int
mimeStateHdrBdy(Mime *m, int ch)
{
	LOGSTATE(m, ch);

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
	LOGSTATE(m, ch);

	/* End of headers. */
	mimeDecodeFlush(m);
	m->mime_body_length = 0;
	m->mime_body_decoded_length = 0;
	m->source.buffer[0] = '\0';
	m->source.length = 0;

	mimeHdrFinish(m);
	mimeBodyStart(m);

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
	LOGSTATE(m, ch);

	if (ch == ASCII_CR) {
		/* Remain in this state waiting for the
		 * LF that marks end-of-headers.
		 */
		m->source.length--;
	} else if (ch == ASCII_SPACE || ch == ASCII_TAB) {
		/* Folded header, resume header gathering. */
		if (ch == ASCII_TAB)
			m->source.buffer[m->source.length-1] = ASCII_SPACE;
		m->state.source_state = mimeStateHdrValue;
	} else {
		if (0 < m->source.length) {
			/* End of unfolded header line less newline. */
			m->state.source_state = mimeStateHdr;
			m->source.buffer[--m->source.length] = '\0';

			LOGIF("%s header=%u:\"%s\"", __func__, m->source.length, m->source.buffer);

			/* Check the header for MIME behaviour. */
			if (0 <= TextFind((char *) m->source.buffer, "Content-Type:*multipart/*", m->source.length, 1)) {
				LOGIF("is_multipart = 1");
				m->state.is_multipart = 1;
				m->state.has_content_type = 1;
			} else if (0 <= TextFind((char *) m->source.buffer, "Content-Type:*message/rfc822", m->source.length, 1)) {
				LOGIF("is_message_rfc822 = 1");
				m->state.has_content_type = 1;
				m->state.is_message_rfc822 = 1;
			} else if (0 <= TextFind((char *) m->source.buffer, "Content-Type:*/*", m->source.length, 1)) {
				LOGIF("has_content_type = 1");
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
				LOGIF("decode_state = QP");
				m->state.decode_state = mimeStateQpLiteral;
				m->state.encoding = MIME_QUOTED_PRINTABLE;
			} else if (0 <= TextFind((char *) m->source.buffer, "Content-Transfer-Encoding:*base64*", m->source.length, 1)) {
				LOGIF("decode_state = B64");
				m->state.decode_state = mimeStateBase64;
				m->state.encoding = MIME_BASE64;
				b64Reset(&m->state.b64);
			}

			LOGHOOK(m);
			mimeDoHook(m, offsetof(MimeHooks, header));
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
mimeStateHdrValue(Mime *m, int ch)
{
	LOGSTATE(m, ch);

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

static int
mimeStateHdrStart(Mime *m, int ch)
{
	LOGSTATE(m, ch);
	m->state.source_state = mimeStateHdr;
	mimeDoHook(m, offsetof(MimeHooks, hdr_start));

	return mimeStateHdr(m, ch);
}

static int
mimeStateHdr(Mime *m, int ch)
{
	int i;

	LOGSTATE(m, ch);

	/* Colon delimiter between header name and value? */
	if (ch == ':'
	/* Or maybe the unix mailbox "From $address $ctime\n" line. */
	|| (ch == ' '
	  && m->mime_part_number == 0
	  && m->mime_part_length == 5
	  && strncmp((char *)m->source.buffer, "From ", 5) == 0)
	) {
		m->state.source_state = mimeStateHdrValue;
	} else if (ch == ASCII_CR) {
		;
	} else if (isspace(ch) || iscntrl(ch)) {
		if (ch == ASCII_LF) {
			if (m->source.length == 1)
				m->source.length--;
			else if (m->source.length == 2 && *m->source.buffer == ASCII_CR)
				m->source.length -= 2;
			else if (m->throw.ready)
				LONGJMP(m->throw.error, MIME_ERROR_NO_EOH);
		} else if (m->throw.ready) {
			LONGJMP(m->throw.error, MIME_ERROR_HEADER_NAME);
		}

		mimeHdrFinish(m);
		mimeBodyStart(m);
		m->state.source_state = mimeStateHdrBdy;
		for (i = 0; i < m->source.length; i++)
			(void) (*m->state.source_state)(m,  m->source.buffer[i]);
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
	LOGHOOK(m);
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

	LOGTRACE();

	if ((m = calloc(1, sizeof (*m))) != NULL) {
		mimeReset(m);
	}

	return m;
}

void
mimeHooksAdd(Mime *m, MimeHooks *hook)
{
	LOGTRACE();

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
	LOGHOOK(m);
	if (m != NULL) {
		mimeDoHook(m, offsetof(MimeHooks, free_hook));
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
	LOGHOOK(m);
	m->state.source_state = flag ? mimeStateHdrStart : mimeStateBdy;
}

/**
 * @param m
 *	Pointer to a Mime context structure.
 *
 * @return
 *	True if the parsing is still in message or MIME headers.
 */
int
mimeIsAnyHeader(Mime *m)
{
	LOGHOOK(m);
	return	m->state.source_state == mimeStateHdrStart
		|| m->state.source_state == mimeStateHdr
		|| m->state.source_state == mimeStateHdrValue
	;
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
	LOGHOOK(m);
	return m->mime_part_number == 0 && mimeIsAnyHeader(m);
}

void
mimeMsgStart(Mime *m)
{
	LOGHOOK(m);
	mimeReset(m);
	mimeDoHook(m, offsetof(MimeHooks, msg_start));
}

void
mimeMsgFinish(Mime *m)
{
	LOGHOOK(m);
	mimeDoHook(m, offsetof(MimeHooks, msg_finish));
}

/**
 * @param m
 *	Pointer to a Mime context structure.
 *
 * @param ch
 *	Next input octet to parse or EOF.
 *
 * @return
 *	Zero on success; otherwise non-zero on error.
 */
MimeErrorCode
mimeNextCh(Mime *m, int ch)
{
	LOGSTATE(m, ch);

	if (m == NULL) {
		if (m->throw.ready)
			LONGJMP(m->throw.error, MIME_ERROR_NULL);
		errno = EFAULT;
		return MIME_ERROR_NULL;
	}

	if (ch != EOF) {
		if (ch < 0 || 255 < ch) {
			if (m->throw.ready)
				LONGJMP(m->throw.error, MIME_ERROR_INVALID_BYTE);
			errno = EINVAL;
			return MIME_ERROR_INVALID_BYTE;
		}

		m->mime_part_length++;
		m->mime_body_length++;
		m->mime_message_length++;
		m->source.buffer[m->source.length++] = ch;

		(void) (*m->state.source_state)(m, ch);
	}

	if (ch == EOF) {
		(void) mimeDecodeState(m, EOF);
		mimeDecodeFlush(m);
	}

	if (ch == EOF || sizeof (m->source.buffer)-1 <= m->source.length)
		mimeSourceFlush(m);

	if (ch == EOF) {
		mimeBodyFinish(m);
	}

	return MIME_ERROR_OK;
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
int enable_throw;
int generate_md5;
int json_dump;
int hack_json_b64_strip_newline;

Mime *mime;


static char usage[] =
"usage: mime [-v] -l < message\n"
"       mime [-v] -p num [-dem] < message\n"
"       mime [-v] -j [-eN] < message\n"
"\n"
"-d\t\tdecode base64 or quoted-printable\n"
"-e\t\treport parsing errors\n"
"-j\t\tJSON dump\n"
"-l\t\tlist MIME part headers\n"
"-m\t\tgenerate MD5s for MIME part\n"
"-N\t\tstrip whitespace from encoded Base64 parts\n"
"-p num\t\textract MIME part\n"
"-v\t\tverbose\n"
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

	LOGCB(m, data);

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

	LOGCB(m, data);

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

	LOGCB(m, data);

	if (m->mime_part_number == extract_part)
		md5_append(&md5->source, m->source.buffer, m->source.length);
}

void
md5_decode_flush(Mime *m, void *data)
{
	md5_mime_state *md5 = data;

	LOGCB(m, data);

	if (m->mime_part_number == extract_part)
		md5_append(&md5->decode, m->decode.buffer, m->decode.length);
}

md5_mime_state md5_state;

MimeHooks md5_hook = {
	(void *)&md5_state,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	md5_body_start,
	md5_body_finish,
	md5_source_flush,
	md5_decode_flush,
	(MimeHookOctet) NULL,
	NULL
};

/*jslint white: true
 *
 * JSON Schema
 *
 * {
 *	"$schema": "http://json-schema.org/draft-04/schema#",
 *	"id": "message.json#",
 *	"description": "Array of MIME parts. Part 0 is the top level message's
 *	 headers and message body or MIME prologue for a multipart. The last
 *	 MIME part, the epilogue, is typically empty.",
 *	"type": "array",
 *	"items": {
 *	    "id": "#part",
 *	    "type": "object",
 *	    "properties": {
 *		"headers": {
 *		    "decription": "Some headers can be appear multiple times,
 *		     such as Received, Received-SPF, and the Resent-* family.
 *		     Original order has to be maintained for digital signatures
 *		     and Resent blocks.",
 *
 *		    "type": [ "array", "object", ],
 *
 *		    "items": {
 *			"id": "#header",
 *			"type": [ "string", "array", "object", ],
 *			"patternProperties": {
 *			    "[a-z][0-9a-z_]*": {
 *				"type": "string",
 *			    },
 *			},
 *			"required": [ "date", "from", "message_id", ],
 *		    },
 *		    "minItems": 1,
 *
 *		    "patternProperties": {
 *			"[a-z][0-9a-z_]*": {
 *			    "type": "string",
 *			},
 *		    },
 *		    "required": [ "date", "from", "message_id", ],
 *		},
 *		"body": {
 *		    "type": "string",
 *		},
 *	    },
 *	    "requires": [ "headers", "body", ],
 *	},
 *	"mimItems": 1,
 * }
 */

char headers_open[] = "[[{[";
char headers_close[] = "]]}]";

typedef struct {
	long hdrs_style;
	Vector received;
	Vector recipients;
} json_mime_state;

void
json_msg_start(Mime *m, void *data)
{
	LOGCB(m, data);
	printf("[\n");
}

void
json_msg_finish(Mime *m, void *data)
{
	LOGCB(m, data);
	printf("]\n");
}

char *
json_encode(const char *string, size_t length)
{
	char *encoded, *t;
	const char *s, *j;

	if ((encoded = malloc(length * 6 + 1)) != NULL) {
		t = encoded;
		for (s = string; *s != '\0'; s++) {
			if ((j = escapeJson(*s)) == NULL)
				continue;
			(void) strcpy(t, j);
			t += strlen(j);
		}
	}

	return encoded;
}

void
json_join_quoted_item(Buf *buf, const char *str, const char *indent)
{
	char *encoded;

	if ((encoded = json_encode(str, strlen(str))) != NULL) {
		(void) BufAddString(buf, indent);
		(void) BufAddString(buf, "\"");
		(void) BufAddString(buf, encoded);
		(void) BufAddString(buf, "\",\n");
		free(encoded);
	}
}

void
json_headers_start(Mime *m, void *data)
{
	json_mime_state *j = data;

	LOGCB(m, data);
	printf("\t{\n\t\t\"index\": %d,\n\t\t\"headers\": %c\n", m->mime_part_number, headers_open[j->hdrs_style]);
}

void
json_headers_finish(Mime *m, void *data)
{
	char **args;
	json_mime_state *j = data;

	LOGCB(m, data);
	if (m->mime_part_number == 0) {
		Buf join;

		if (j->hdrs_style == HDRS_OBJECT && !BufInit(&join, 100)) {
			if (0 < VectorLength(j->received)) {
				for (args = (char **)VectorBase(j->received); *args != NULL; args++) {
					json_join_quoted_item(&join, *args, "\t\t\t\t");
				}
				printf("\t\t\t\"received\": [\n%s\t\t\t],\n", BufBytes(&join));
			}
			BufSetLength(&join, 0);
			if (0 < VectorLength(j->recipients)) {
				for (args = (char **)VectorBase(j->recipients); *args != NULL; args++) {
					json_join_quoted_item(&join, *args, "\t\t\t\t");
				}
				printf("\t\t\t\"to\": [\n%s\t\t\t],\n", BufBytes(&join));
			}
			BufFini(&join);
		}
	}

	printf("\t\t%c,\n", headers_close[j->hdrs_style]);
}

void
json_header(Mime *m, void *data)
{
	char *js;
	int colon, spaces;
	json_mime_state *j = data;

	LOGCB(m, data);
	if ((js = json_encode((char *)m->source.buffer, m->source.length)) != NULL) {
		colon = strcspn(js, ":");
		spaces = strspn(js+colon+1, " \t")+1;

		if (0 < TextInsensitiveStartsWith(js, "Received:")) {
			(void) VectorAdd(j->received, TextDup(js+colon+spaces));
		}
		if (0 < TextInsensitiveStartsWith(js, "To:")) {
			char **items;
			Vector list = TextSplit(js+colon+spaces, ",", 0);
			for (items = (char **)VectorBase(list); *items != NULL; items++) {
				if (!VectorAdd(j->recipients, *items))
					*items = NULL;
			}
			VectorDestroy(list);
		}

		switch (j->hdrs_style) {
		case HDRS_OBJECT:
			if (TextInsensitiveStartsWith(js, "To") < 0
			/* Resent- header blocks, */
			&&  TextInsensitiveStartsWith(js, "Resent-") < 0
			/* Received, and Received-SPF. */
			&&  TextInsensitiveStartsWith(js, "Received") < 0
			) {
				TextLower(js, colon);
				printf("\t\t\t\"%.*s\": \"%s\",\n", colon, js, js+colon+spaces);
			}
			break;

		case HDRS_ARRAY_OBJECTS:
			TextLower(js, colon);
			printf("\t\t\t{ \"%.*s\": \"%s\", },\n", colon, js, js+colon+spaces);
			break;

		default:
			printf("\t\t\t\"%s\",\n", js);
		}
		free(js);
	}
}

void
json_body_start(Mime *m, void *data)
{
	LOGCB(m, data);
	printf("\t\t\"body\": \"");
}

void
json_body_finish(Mime *m, void *data)
{
	LOGCB(m, data);
	printf("\",\n\t},\n");
}

void
json_octet(Mime *m, int ch, void *data)
{
	LOGCB(m, data);
	if (0 <= ch) {
		if (hack_json_b64_strip_newline
		&& m->state.encoding == MIME_BASE64
		&& isspace(ch))
			return;
		(void) fputs(escapeJson(ch), stdout);
	}
}

json_mime_state json_state = { HEADERS_OBJECT };

MimeHooks json_hook = {
	(void *)&json_state,
	NULL,
	json_msg_start,
	json_msg_finish,
	json_headers_start,
	json_header,
	json_headers_finish,
	json_body_start,
	json_body_finish,
	NULL,
	NULL,
	json_octet,
	NULL,
	NULL
};

void
listHeaders(Mime *m, void *_data)
{
	LOGCB(m, _data);

	if (0 <= TextFind((char *) m->source.buffer, "Content-*", m->source.length, 1)
	||  0 <= TextFind((char *) m->source.buffer, "X-MD5-*", m->source.length, 1)
	)
		printf("%u: %s\r\n", m->mime_part_number, m->source.buffer);
}

void
listEndHeaders(Mime *m, void *_data)
{
	LOGCB(m, _data);

	printf("%u:\r\n", m->mime_part_number);
}

void
printSource(Mime *m, void *_data)
{
	LOGCB(m, _data);

	if (m->mime_part_number == extract_part)
		fwrite(m->source.buffer, 1, m->source.length, stdout);
}

void
printDecode(Mime *m, void *_data)
{
	LOGCB(m, _data);

	if (m->mime_part_number == extract_part)
		fwrite(m->decode.buffer, 1, m->decode.length, stdout);
}

void
printFinish(Mime *m, void *_data)
{
	LOGCB(m, _data);

	if (m->mime_part_number == extract_part && m->throw.ready)
		LONGJMP(m->throw.error, MIME_ERROR_BREAK);
}

void
processInput(Mime *m, FILE *fp)
{
	int ch;

	LOGTRACE();

	if (fp != NULL) {
		mimeMsgStart(m);
		do {
			ch = fgetc(fp);
			(void) mimeNextCh(m, ch);
		} while (ch != EOF);
		(void) fflush(stdout);
		mimeMsgFinish(m);
	}
}

void
processFile(Mime *m, const char *filename)
{
	FILE *fp;

	LOGTRACE();

	if ((fp = fopen(filename, "r")) == NULL) {
		fprintf(stderr, "%s: %s (%d)\n", filename, strerror(errno), errno);
	} else {
		processInput(m, fp);
		(void) fclose(fp);
	}
}

typedef struct {
	const char *name;
	MimeIsBoundaryChar fn;
} MimeBoundary;

MimeBoundary boundary[] = {
	{ "weak", mimeWeakBoundary },
	{ "nospace", mimeNoSpaceBoundary },
	{ "strict", mimeStrictBoundary },
	{ "strict|nospace|weak", NULL }
};

int
main(int argc, char **argv)
{
	int ch;
	MimeHooks hook;
	MimeBoundary *mb;

	while ((ch = getopt(argc, argv, "B:NdejJ:lmp:v")) != -1) {
		switch (ch) {
		case 'd':
			enable_decode = 1;
			break;
		case 'e':
			enable_throw = 1;
			break;
		case 'J':
			/* Undocumented option for experimenting. */
			json_state.hdrs_style = strtol(optarg, NULL, 10);
			/*@fallthrough@*/
		case 'j':
			json_dump = 1;
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
		case 'v':
			debug++;
			break;
		case 'N':
			hack_json_b64_strip_newline = 1;
			break;
		case 'B':
			/* Undocumented option for experimenting. */
			for (mb = boundary; mb->fn != NULL; mb++) {
				if (strcmp(optarg, mb->name) == 0) {
					mimeIsBoundaryChar = mb->fn;
					break;
				}
			}
			if (mb->fn == NULL) {
				(void) fprintf(stderr, "use -B %s\n", mb->name);
				exit(EX_USAGE);
			}
			break;
		default:
			(void) fprintf(stderr, usage);
			exit(EX_USAGE);
		}
	}

	if (!json_dump && !list_parts && extract_part < 0) {
		(void) fprintf(stderr, usage);
		exit(EX_USAGE);
	}
	if (json_state.hdrs_style < HDRS_ARRAY_STRINGS || HDRS_OBJECT < json_state.hdrs_style) {
		(void) fprintf(stderr, "-J must be 0, 1, or 2.\n");
		exit(EX_USAGE);
	}
	if (0 < debug) {
		LogOpen("(standard error)");
		LogSetProgramName("mime");
	}

	if ((mime = mimeCreate()) == NULL) {
		fprintf(stderr, "mimeCreate error: %s (%d)\n", strerror(errno), errno);
		exit(EX_SOFTWARE);
	}

	memset(&hook, 0, sizeof (hook));

	if (json_dump) {
		if ((json_state.recipients = VectorCreate(10)) == NULL
		||  (json_state.received = VectorCreate(10)) == NULL) {
			fprintf(stderr, "%s", strerror(errno));
			exit(EX_SOFTWARE);
		}

		if (enable_decode) {
			json_hook.source_octet = NULL;
			json_hook.decoded_octet = json_octet;
			/* Keep the newlines in decoded Base64 output. */
			hack_json_b64_strip_newline = 0;
		}

		mimeHooksAdd(mime, &json_hook);
	} else {
		if (list_parts) {
			hook.header = listHeaders;
			hook.body_start = listEndHeaders;
		} else if (enable_decode) {
			hook.decode_flush = printDecode;
			hook.body_finish = printFinish;
		} else {
			hook.source_flush = printSource;
			hook.body_finish = printFinish;
		}

		mimeHooksAdd(mime, &hook);

		if (generate_md5)
			mimeHooksAdd(mime, &md5_hook);
	}

	if (enable_throw) {
		if ((ch = SETJMP(mime->throw.error)) != MIME_ERROR_OK) {
			if (ch != MIME_ERROR_BREAK) {
				fprintf(stderr, "mime error: %d\n", ch);
				exit(EX_DATAERR);
			}
			goto done;
		}
		mime->throw.ready = 1;
	}

	if (optind + 1 == argc) {
		processFile(mime, argv[optind]);
	} else {
		processInput(mime, stdin);
	}
done:
	VectorDestroy(json_state.recipients);
	VectorDestroy(json_state.received);
	mimeFree(mime);

	return EX_OK;
}

#endif /* TEST */
