/*
 * parsePath.c
 *
 * Copyright 2002, 2013 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#ifndef __MINGW32__
# if defined(HAVE_SYSLOG_H)
#  include <syslog.h>
# endif
#endif

#include <com/snert/lib/io/Log.h>
#include <com/snert/lib/util/Text.h>
#include <com/snert/lib/mail/limits.h>
#include <com/snert/lib/mail/parsePath.h>
#include <com/snert/lib/net/network.h>

#ifdef DEBUG_MALLOC
#include <com/snert/lib/util/DebugMalloc.h>
#endif

/***********************************************************************
 *** Global Variables & Constants
 ***********************************************************************/

/* NOTE that the LOCAL_PART_LENGTH in RFC 2821 is limited to 64
 * octets, but some stupid mailing list management software use
 * Variable Envelope Return Paths (VERP) or the Sender Rewriting
 * Scheme (SRS), in order to encode another address within the
 * local-part of an address. If the address being encoded is
 * already EMAIL_LENGTH octets in length, then the new encoded
 * address will NOT be conformant with RFC 2821 size limits for
 * the local-part.
 *
 * The LOCAL_PART_LENGTH here is doubled as a partial concesion
 * to these popular yet broken schemes. It appears to work for
 * majority of cases.
 */
#define LOCAL_PART_LENGTH	(2*SMTP_LOCAL_PART_LENGTH)

static char empty[] = "";
static const string emptyString = { empty, 0 };
static const ParsePath nullPath = {
	0, 0,
#ifdef STRIP_IP_AS_DOMAIN
	0,
#endif
	{ empty, 0 },
	{ empty, 0 },
	{ empty, 0 },
	{ empty, 0 },
	{ empty, 0 }
};

/***********************************************************************
 *** Routines
 ***********************************************************************/

static int debug = 0;

void
parsePathSetDebug(int level)
{
	debug = level;
}

/**
 * Find the right inner most path.
 *
 *	<>				<>
 *	<<<>>>				<>
 *	<<<a>>>				<a>
 *	a <b> c				<b>
 *	<a <b> c>			<b>
 *	<a <b <c>>>			<c>
 *	<a> <b>				<b>
 *	<a <b> <c> d>			<c>
 *	<a <b> <c <d <e> f> g> h>	<e>
 *
 *	a@b.c				a@b.c
 *	empty string			empty string
 *
 * Strange but valid:
 *
 *	<"<>"@plab.ku.dk>		<"<>"@plab.ku.dk>
 *
 * @param path
 *	An envelope path as defined by RFC 2821.
 *
 * @param start
 *	A pointer passed back to the caller, which points to the
 * 	the start of the path within the path argument.
 *
 * @param stop
 *	A pointer passed back to the caller, which points one past
 *	the end of the path such that stop - start = length.
 *
 * @return
 *	Zero on success or -1 imbalanced angle brackets.
 */
int
findInnerPath(const char *path, const char **start, const char **stop)
{
	int depth = 0;
	int quote = 0;

	if (path == NULL || start == NULL || stop == NULL) {
		errno = EFAULT;
		return -1;
	}

	*start = path;
	*stop = NULL;

	for ( ; *path != '\0'; path++) {
		switch (*path) {
		case '"':
			quote = !quote;
			break;
		case '\\':
			if (path[1] != '\0')
				path++;
			break;
		case '<':
			if (!quote) {
				depth++;
				*start = path;
			}
			break;
		case '>':
			if (!quote) {
				if (*stop <= *start)
					*stop = path+1;
				depth--;
			}
			break;
		}
	}

	if (*stop == NULL)
		*stop = path;

	return -(depth != 0);
}

/**
 * Parse an envelope path into its component parts.
 *
 * @param path
 *	An envelope path as defined by RFC 2821.
 *
 * @param flags
 *	A bit mask of flags used to enable/disable specific tests. Supported
 *	flags are STRICT_ANGLE_BRACKETS, STRICT_LOCAL_LENGTH, STRICT_DOMAIN_LENGTH,
 *	STRICT_LITERAL_PLUS, STRICT_ADDR_SPEC, STRICT_MIN_DOTS.
 *
 * @param dots
 *	The minimum number of dots expected in the domain portion of the
 *	path. For a sender adderss this value is typically 1 and for a;
 *	recipient its typically 0, which allows for <postmaster> form.
 *
 * @param out
 *	A pointer to a ParsePath pointer used to pass back an allocated
 *	ParsePath structure to the caller. Its the caller's responsibility
 *	to free() this structure.
 *
 * @return
 *	NULL on success. Otherwise an error string suitable as an SMTP error
 *	reply message, in which case the reply code should be 553.
 */
