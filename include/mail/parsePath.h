/*
 * parsePath.h
 *
 * Copyright 2002, 2006 by Anthony Howe. All rights reserved.
 */

#ifndef __com_snert_lib_mail_parsePath_h__
#define __com_snert_lib_mail_parsePath_h__	1

#ifdef __cplusplus
extern "C" {
#endif

#include <com/snert/lib/net/network.h>

typedef struct {
	/*@observer@*/ char *string;
	long length;
} string;

typedef struct {
	int isLocal;		/* True if the address appears to be local.
				 * Not set by parsePath(), but used by the
				 * caller as a convenience variable.
				 */
	int isWhiteListed;	/* True if the address is white listed.
				 * Not set by parsePath(), but used by the
				 * caller as a convenience variable.
				 */
#ifdef STRIP_IP_AS_DOMAIN
	int isDomainAnIP;	/* True for "[1.2.3.4]" style domain. */
#endif
	string address;		/* Original address found between < and >. */
	string sourceRoute;	/* Lower cased source route prefix. */
	string localLeft;	/* Lower cased local part left of + or @. */
	string localRight;	/* Mixed cased local part right of +. */
	string domain;		/* Lower cased domain name after @. */
} ParsePath;

/*@-exportlocal@*/

extern void parsePathSetDebug(int level);

/**
 * @param ip
 *	An IP address in network byte order.
 *
 * @param ip_length
 *	The length of the IP address, which is either IPV4_BYTE_LENGTH (4)
 *	or IPV6_BYTE_LENGTH (16).
 *
 * @param compact
 *	If true and the ip argument is an IPv6 address, then the compact
 *	IPv6 address form will be written into buffer. Otherwise the full
 *	IP address is written to buffer.
 *
 * @param buffer
 *	The buffer for the IP address string. The buffer is always null
 *	terminated.
 *
 * @param size
 *	The size of the buffer, which should be at least IPV6_STRING_LENGTH.
 *
 * @return
 *	The length of the formatted address, excluding the terminating null
 *	byte if the buffer were of infinite size. If the return value is
 *	greater than or equal to the buffer size, then the contents of the
 *	buffer are truncated.
 */
extern long formatIP(unsigned char *ip, int ip_length, int compact, char *buffer, long size);

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
extern int findInnerPath(/*@returned@*/ const char *path, /*@out@*/ const char **start, /*@out@*/ const char **stop);

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
extern long formatPath(/*@unique@*/ char *buffer, long length, const char *fmt, ParsePath *p);

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
extern long formatPathLength(const char *fmt, ParsePath *p);

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
extern /*@null@*/ char *allocatePath(const char *fmt, ParsePath *p);

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
extern /*@observer@*//*@null@*/ const char *parsePath(const char *path, unsigned long flags, int dots, /*@out@*/ ParsePath **out);

/*
 * Flags for parsePath()
 */
#define STRICT_ANGLE_BRACKETS	0x0001
#define STRICT_LOCAL_LENGTH	0x0002
#define STRICT_DOMAIN_LENGTH	0x0004
#define STRICT_LITERAL_PLUS	0x0008
#define STRICT_ADDR_SPEC	0x0010
#define STRICT_MIN_DOTS		0x0020

#define STRICT_SYNTAX		(STRICT_ANGLE_BRACKETS|STRICT_ADDR_SPEC|STRICT_MIN_DOTS)
#define STRICT_LENGTH		(STRICT_LOCAL_LENGTH|STRICT_DOMAIN_LENGTH)

/*@=exportlocal@*/

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_mail_parsePath_h__ */
