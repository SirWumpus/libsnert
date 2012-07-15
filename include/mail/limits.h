/*
 * limits.h
 *
 * Copyright 2005, 2006 by Anthony Howe. All rights reserved.
 */

#ifndef __com_snert_lib_mail_limits_h__
#define __com_snert_lib_mail_limits_h__	1

#ifdef __cplusplus
extern "C" {

#endif

#define SMTP_PORT			25

/*
 * RFC 2821 4.5.3.1 Size limits and minimums.
 */

/*
 * The maximum total length of a user name or other local-part is 64
 * characters.
 *
 * NOTE some stupid mailing list management software use Variable
 * Envelope Return Paths (VERP) or Sender Rewriting Scheme (SRS), in
 * order to encode another address within the local-part of an address.
 * If the address being encoded is already SMTP_PATH_LENGTH octets in
 * length, then the new encoded address will NOT be conformant with
 * respect to the RFC 2821 size limits for the local-part.
 */
#define SMTP_LOCAL_PART_LENGTH		64

/*
 * The maximum total length of a domain name or number is 255
 * characters.
 */
#define SMTP_DOMAIN_LENGTH		255

/*
 * The maximum total length of a reverse-path or forward-path is 256
 * characters (including the punctuation and element separators).
 */
#define SMTP_PATH_LENGTH		256

/*
 * The maximum total length of a command line including the command
 * word and the <CRLF> is 512 characters.  SMTP extensions may be
 * used to increase this limit.
 */
#define SMTP_COMMAND_LINE_LENGTH	512

/*
 * The maximum total length of a reply line including the reply code
 * and the <CRLF> is 512 characters.  More information may be
 * conveyed through multiple-line replies.
 */
#define SMTP_REPLY_LINE_LENGTH		512

/*
 * The maximum total length of a text line including the <CRLF> is
 * 1000 characters (not counting the leading dot duplicated for
 * transparency).  This number may be increased by the use of SMTP
 * Service Extensions.
 */
#define SMTP_TEXT_LINE_LENGTH		1000

/*
 * The maximum total length of a message content (including any
 * message headers as well as the message body) MUST BE at least 64K
 * octets.  Since the introduction of Internet standards for
 * multimedia mail [12], message lengths on the Internet have grown
 * dramatically, and message size restrictions should be avoided if
 * at all possible.  SMTP server systems that must impose
 * restrictions SHOULD implement the "SIZE" service extension [18],
 * and SMTP client systems that will send large messages SHOULD
 * utilize it when possible.
 */
#define SMTP_MINIMUM_MESSAGE_LENGTH	(64*1024)

/*
 * RFC 2821 4.5.3.2 Timeouts in seconds
 */
#define SMTP_CONNECT_TO			30
#define SMTP_WELCOME_TO			300
#define SMTP_COMMAND_TO			300
#define SMTP_DATA_REPLY_TO		120
#define SMTP_DATA_BLOCK_TO		180
#define SMTP_DOT_TO			600
#define SMTP_SERVER_TO			300

#ifndef IPV4_BIT_LENGTH
#define IPV4_BIT_LENGTH			32
#endif

#ifndef IPV4_BYTE_LENGTH
#define IPV4_BYTE_LENGTH		(IPV4_BIT_LENGTH/8)
#endif

#ifndef IPV4_STRING_LENGTH
/* Space for a full-size IPv4 string (4 octets of 3 decimal digits
 * separated by dots and terminating NULL byte).
 */
#define IPV4_STRING_LENGTH		(IPV4_BIT_LENGTH/8*4)
#endif

#define IPV6_TAG			"IPv6:"
#define IPV6_TAG_LENGTH			5

#ifndef IPV6_BIT_LENGTH
#define IPV6_BIT_LENGTH			128
#endif

#ifndef IPV6_BYTE_LENGTH
#define IPV6_BYTE_LENGTH		(IPV6_BIT_LENGTH/8)
#endif

#ifndef IPV6_STRING_LENGTH
/* Space for a full-size IPv6 string; 8 groups of 4 character hex
 * words (16-bits) separated by colons and terminating NULL byte.
 */
#define IPV6_STRING_LENGTH		(IPV6_BIT_LENGTH/16*5)
#endif

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_mail_limits_h__ */