const char *
parsePath(const char *path, unsigned long flags, int dots, ParsePath **out)
{
	ParsePath *p;
	const char *start, *stop, *error = NULL;
	int hasAtSign, hasPlusSign, isUnqualified;

	if (1 < debug)
		syslog(LOG_DEBUG, "enter parsePath(%s, %lx, %d, %lx)", path, flags, dots, (long) out);

	/*@-mustdefine@*/
	if (path == NULL || out == NULL) {
		errno = EFAULT;
		error = "4.0.0 internal error, invalid arguments";
		goto error0;
	}
	/*@=mustdefine@*/

	*out = NULL;

	/* Apparently there is some old mail server software that specifies:
	 *
	 *	MAIL FROM:<john smith <jsmith@domain>>
	 *
	 * Which is so non-conformant with RFC 2821. Since sendmail supports
	 * this archaic form, I've opted to support it for now on the grounds
	 * that the necessary information can still be found within.
	 */
	if (findInnerPath(path, &start, &stop)) {
		error = "5.1.0 imbalanced angle brackets in path";
		goto error0;
	}

	if (1 < debug)
		syslog(LOG_DEBUG, "*start=%c", *start);

	/* TEST that the path, as defined by the RFC 2821 grammar, begins
	 * and ends with angles brackets. Some mail servers relax this
	 * requirement allowing for MAIL FROM:address or RCPT TO:address
	 * instead of MAIL FROM:<address> or RCPT TO:<address>.
	 */
	if (flags & STRICT_ANGLE_BRACKETS) {
		if (*start != '<' || (start < stop && stop[-1] != '>')) {
			error = "5.1.0 address does not conform to RFC 2821 syntax";
			goto error0;
		}
	}

	/* Remove/skip the angle brackets. */
	stop -= (start < stop && stop[-1] == '>');
	start += (*start == '<');

	/* Optionally ignore leading and trailing whitespace within the
	 * angle brackets. eg. MAIL FROM:<  suspect@example.com  >
	 */
	if (!(flags & STRICT_ADDR_SPEC)) {
		start += strspn(start, " \t\r\n\f");
		stop = start + strlrspn(start, stop - start, " \t\r\n\f");
	}

	if (1 < debug)
		syslog(LOG_DEBUG, "call calloc(1, %lu)", (unsigned long)(sizeof(ParsePath) + (stop - start + 1) * 2));

	/* Allocate enough space for the structure and the string data.
	 * While more complex to setup, it allows for a single call to
	 * free() to release the structure and the strings it points to.
	 */
	if ((p = calloc(1, sizeof(ParsePath) + (stop - start + 1) * 2)) == NULL) {
		error = "4.3.0 internal error, out of memory";
		goto error0;
	}

	if (1 < debug)
		syslog(LOG_DEBUG, "start equals stop=%d", start == stop);

	/* Check for the null address. */
	if (start == stop) {
		/*@-observertrans@*/
		p->address.string 	=
		p->sourceRoute.string 	=
		p->localLeft.string 	=
		p->localRight.string 	=
		p->domain.string 	= empty;
		/*@=observertrans@*/
		goto done;
	}

	if (1 < debug)
		syslog(LOG_DEBUG, "Place a copy of the address string after the structure.");

	/* Place a copy of the address string after the structure. */
	p->address.length = stop - start;

	if (1 < debug)
		syslog(LOG_DEBUG, "address-length=%ld", p->address.length);

	p->address.string = (char *)(&p[1]);

	/*@-modobserver -observertrans@*/
	(void) strncpy(p->address.string, start, (size_t) p->address.length);
	/*@=modobserver =observertrans@*/

	if (1 < debug)
		syslog(LOG_DEBUG, "address-string='%s'", p->address.string);

	/* Place a second copy following the previous. */
	p->sourceRoute.string = &p->address.string[p->address.length + 1];

	/*@-modobserver -observertrans */
	(void) strncpy(p->sourceRoute.string, p->address.string, (size_t) p->address.length);
#ifdef LOWER_WHOLE_PATH
	TextLower(p->sourceRoute.string, -1);
#endif
	/*@=modobserver =observertrans@*/

	if (1 < debug)
		syslog(LOG_DEBUG, "Split the local-part at a plus-sign or at-sign.");

	p->sourceRoute.length = spanSourceRoute((const unsigned char *)p->sourceRoute.string);
	p->localLeft.string = &p->sourceRoute.string[p->sourceRoute.length];

	if (0 < p->sourceRoute.length) {
		if (p->sourceRoute.string[p->sourceRoute.length] != ':') {
			error = "5.1.0 invalid source route";
			goto error1;
		}

		p->sourceRoute.string[p->sourceRoute.length] = '\0';
		p->localLeft.string++;
	} else {
		/* Backup and point at the null byte of the previous string. */
		p->sourceRoute.string--;
	}

	if (1 < debug)
		syslog(LOG_DEBUG, "source-route-string='%s' source-route-length=%ld", p->sourceRoute.string, p->sourceRoute.length);

	/* Split the local-part at a plus-sign or at-sign. */
	p->localRight.length = spanLocalPart((const unsigned char *)p->localLeft.string);
	hasAtSign = p->localLeft.string[p->localRight.length] == '@';
	isUnqualified = p->localLeft.string[p->localRight.length] == '\0';
	p->localLeft.string[p->localRight.length] = '\0';

	if (flags & STRICT_LITERAL_PLUS)
		p->localLeft.length = p->localRight.length;
	else
		p->localLeft.length = (long) strcspn(p->localLeft.string, "+");

	if (1 < debug)
		syslog(LOG_DEBUG, "left-length=%ld right-length=%ld", p->localLeft.length, p->localRight.length);

	if ((flags & STRICT_LOCAL_LENGTH) && SMTP_LOCAL_PART_LENGTH < p->localRight.length) {
		error = "5.1.0 local-part too long, see RFC 2821 section 4.5.3.1";
		goto error1;
	}

	hasPlusSign = p->localLeft.length < p->localRight.length;

	/* TEST for the presence of an at-sign or that the local part is
	 * terminated at the null byte (we stripped the angle brackets
	 * above), which allows for <local> form.
	 */
	if (!hasAtSign && !isUnqualified) {
		/* An illegal character was found. */
		/* NOTE that dots == 0 implies a RCPT otherwise a MAIL. */
		error = dots <= 0 ? "5.1.3 invalid local part" : "5.1.7 invalid local part";
		goto error1;
	}

	p->domain.string = &p->localLeft.string[p->localRight.length + hasAtSign];
	p->localRight.string = &p->localLeft.string[p->localLeft.length + hasPlusSign];

	/* TEST that the domain name is within RFC 2821 limits and has
	 * at least the demanded number of dots. For example we would
	 * disallow MAIL FROM:<local>, but allow RCPT TO:<local>.
	 */
	p->domain.length = spanDomain((const unsigned char *)p->domain.string, dots);
/*	p->domain.length = p->address.length - p->localRight.length - hasAtSign; */

	if (1 < debug)
		syslog(LOG_DEBUG, "domain-length=%ld", p->domain.length);

	if ((flags & STRICT_DOMAIN_LENGTH) && SMTP_DOMAIN_LENGTH < p->domain.length) {
		error = "5.1.0 domain name too long, see RFC 2821 section 4.5.3.1";
		goto error1;
	}

	if ((flags & STRICT_MIN_DOTS) && 0 < dots && p->domain.length <= 0) {
		/* We want some dots in the domain after an @-sign. */
		error = "5.1.7 address incomplete";
		goto error1;
	}

	if (p->domain.string[p->domain.length] != '\0') {
		/* An illegal character was found. */
		/* NOTE that dots == 0 implies a RCPT otherwise a MAIL. */
		error = dots <= 0 ? "5.1.3 invalid domain name" : "5.1.7 invalid domain name";
		goto error1;
	}

	/* Adjust local-right-side length. */
	p->localRight.length -= p->localLeft.length + hasPlusSign;

	/*@-modobserver -observertrans */
	/* Split the string into local-part and domain at the at-sign. */
	p->localRight.string[p->localRight.length] = '\0';

	/* Split the string into local-left and local-right at the plus-sign if present. */
	p->localLeft.string[p->localLeft.length] = '\0';
	/*@=modobserver =observertrans */

#ifdef STRIP_IP_AS_DOMAIN
/* The .isDomainAnIP flag doesn't appear to be used and it makes
 * more sense to retain the square brackets around the IP address
 * for the domain name. parseIPv6() handles IP-as-domain.
 */

	/* Check for ip-as-domain surrounded by square brackets. */
	p->isDomainAnIP = (p->domain.string[0] == '[' && p->domain.string[p->domain.length - 1] == ']');

	if (p->isDomainAnIP) {
		/* Remove the square brackets from around the IP address. */
		p->domain.length -= 2;
		/*@-modobserver -observertrans */
		(void) memmove(p->domain.string, p->domain.string+1, (size_t) p->domain.length);
		p->domain.string[p->domain.length] = '\0';
		/*@=modobserver =observertrans */
	}
#endif
	TextLower(p->sourceRoute.string, -1);
	TextLower(p->localLeft.string, -1);
	TextLower(p->domain.string, -1);

	if (error != NULL) {
error1:
		free(p);
		p = NULL;
	}
done:
	*out = p;
error0:
	if (1 < debug)
		syslog(LOG_DEBUG, "exit parsePath(%s, %lx, %d, %lx) error=\"%s\"", path, flags, dots, (long) out, error);

	return error;
}

/**
 * @param buffer
 *	A string buffer into which the formatted path will be copied.
 *	The string is always terminate by a null byte.
 *
 * @param length
 *	The size of the string buffer.
 *
 * @param fmt
 * 	The format string comprises of literal character and
 *	percent-sign (%) prefixed format characters:
 *
 *	%%		A literal percent-sign (%)
 *	%A		The original address, equivalent of %T%P@%D
 *	%D		The domain name portion.
 *	%L		The left-hand-side of a plus-detailed
 *			address or the entire local part if
 *			there was no plus-detail.
 *	%P		The local-part. If %R is not empty
 *			then "%L+%R" else "%L".
 *	%R		The right-hand-side of a plus detailed
 *			address or the empty string.
 *	%S		The source-route, ie "@A,@B,@C", or the
 * 			empty string.
 *	%T		If %S is not empty, then "%S:" else the
 *			empty string.
 *
 * 	Some examples:
 *
 *	Source			Format		Result
 *
 *	foo@domain		%P@%D		foo@domain
 *	foo+bar@domain		%P@%D		foo+bar@domain
 *	foo+bar@domain		%L+bulk@%D	foo+bulk@domain
 *	foo+bar@domain		bulk+%L@%D	bulk+foo@domain
 *	foo+bar@domain		bulk+%R@%D	bulk+bar@domain
 *	foo+bar@domain		%P@bulk.%D	foo+bar@bulk.domain
 *
 * @param p
 *	A pointer to a ParsePath structure previously assigned by
 *	parsePath().
 *
 * @return
 *	The number of octets copied excluding the null byte; otherwise
 *	-1 indicates an argument error.
 */
long
formatPath(char *buffer, long length, const char *fmt, ParsePath *p)
{
	long i;

	if (1 < debug)
		syslog(LOG_DEBUG, "enter formatPath(%lx, %ld, %s, %lx)", (long) buffer, length, fmt, (long) p);

	if (buffer == NULL || length <= 0 || fmt == NULL || p == NULL) {
		errno = EFAULT;
		i = -1;
		goto error0;
	}

	if (p->address.string == NULL)
		p->address = emptyString;

	if (p->localLeft.string == NULL)
		p->localLeft = emptyString;

	if (p->localRight.string == NULL)
		p->localRight = emptyString;

	if (p->domain.string == NULL)
		p->domain = emptyString;

	if (p->sourceRoute.string == NULL)
		p->sourceRoute = emptyString;

	for (i = 0; *fmt != '\0' && i < length; fmt++) {
		if (*fmt != '%') {
			/* Copy a literal character into the buffer.
			 */
			buffer[i++] = *fmt;
			continue;
		}

		/* Percent format character. */
		switch (*++fmt) {
		case '%':
			/* A percent literal. */
			buffer[i++] = *fmt;
			break;
		case 'A':
			i += TextCopy(buffer + i, length - i, p->address.string);
			break;
		case 'D':
			/* Copy the domain name into the buffer. */
#ifdef STRIP_IP_AS_DOMAIN
			if (i+1 < length && p->isDomainAnIP)
				buffer[i++] = '[';
#endif
			i += TextCopy(buffer + i, length - i, p->domain.string);

#ifdef STRIP_IP_AS_DOMAIN
			if (i+1 < length && p->isDomainAnIP)
				buffer[i++] = ']';
#endif
			break;
		case 'L':
			/* Copy the left-hand-string into the buffer.
			 * This is either the left-hand-side of a plus
			 * separated email address or the entire local-
			 * part.
			 */
			/*@fallthrough@*/
		case 'P':
			/* Copy local-part into the buffer. If %R is
			 * not empty then its "%L+%R" else "%L".
			 */
			i += TextCopy(buffer + i, length - i, p->localLeft.string);

			if (*fmt == 'P' && i+1 < length && *p->localRight.string != '\0') {
				buffer[i++] = '+';
				/*@fallthrough@*/
		case 'R':
			/* Copy the right-hand-string into the buffer.
			 * This is the right-hand-side of a plus
			 * separated email address or the empty string.
			 */
				i += TextCopy(buffer + i, length - i, p->localRight.string);
			}
			break;
		case 'S':
			/* Copy the source-route into the buffer. This is
			 * either an at-domain-list or the empty string.
			 */
			/*@fallthrough@*/
		case 'T':
			/* Copy the source-route into the buffer. If %S is
			 * not empty then its "%S:" else the empty string.
			 */
			i += TextCopy(buffer + i, length - i, p->sourceRoute.string);

			if (*fmt == 'T' && i+1 < length && *p->sourceRoute.string != '\0')
				buffer[i++] = ':';
			break;
		}
	}

	/* Make sure the string is terminated. */
	buffer[i - (length <= i)] = '\0';
error0:
	if (1 < debug)
		syslog(LOG_DEBUG, "exit  formatPath(%lx, %ld, %s, %lx) length=%ld", (long) buffer, length, fmt, (long) p, i);

	return i;
}

/**
 * @param fmt
 * 	The format string comprises of literal character and
 *	percent-sign (%) prefixed format characters. See formatPath().
 *
 * @param p
 *	A pointer to a ParsePath structure previously assigned by
 *	parsePath().
 *
 * @return
 *	The computed length of a formatted path string.
 *
 * @see formatPath()
 */
long
formatPathLength(const char *fmt, ParsePath *p)
{
	size_t length;

	if (fmt == NULL || p == NULL) {
		errno = EFAULT;
		return 0;
	}

	for (length = 0; *fmt != '\0'; fmt++) {
		if (*fmt != '%') {
			/* Copy a literal character into the buffer.
			 */
			length++;
			continue;
		}

		/* Percent format character. */
		switch (*++fmt) {
		case '%':
			/* A percent literal. */
			length++;
			break;
		case 'A':
			length += p->address.length;
			break;
		case 'D':
			/* Copy the domain name into the buffer. */
#ifdef STRIP_IP_AS_DOMAIN
			length += p->domain.length + (p->isDomainAnIP ? 2 : 0);
#else
			length += p->domain.length;
#endif
			break;
		case 'L':
			/* Copy the left-hand-string into the buffer.
			 * This is either the left-hand-side of a plus
			 * separated email address or the entire local-
			 * part.
			 */
			/*@fallthrough@*/
		case 'P':
			/* Copy local-part into the buffer. If %R is
			 * not empty then its "%L+%R" else "%L".
			 */
			length += p->localLeft.length;

			if (*fmt == 'P' && *p->localRight.string != '\0') {
				length += 1;
				/*@fallthrough@*/
		case 'R':
			/* Copy the right-hand-string into the buffer.
			 * This is the right-hand-side of a plus
			 * separated email address or the empty string.
			 */
				length += p->localRight.length;
			}
			break;
		case 'S':
			/* Copy the source-route into the buffer. This is
			 * either an at-domain-list or the empty string.
			 */
			/*@fallthrough@*/
		case 'T':
			/* Copy the source-route into the buffer. If %S is
			 * not empty then its "%S:" else the empty string.
			 */
			if (*p->sourceRoute.string != '\0')
				length += p->sourceRoute.length + (*fmt == 'T');
			break;
		}
	}

	return (long) length;
}

/**
 * @param fmt
 * 	The format string comprises of literal character and
 *	percent-sign (%) prefixed format characters. See formatPath().
 *
 * @param p
 *	A pointer to a ParsePath structure previously assigned by
 *	parsePath().
 *
 * @return
 *	An allocated string, which is the caller's responsibility
 *	to free; otherwise NULL on error.
 *
 * @see formatPath()
 */
char *
allocatePath(const char *fmt, ParsePath *p)
{
	char *path;
	long length, nbytes;

	if (fmt == NULL || p == NULL) {
		errno = EFAULT;
		return NULL;
	}

	length = formatPathLength(fmt, p);
	if ((path = malloc(length+1)) != NULL) {
		 if ((nbytes = formatPath(path, length+1, fmt, p)) != length) {
			syslog(LOG_ERR, "length mismatch, formatPathLength=%ld formatPath=%ld", length, nbytes);
			errno = EFAULT;
		 	free(path);
			return NULL;
		 }
	}

	return path;
}

#ifdef TEST
#include <stdio.h>
#include <com/snert/lib/util/getopt.h>
#include <com/snert/lib/util/setBitWord.h>

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

struct test_path {
	long expect;
	char *path;
};

static char *regression[] = {
	"parsePath",

	/* Always Good */
	"<>",
	"<postmaster>",
	"<\"<>\"@plab.ku.dk>",

	/* The following is not RFC 2822 compliant, but accepted
	 * by sendmail. The grammar for the local-part only allows
	 * backslash escape within a quoted string,
	 */
		"<escape\\ space@pan.olsztyn.pl>",
		"<escape\\ spa+ce@pan.olsztyn.pl>",
		"<esc+ape\\ space@pan.olsztyn.pl>",
	"<\"quoted\\backslash\"@pan.olsztyn.pl>",
	"<\"quoted space\"@example.com>",
	"<\"Andreas-TRANSMAR\\(Fax del trabajo\\)\"@dmz-113665s.salvesen.com>",
	"<achowe@snert.com>",
	"<achowe@pop.snert.com>",
	"<anthony.howe@pop.snert.com>",
	"<anthony.howe+folder@pop.snert.com>",

	/* Source routed. */
	"<@LBDRSCS.LBDCVM.STATE.NY.US:VMMAIL@LBDC.STATE.NY.US>",
	"<@a.com,@b.net,@c.org:VMMAIL@LBDC.STATE.NY.US>",

	/* Apostrophe in local-part. */
	"<Micheal.O'Rouke@someplace.ie>",

	/* IP address literals */
	"<achowe@[123.45.67.89]>",
	"<achowe@[IPv6:::]>",
	"<achowe@[IPv6:1:2:3:4:5:6:7:8]>",
	"<achowe@[IPv6:::1]>",
	"<achowe@[IPv6:feed::beef]>",
	"<achowe@[IPv6:::123.45.67.89]>",
	"<achowe@[IPv6:beef::123.45.67.89]>",

	/* Always Good and RFC 2606 is TRUE. */
	"<achowe@dummy.test>",
	"<achowe@dummy.invalid>",
	"<achowe@dummy.localhost>",
	"<achowe@dummy.example>",
	"<achowe@example.com>",
	"<achowe@sub.example.com>",

	/* Bad unless STRICT_LITERAL_PLUS is set */
	"<+folder@pop.snert.com>",

	/* Bad if STRICT_ANGLE_BRACKETS, STRICT_ADDR_SPEC is set. */
	"",
	"postmaster",
	"achowe@snert.com",
	"    achowe@snert.com    ",
	"<    achowe@snert.com>",
	"<achowe@snert.com    >",
	"<    achowe@snert.com    >",

	/* Always Bad */
	"<  __achowe@snert.com>",		/* leading spaces within angle brackets */
	"<achowe@snert.com__  >",		/* trailing spaces within angle brackets */
	"<a chowe@snert.com>",			/* unquoted space in address */
	"<\"achowe@snert.com\">",		/* quoting only applies to local-part */
	"<achowe@[IPv6:feed:::beef]>",		/* too many colons */
	"<achowe@[IPv6:1:2:3:4:5:6:7]>",	/* too few IPv6 words */
	"<achowe@[IPv6:1:2:3:4:5:6:7:]>",	/* missing word after colon */
	"<achowe@[IPv6:1:2:3:4:5:6:7:8:9]>",	/* too many IPv6 words */
	"<.achowe@snert.com>",			/* Cannot start with dot. */
	"<achowe@.snert.com>",			/* Cannot start with dot. */
	"<achowe.@snert.com>",			/* Cannot end with dot. */
	"<achowe...@snert.com>",		/* No consectutive dots. */
	"<achowe@snert..com>",			/* No consectutive dots. */

	/* Colon reserved for source route address. */
	"<Vanessa-kauskas:@SELSOL.COM>",
};

struct bitword strict_flags[] = {
	{ STRICT_ANGLE_BRACKETS,	"STRICT_ANGLE_BRACKETS" },
	{ STRICT_LOCAL_LENGTH,		"STRICT_LOCAL_LENGTH" },
	{ STRICT_DOMAIN_LENGTH,		"STRICT_DOMAIN_LENGTH" },
	{ STRICT_LITERAL_PLUS,		"STRICT_LITERAL_PLUS" },
	{ STRICT_ADDR_SPEC,		"STRICT_ADDR_SPEC" },
	{ STRICT_MIN_DOTS,		"STRICT_MIN_DOTS" },
	{ 0, 				NULL }
};

void
flags_to_name(struct bitword *map, unsigned long flags)
{
	for ( ; map->name != NULL; map++) {
		if (map->bit & flags) {
			(void) printf("%s", map->name);
			flags &= ~map->bit;
			if (flags)
				fputc(',', stdout);
		}
	}
}

int
testFormat(const char *fmt, ParsePath *p)
{
	char *path;
	long len1, len2;
	static char buffer[1024];

	len1 = formatPathLength(fmt, p);
	len2 = formatPath(buffer, sizeof buffer, fmt, p);
	printf("\t%s\n", buffer);

	if (len1 != len2) {
		printf("formatPathLength=%ld not equal to formatPath=%ld\n", len1, len2);
		return 1;
	}

	if ((path = allocatePath(fmt, p)) == NULL) {
		printf("allocatePath(%s, %lx) failed: %s (%d)\n", fmt, (long) p, strerror(errno), errno);
		return 1;
	}

	if (strcmp(buffer, path) != 0) {
		printf("formatPath='%s' allocatePath='%s'\n", buffer, path);
		free(path);
		return 1;
	}

	free(path);

	return 0;
}

int
test(unsigned long flags, int argc, char **argv)
{
	int i;
	size_t n;
	ParsePath *p;
	const char *error;

	for (i = 1; i < argc; i++) {
		error = parsePath(argv[i], flags, 0, &p);
		if (error != NULL) {
			printf("FAIL '%s' %s\n", argv[i], error);
			continue;
		}

		(void) printf("'%s' flags=0x%lX ", argv[i], flags);
		flags_to_name(strict_flags, flags);
		(void) fputc('\n', stdout);

		printf(
			"\taddress='%s' sourceroute='%s' localleft='%s' localright='%s' domain='%s'\n",
			p->address.string, p->sourceRoute.string, p->localLeft.string,
			p->localRight.string, p->domain.string
		);

		printf("\tis RFC 2606: %s\n", isRFC2606(p->address.string) ? "true" : "false");

		if ((n = strlen(p->address.string)) != (size_t) p->address.length) {
			printf("%lu = strlen(p->address.string) != p->address.length = %ld\n", (unsigned long) n, p->address.length);
			return 1;
		}

		if ((n = strlen(p->sourceRoute.string)) != (size_t) p->sourceRoute.length) {
			printf("%lu = strlen(p->sourceRoute.string) != p->sourceRoute.length = %ld\n", (unsigned long) n, p->sourceRoute.length);
			return 1;
		}

		if ((n = strlen(p->localLeft.string)) != (size_t) p->localLeft.length) {
			printf("%lu = strlen(p->localLeft.string) != p->localLeft.length = %ld\n", (unsigned long) n, p->localLeft.length);
			return 1;
		}

		if ((n = strlen(p->localRight.string)) != (size_t) p->localRight.length) {
			printf("%lu = strlen(p->localRight.string) != p->localRight.length = %ld\n", (unsigned long) n, p->localRight.length);
			return 1;
		}

		if ((n = strlen(p->domain.string)) != (size_t) p->domain.length) {
			printf("%lu = strlen(p->domain.string) != p->domain.length\n = %ld", (unsigned long) n, p->domain.length);
			return 1;
		}

		if (testFormat("<%A>", p))
			return 1;
		if (testFormat("%P@%D", p))
			return 1;
		if (testFormat("%L+bulk@%D", p))
			return 1;
		if (testFormat("bulk+%L@%D", p))
			return 1;
		if (testFormat("bulk%%+%R@%D", p))
			return 1;
		if (testFormat("%P@bulk.%D", p))
			return 1;
		if (testFormat("%S:%L+bulk@%D", p))
			return 1;
		if (testFormat("%T%P@%D", p))
			return 1;

		free(p);
	}

	return 0;
}

struct test_ip {
	unsigned char address[IPV6_BYTE_SIZE];
	long mask;
	int expect;
	char *ip;
	long parsed_length;
};

struct test_ip ip_list[] = {
	/* An invlid IPv6 styled string. */
	{ "\x00\x01\x00\x02\x00\x03\x00\x04\x00\x05\x00\x06\x00\x07\x00\x08", 0,  0, "1:2:3:4:5:6:7:8:9:a:b", sizeof ("1:2:3:4:5:6:7:8")-1 },

	/* A domain/hostname that resembles a base 16 IPv6 address, should not parse. */
	{ "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 0,  0, "feed.beef.ab", 0 },

	/* Something that looks like an IPv4 address. */
	{ "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01\x02\x03\x04", 0,  0, "1.2.3.4.5.6.7.8", sizeof ("1.2.3.4")-1 },

	{ "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", IS_IP_THIS_HOST,   1, "::", sizeof ("::")-1 },
	{ "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", IS_IP_THIS_HOST,   1, "IPV6:::" , sizeof ("IPV6:::")-1 },
	{ "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", IS_IP_THIS_HOST,   1, "0:0:0:0:0:0:0:0" , sizeof ("0:0:0:0:0:0:0:0")-1 },
	{ "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", IS_IP_THIS_HOST,   1, "::0.0.0.0" , sizeof ("::0.0.0.0")-1 },
	{ "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", IS_IP_THIS_HOST,   1, "0:0:0:0:0:0:0.0.0.0" , sizeof ("0:0:0:0:0:0:0.0.0.0")-1 },

	{ "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01\x02\x03", IS_IP_THIS_NET,   1, "0.1.2.3" , sizeof ("0.1.2.3")-1 },

	{ "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01", IS_IP_LOCALHOST,  1, "::1" , sizeof ("::1")-1 },
	{ "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x7F\x00\x00\x01", IS_IP_LOCALHOST,  1, "127.0.0.1" , sizeof ("127.0.0.1")-1 },
	{ "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xFF\xFF\xFF\xFF", IS_IP_BROADCAST,  1, "255.255.255.255" , sizeof ("255.255.255.255")-1 },

	{ "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x7B\x2D\x43\x59",              0,   0, "[123.45.67.89]" , sizeof ("[123.45.67.89]")-1 },
	{ "\x00\x01\x00\x02\x00\x03\x00\x04\x00\x05\x00\x06\x00\x07\x00\x08", IS_IP_V6,   1, "[IPv6:1:2:3:4:5:6:7:8]" , sizeof ("[IPv6:1:2:3:4:5:6:7:8]")-1 },

	{ "\xFE\xED\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xBE\xEF", IS_IP_V6,   1, "[IPv6:feed::beef]" , sizeof ("[IPv6:feed::beef]")-1 },
	{ "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x7B\x2D\x43\x59",              0,   0, "[IPv6:::123.45.67.89]" , sizeof ("[IPv6:::123.45.67.89]")-1 },
	{ "\xBE\xEF\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x7B\x2D\x43\x59", IS_IP_V6,   1, "[IPv6:beef::123.45.67.89]" , sizeof ("[IPv6:beef::123.45.67.89]")-1 },
	{ "\xBE\xEF\x87\x65\x43\x21\x00\x00\x00\x00\x00\x00\x7B\x2D\x43\x59", IS_IP_V6,   1, "[IPv6:beef:8765:4321::123.45.67.89]" , sizeof ("[IPv6:beef:8765:4321::123.45.67.89]")-1 },
	{ "\xFE\xED\xBE\xEF\x00\x00\x00\x00\x00\x00\x12\x34\x56\x78\x9a\xbc", IS_IP_V6,   1, "[IPv6:feed:beef::1234:5678:9abc]" , sizeof ("[IPv6:feed:beef::1234:5678:9abc]")-1 },

	{ "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xC0\x00\x02\x03", IS_IP_TEST_NET,   1, "192.0.2.3" , sizeof ("192.0.2.3")-1 },
	{ "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x7F\x00\x00\x02", IS_IP_LOOPBACK,   1, "127.0.0.2" , sizeof ("127.0.0.2")-1 },
	{ "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xC6\x12\x01\x02", IS_IP_BENCHMARK,  1, "198.18.1.2" , sizeof ("198.18.1.2")-1 },
	{ "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xA9\xfe\x01\x02", IS_IP_LINK_LOCAL, 1, "169.254.1.2" , sizeof ("169.254.1.2")-1 },
	{ "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x0A\x01\x02\x03", IS_IP_PRIVATE_A,  1, "10.1.2.3" , sizeof ("10.1.2.3")-1 },
	{ "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xAC\x10\x01\x02", IS_IP_PRIVATE_B,  1, "172.16.1.2" , sizeof ("172.16.1.2")-1 },
	{ "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xC0\xA8\x01\x02", IS_IP_PRIVATE_C,  1, "192.168.1.2" , sizeof ("192.168.1.2")-1 },
	{ "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xe0\x00\x01\x02", IS_IP_MULTICAST,  1, "224.0.1.2" , sizeof ("224.0.1.2")-1 },
	{ "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xf0\x01\x02\x03", IS_IP_CLASS_E,    1, "240.1.2.3" , sizeof ("240.1.2.3")-1 },

	{ "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xFF\xFF\xC0\x00\x02\x03", IS_IP_TEST_NET,   1, "::ffff:192.0.2.3" , sizeof ("::ffff:192.0.2.3")-1 },
	{ "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xFF\xFF\x7F\x00\x00\x02", IS_IP_LOOPBACK,   1, "::ffff:127.0.0.2" , sizeof ("::ffff:127.0.0.2")-1 },
	{ "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xFF\xFF\xC6\x12\x01\x02", IS_IP_BENCHMARK,  1, "::ffff:198.18.1.2" , sizeof ("::ffff:198.18.1.2")-1 },
	{ "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xFF\xFF\xA9\xfe\x01\x02", IS_IP_LINK_LOCAL, 1, "::ffff:169.254.1.2" , sizeof ("::ffff:169.254.1.2")-1 },
	{ "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xFF\xFF\x0A\x01\x02\x03", IS_IP_PRIVATE_A,  1, "::ffff:10.1.2.3" , sizeof ("::ffff:10.1.2.3")-1 },
	{ "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xFF\xFF\xAC\x10\x01\x02", IS_IP_PRIVATE_B,  1, "::ffff:172.16.1.2" , sizeof ("::ffff:172.16.1.2")-1 },
	{ "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xFF\xFF\xC0\xA8\x01\x02", IS_IP_PRIVATE_C,  1, "::ffff:192.168.1.2" , sizeof ("::ffff:192.168.1.2")-1 },
	{ "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xFF\xFF\xe0\x00\x01\x02", IS_IP_MULTICAST,  1, "::ffff:224.0.1.2" , sizeof ("::ffff:224.0.1.2")-1 },

	{ "\x20\x01\x0D\xB8\x00\x00\x00\x00\x00\x00\x12\x34\x56\x78\x9a\xbc", IS_IP_TEST_NET,   1, "[IPv6:2001:DB8::1234:5678:9abc]" , sizeof ("[IPv6:2001:DB8::1234:5678:9abc]")-1 },
	{ "\xFE\x80\x00\x00\x00\x00\x00\x00\x00\x00\x12\x34\x56\x78\x9a\xbc", IS_IP_LINK_LOCAL, 1, "[IPv6:fe80::1234:5678:9abc]" , sizeof ("[IPv6:fe80::1234:5678:9abc]")-1 },
	{ "\xFF\x01\x00\x00\x00\x00\x00\x00\x00\x00\x12\x34\x56\x78\x9a\xbc", IS_IP_MULTICAST,  1, "[IPv6:ff01::1234:5678:9abc]" , sizeof ("[IPv6:ff01::1234:5678:9abc]")-1 },

	{ "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xD5\x24\xC6\x81", IS_IP_ANY,  0, "[213.36.198.129]" , sizeof ("[213.36.198.129]")-1 },
};

int
testIP()
{
	int i, length, reserved;
	unsigned char ipv6[IPV6_BYTE_SIZE];

	for (i = 0; i < sizeof (ip_list) / sizeof (*ip_list); i++) {
		length = parseIPv6(ip_list[i].ip, ipv6);

		if (length != ip_list[i].parsed_length) {
			printf("unexpected parse length %d for \"%s\"\n", length, ip_list[i].ip);
			continue;
		}

		if (0 < length) {
			if (memcmp(ip_list[i].address, ipv6, sizeof ipv6) != 0) {
				printf("parsed address \"%s\" did not match binary form\n", ip_list[i].ip);
				continue;
			}

			if (ip_list[i].expect != (reserved = isReservedIPv6(ipv6, ip_list[i].mask))) {
				printf("isReservedIPv6(\"%s\", %lx) returned %d, expected %d\n", ip_list[i].ip, ip_list[i].mask, reserved, ip_list[i].expect);
				continue;
			}
		}

		printf("OK \"%s\"\n", ip_list[i].ip);
	}

	return 0;
}

int
main(int argc, char **argv)
{
	long flags;
	int regressTest, vflag, ch;

	if (argc <= 1) {
		printf("usage: parsePath [-t][-v ...][-f flags] [email-path ...]\n");
		return 2;
	}

	vflag = 0;
	regressTest = 0;
	flags = STRICT_LENGTH;

	while ((ch = getopt(argc, argv, "f:tv")) != -1) {
		switch (ch) {
		case 'f':
			flags = setBitWord(strict_flags, optarg);
			break;

		case 't':
			regressTest = 1;
			break;

		case 'v':
			vflag++;
			break;
		}
	}

	if (0 < vflag) {
		LogSetProgramName("parsePath");
		LogOpen("(standard error)");
		parsePathSetDebug(vflag);
	}

	if (regressTest) {
		printf("==== flags STRICT_LENGTH\n");
		if (test(STRICT_LENGTH, sizeof (regression) / sizeof (*regression), regression))
			return 1;

		printf("\n==== flags STRICT_LENGTH|STRICT_SYNTAX\n");
		if (test(STRICT_LENGTH|STRICT_SYNTAX, sizeof (regression) / sizeof (*regression), regression))
			return 1;

		printf("\n==== flags STRICT_LENGTH|STRICT_LITERAL_PLUS\n");
		if (test(STRICT_LENGTH|STRICT_LITERAL_PLUS, sizeof (regression) / sizeof (*regression), regression))
			return 1;

		printf("\n==== IP parsing\n");
		if (testIP())
			return 1;
	}

	if (test(flags, argc - optind + 1, argv + optind - 1))
		return 1;

	return 0;
}
#endif
