/*
 * smf.h
 *
 * Copyright 2002, 2006 by Anthony Howe.  All rights reserved.
 */

#ifndef __com_snert_lib_mail_smf_h__
#define __com_snert_lib_mail_smf_h__	1

#ifdef __cplusplus
extern "C" {
#endif

#if defined(HAVE_LIBMILTER_MFAPI_H) && defined(HAVE_PTHREAD_CREATE)

#include <stdarg.h>
#include <sys/types.h>

#ifdef SET_SMFI_VERSION
/* Have to define this before loading libmilter
 * headers to enable the extra handlers.
 */
# define SMFI_VERSION		SET_SMFI_VERSION
#endif

#include <libmilter/mfapi.h>

#include <com/snert/lib/sys/pthread.h>
#include <com/snert/lib/util/option.h>
#include <com/snert/lib/mail/limits.h>
#include <com/snert/lib/mail/parsePath.h>
#include <com/snert/lib/sys/pid.h>

/*@-exportlocal@*/

/***********************************************************************
 *** Constants
 ***********************************************************************/

/*
 * These are things I like to log in assorted milters and felt should be
 * standardised across all my milters.
 */
#define SMF_LOG_ALL			(~0)
#define SMF_LOG_ERROR			0	/* errors are always logged */
#define SMF_LOG_WARN			1	/* warnings should be logged */
#define SMF_LOG_INFO			2	/* default, general */
#define SMF_LOG_TRACE			4	/* funtions in/out */
#define SMF_LOG_PARSE			8	/* slice & dice */
#define SMF_LOG_DEBUG			16	/* funtions debug */
#define SMF_LOG_DIALOG			32	/* communications */
#define SMF_LOG_STATE			64	/* state transitions */
#define SMF_LOG_DNS			128	/* debug DNS code */
#define SMF_LOG_CACHE			256	/* cache get/put/gc */
#define SMF_LOG_DATABASE		512	/* SMDB lookups */
#define SMF_LOG_SOCKET_FD		1024	/* socket open/close */
#define SMF_LOG_SOCKET_ALL		2048	/* socket functions & I/O */
#define SMF_LOG_LIBMILTER		4096	/* libmilter engine */

/* DEPRICATED; use smfOpt* */
#define SMF_FLAG_ALL			(~0)
#define SMF_FLAG_STRICT_SYNTAX		0x00000001	/* see parsePath.h */
#define SMF_FLAG_STRICT_LOCAL_LENGTH	0x00000002	/* see parsePath.h */
#define SMF_FLAG_STRICT_DOMAIN_LENGTH	0x00000004	/* see parsePath.h */
#define SMF_FLAG_STRICT_LITERAL_PLUS	0x00000008	/* see parsePath.h */
#define SMF_FLAG_REJECT_PERCENT_RELAY	0x00000010	/* see smfAccessRcpt() */
#define SMF_FLAG_REJECT_RFC2606		0x00000020	/* see smf.c */
#define SMF_FLAG_REJECT_UNKNOWN_TLD	0x00000040	/* see smf.c */
#define SMF_FLAG_SMTP_AUTH_OK		0x00000080	/* see smf.c */

#ifndef SMF_SOCKET_TIMEOUT
#define SMF_SOCKET_TIMEOUT		1800
#endif

#ifndef LIBMILTER_SOCKET_TIMEOUT
/* See smfi_settimeout() documentation. No constant defined. */
#define LIBMILTER_SOCKET_TIMEOUT	7210
#endif

#ifndef MILTER_CHUNK_SIZE
#define MILTER_CHUNK_SIZE		65535
#endif

#define X_SMFIS_UNKNOWN			(-1)

#define PRECEDENCE_SPECIAL_DELIVERY	4
#define PRECEDENCE_FIRST_CLASS		3
#define PRECEDENCE_LIST			2
#define PRECEDENCE_JUNK			1
#define PRECEDENCE_BULK			0

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
#define LOCAL_PART_LENGTH		(2*SMTP_LOCAL_PART_LENGTH)

#define EMAIL_LENGTH			(LOCAL_PART_LENGTH+1+SMTP_DOMAIN_LENGTH)

#define SMF_STDIO_CLOSE			0
#define SMF_STDIO_AS_IS			1
#define SMF_STDIO_IGNORE		2

/***********************************************************************
 *** Global Variables
 ***********************************************************************/

typedef struct {
	/* VersionInfo members */
	int major;
	int minor;
	int build;
	char *package;				/* name used for file names */
	char *author;
	char *copyright;
	char *user;				/* set process owner */
	char *group;				/* set process group */
	char *cf;				/* /etc/mail/program.cf */
	char *pid;				/* /var/run/program.pid */
	char *socket;				/* /var/run/program.socket */
	char *workdir;				/* /var/tmp */
	int standardIO;				/* SMF_STDIO_ constant */
	struct smfiDesc handlers;		/* libmilter description */
} smfInfo;

typedef struct {
	SMFICTX *ctx;				/* per connection */
	smfInfo *info;				/* per process, static */
	unsigned short cid;			/* per connection id */
	int skipConnection;			/* per connection */
	int skipMessage;			/* per message */
	int skipRecipient;			/* per recipient */
	const char *qid;			/* per message, $i macro */
	ParsePath *mail;			/* per message, must free */
	ParsePath *rcpt;			/* per recipient, must free */
	char replyLine[SMTP_REPLY_LINE_LENGTH+1];	/* smfReply(), smfReplyV() */
	char client_name[SMTP_DOMAIN_LENGTH+1];	/* per connection */
	char client_addr[IPV6_TAG_LENGTH+IPV6_STRING_LENGTH];	/* per connection */
} smfWork;

/* DEPRICATED; use smfOpt* */
extern long smfFlags;

extern long smfLogDetail;
extern pthread_mutex_t smfMutex;

extern const char smfNo[];
extern const char smfYes[];
extern const char smfNoQueue[];
extern const char smfUndefined[];
extern const char * const smfPrecedence[];

extern /* const */ char smMacro_auth_authen[];
extern /* const */ char smMacro_auth_author[];
extern /* const */ char smMacro_auth_ssf[];
extern /* const */ char smMacro_auth_type[];
extern /* const */ char smMacro_client_addr[];
extern /* const */ char smMacro_client_name[];
extern /* const */ char smMacro_client_resolv[];
extern /* const */ char smMacro_if_addr[];
extern /* const */ char smMacro_if_name[];
extern /* const */ char smMacro_verify[];

extern Option smfOptAccessDb;
extern Option smfOptDaemon;
extern Option smfOptFile;
extern Option smfOptHelp;
extern Option smfOptMilterQueue;
extern Option smfOptMilterSocket;
extern Option smfOptMilterTimeout;
extern Option smfOptInterfaceIp;
extern Option smfOptInterfaceName;
extern Option smfOptPidFile;
extern Option smfOptQuit;
extern Option smfOptRFC2821DomainLength;
extern Option smfOptRFC2821LiteralPlus;
extern Option smfOptRFC2821LocalLength;
extern Option smfOptRFC2821Syntax;
extern Option smfOptRejectPercentRelay;
extern Option smfOptRejectRFC2606;
extern Option smfOptRejectUnknownTLD;
extern Option smfOptRestart;
extern Option smfOptRunGroup;
extern Option smfOptRunUser;
extern Option smfOptSendmailCf;		/* DEPRICATED; use smfOptAccessDb */
extern Option smfOptSmtpAuthOk;
extern Option smfOptVerbose;
extern Option smfOptWorkDir;

extern Option *smfOptTable[];

/***********************************************************************
 *** Functions
 ***********************************************************************/

/* DEPRICATED; use optionArray() with smfOptionTable. */
extern void smfSetFlags(const char *flags);

extern void smfSetLogDetail(const char *detail);
extern void smfLog(int category, const char *fmt, ...);


extern sfsistat smfNullWorkspaceError(const char *where);
extern sfsistat smfReply(smfWork *work, int code, const char *ecode, const char *fmt, ...);
extern sfsistat smfReplyV(smfWork *work, int code, const char *ecode, const char *fmt, va_list args);

#ifdef HAVE_SMFI_SETMLREPLY
extern sfsistat smfMultiLineReply(smfWork *work, int code, const char *ecode, ...);
extern sfsistat smfMultiLineReplyV(smfWork *work, int code, const char *ecode, va_list args);
extern sfsistat smfMultiLineReplyA(smfWork *work, int code, const char *ecode, char **lines);
#endif

extern void smfProlog(smfWork *work, SMFICTX *ctx, char *client_name, _SOCK_ADDR *raw_client_addr);
extern unsigned short smfOpenProlog(SMFICTX *ctx, char *client_name, _SOCK_ADDR *raw_client_addr, char *client_addr, long length);
extern unsigned short smfCloseEpilog(smfWork *work);

/**
 * @param hay
 *	A C string to search.
 *
 * @param pins
 *	A C string containing an optional list of whitespace separated
 *	pattern/action pairs followed by an optional default action.
 *
 *	( !pattern!action | /regex/action  | [network/cidr]action )* default-action?
 *
 *	The !pattern! uses the simple TextMatch() function with * and ?
 *	wild cards. The /regex/ uses Exteneded Regular Expressions (or
 *	Perl Compatible Regular Expressions if selected at compile time).
 *
 * @param action
 *	A pointer to a C string pointer, which can be NULL. Used to
 *	passback an allocated copy of the action string or NULL. Its
 *	the caller's responsiblity to free() this string.
 *
 * @return
 *	 A SMDB_ACCESS_* code.
 */
extern int smfAccessPattern(smfWork *work, const char *hay, char *pins, char **action);

/**
 * Perform the following access.db lookups concerning IP and/or resolved
 * domain name, stopping on the first entry found:
 *
 * For an IPv4 address:
 *
 *	tag:a.b.c.d
 *	tag:a.b.c
 *	tag:a.b
 *	tag:a
 *
 * For an IPv6 address:
 *
 *	tag:a:b:c:d:e:f:g
 *	tag:a:b:c:d:e:f
 *	tag:a:b:c:d:e
 *	tag:a:b:c:d
 *	tag:a:b:c
 *	tag:a:b
 *	tag:a
 *
 * If the above IP address lookups fail to find an entry and the IP address
 * resolved, then the subsequent lookups are:
 *
 *	tag:some.sub.domain.tld
 *	tag:sub.domain.tld
 *	tag:domain.tld
 *	tag:tld
 *	tag:
 *
 * If the above IP address lookups fail to find an entry and the IP address
 * did NOT resolve, then the subsequent lookups are:
 *
 *	tag:[ip]
 *	tag:
 *
 * When an entry is found, then the right-hand-side value is processed
 * as a pattern list and that result returned. Otherwise if no entry is
 * found, then SMDB_ACCESS_NOT_FOUND will be returned.
 *
 * Note this lookup ordering, except the empty tag:, is based on sendmail's
 * lookups. Sendmail syntax limits the netmasks to /32, /24, /16, /8 for IPv4
 * and /128, /112. /96, ... /16 for IPv6, which are the most common cases,
 * but not so flexible as full range netmasks. The smfAccessPattern() pattern
 * list processing provides "[network/cidr]action" for finer granularity.
 *
 * @param work
 *	A pointer to a smfWork workspace.
 *
 * @param tag
 *	A C string tag that may be prefixed to access.db look-ups.
 *
 * @param client_name
 *	A C string for the SMTP client host name.
 *
 * @param client_addr
 *	A C string for the SMTP client address.
 *
 * @param lhs
 *	A pointer to C string pointer. May be NULL. If this pointer is
 *	not NULL, then pass back the pointer to an allocated C string
 *	corresponding to the key found. Its the  responsibilty of the
 *	caller to release this memory.
 *
 * @param rhs
 *	A pointer to C string pointer. May be NULL. If this pointer is
 *	not NULL, then pass back the pointer to an allocated C string
 *	corresponding to the value found. Its the  responsibilty of the
 *	caller to release this memory.
 *
 * @return
 *	One of SMDB_ACCESS_OK, SMDB_ACCESS_REJECT, or SMDB_ACCESS_UNKNOWN.
 *
 * @see
 *	smfAccessPattern()
 */
extern int smfAccessClient(smfWork *work, const char *tag, const char *client_name, const char *client_addr, char **lhs, char **rhs);

/**
 * Perform the following access.db lookups concerning IP and/or resolved
 * domain name, stopping on the first entry found:
 *
 * For an IPv4 address:
 *
 *	tag:a.b.c.d
 *	tag:a.b.c
 *	tag:a.b
 *	tag:a
 *
 *	connect:a.b.c.d
 *	connect:a.b.c
 *	connect:a.b
 *	connect:a
 *
 *	a.b.c.d
 *	a.b.c
 *	a.b
 *	a
 *
 * For an IPv6 address:
 *
 *	tag:a:b:c:d:e:f:g
 *	tag:a:b:c:d:e:f
 *	tag:a:b:c:d:e
 *	tag:a:b:c:d
 *	tag:a:b:c
 *	tag:a:b
 *	tag:a
 *
 *	connect:a:b:c:d:e:f:g
 *	connect:a:b:c:d:e:f
 *	connect:a:b:c:d:e
 *	connect:a:b:c:d
 *	connect:a:b:c
 *	connect:a:b
 *	connect:a
 *
 *	a:b:c:d:e:f:g
 *	a:b:c:d:e:f
 *	a:b:c:d:e
 *	a:b:c:d
 *	a:b:c
 *	a:b
 *	a
 *
 * If the above IP address lookups fail to find an entry and the IP address
 * resolved, then the subsequent lookups are:
 *
 *	tag:some.sub.domain.tld
 *	tag:sub.domain.tld
 *	tag:domain.tld
 *	tag:tld
 *	tag:
 *
 *	connect:some.sub.domain.tld
 *	connect:sub.domain.tld
 *	connect:domain.tld
 *	connect:tld
 *
 *	some.sub.domain.tld
 *	sub.domain.tld
 *	domain.tld
 *	tld
 *
 * If the above IP address lookups fail to find an entry and the IP address
 * did NOT resolve, then the subsequent lookups are:
 *
 *	tag:[ip]
 *	tag:
 *
 *	connect:[ip]
 *
 *	[ip]
 *
 * When a tag: entry is found, then the right-hand-side value is processed
 * as a pattern list and that result returned, else on the result of the
 * the right-hand-side is returned. Otherwise if no entry is found, then
 * SMDB_ACCESS_NOT_FOUND will be returned.
 *
 * Note this lookup ordering, except the empty tag:, is based on sendmail's
 * lookups. Sendmail syntax limits the netmasks to /32, /24, /16, /8 for IPv4
 * and /128, /112. /96, ... /16 for IPv6, which are the most common cases,
 * but not so flexible as full range netmasks. The smfAccessPattern() pattern
 * list processing provides "[network/cidr]action" for finer granularity.
 *
 * @param work
 *	A pointer to a smfWork workspace.
 *
 * @param tag
 *	A C string tag that may be prefixed to access.db look-ups.
 *
 * @param client_name
 *	A C string for the SMTP client host name.
 *
 * @param client_addr
 *	A C string for the SMTP client address.
 *
 * @param loopbackDefault
 *	An SMDB_ACCESS_* value returned for the localhost [127.0.0.1]
 *	loopback address.
 *
 * @return
 *	One of SMDB_ACCESS_OK, SMDB_ACCESS_REJECT, or SMDB_ACCESS_UNKNOWN.
 */
extern int smfAccessHost(smfWork *work, const char *tag, const char *client_name, const char *client_addr, int loopDefault);

/**
 * Perform the following access.db lookups for an auth-id, stopping on
 * the first entry found:
 *
 *	tag:auth_authen				RHS
 * 	tag:					RHS
 *
 * When an entry is found, then the right-hand-side value is processed
 * as a pattern list and that result returned. The string to search will
 * be "auth:mail".
 *
 * Otherwise if no entry is found, then SMDB_ACCESS_NOT_FOUND will be
 * returned.
 *
 * @param work
 *	A pointer to a smfWork workspace.
 *
 * @param tag
 *	A C string tag that may be prefixed to access.db look-ups.
 *
 * @param auth
 *	A C string for the {auth_authen} macro or NULL.
 *
 * @param mail
 *	A C string for the SMTP MAIL FROM: address.
 *
 * @param lhs
 *	A pointer to C string pointer. May be NULL. If this pointer is
 *	not NULL, then pass back the pointer to an allocated C string
 *	corresponding to the key found. Its the  responsibilty of the
 *	caller to release this memory.
 *
 * @param rhs
 *	A pointer to C string pointer. May be NULL. If this pointer is
 *	not NULL, then pass back the pointer to an allocated C string
 *	corresponding to the value found. Its the  responsibilty of the
 *	caller to release this memory.
 *
 * @return
 *	One of SMDB_ACCESS_OK, SMDB_ACCESS_REJECT, SMDB_ACCESS_UNKNOWN,
 *	SMDB_ACCESS_NOT_FOUND, or SMDB_ACCESS_ERROR.
 *
 * @see
 *	smfAccessPattern()
 */
extern int smfAccessAuth(smfWork *work, const char *tag, const char *auth, const char *mail, char **lhs, char **rhs);

/**
 * Perform the following access.db lookups for a mail address, stopping on
 * the first entry found:
 *
 *	tag:account@some.sub.domain.tld
 *	tag:some.sub.domain.tld
 *	tag:sub.domain.tld
 *	tag:domain.tld
 *	tag:tld
 *	tag:account@
 * 	tag:
 *
 * When an entry is found, then the right-hand-side value is processed
 * as a pattern list and that result returned. If auth is not NULL, then
 * the string to search will be "auth:mail", else just "mail".
 *
 * Otherwise if no entry is found, then SMDB_ACCESS_NOT_FOUND will be
 * returned.
 *
 * @param work
 *	A pointer to a smfWork workspace.
 *
 * @param tag
 *	A C string tag that may be prefixed to access.db look-ups.
 *
 * @param mail
 *	A C string for the SMTP MAIL FROM: address.
 *
 * @param lhs
 *	A pointer to C string pointer. May be NULL. If this pointer is
 *	not NULL, then pass back the pointer to an allocated C string
 *	corresponding to the key found. Its the  responsibilty of the
 *	caller to release this memory.
 *
 * @param rhs
 *	A pointer to C string pointer. May be NULL. If this pointer is
 *	not NULL, then pass back the pointer to an allocated C string
 *	corresponding to the value found. Its the  responsibilty of the
 *	caller to release this memory.
 *
 * @return
 *	One of SMDB_ACCESS_OK, SMDB_ACCESS_REJECT, SMDB_ACCESS_UNKNOWN,
 *	SMDB_ACCESS_NOT_FOUND, or SMDB_ACCESS_ERROR.
 *
 * @see
 *	smfAccessPattern()
 */
extern int smfAccessEmail(smfWork *work, const char *tag, const char *mail, char **lhs, char **rhs);

/**
 * Perform the following access.db lookups for mail address, stopping on
 * the first entry found:
 *
 *	tag:account@some.sub.domain.tld
 *	tag:some.sub.domain.tld
 *	tag:sub.domain.tld
 *	tag:domain.tld
 *	tag:tld
 *	tag:account@
 * 	tag:
 *
 *	from:account@some.sub.domain.tld
 *	from:some.sub.domain.tld
 *	from:sub.domain.tld
 *	from:domain.tld
 *	from:tld
 *	from:account@
 *
 *	account@some.sub.domain.tld
 *	some.sub.domain.tld
 *	sub.domain.tld
 *	domain.tld
 *	tld
 *	account@
 *
 * When a tag: entry is found, then the right-hand-side value is processed
 * as a pattern list and that result returned, else on the result of the
 * the right-hand-side is returned. Otherwise if no entry is found, then
 * SMDB_ACCESS_NOT_FOUND will be returned.
 *
 * @param work
 *	A pointer to a smfWork workspace.
 *
 * @param tag
 *	A C string tag that may be prefixed to access.db look-ups.
 *
 * @param mail
 *	A C string for the SMTP MAIL FROM: address.
 *
 * @param dsnDefault
 *	A SMDB_ACCESS_* value to be return for the DSN (null sender).
 *
 * @return
 *	One of SMDB_ACCESS_OK, SMDB_ACCESS_REJECT, SMDB_ACCESS_UNKNOWN,
 *	SMDB_ACCESS_NOT_FOUND, or SMDB_ACCESS_ERROR for a parse error in
 *	which case the SMTP reponse will also have been set.
 */
extern int smfAccessMail(smfWork *work, const char *tag, const char *mail, int dsnDefault);

/**
 * Perform the following access.db lookups for mail address, stopping on
 * the first entry found:
 *
 *	tag:account@some.sub.domain.tld
 *	tag:some.sub.domain.tld
 *	tag:sub.domain.tld
 *	tag:domain.tld
 *	tag:tld
 *	tag:account@
 * 	tag:
 *
 *	spam:account@some.sub.domain.tld	FRIEND
 *	spam:some.sub.domain.tld		FRIEND
 *	spam:sub.domain.tld			FRIEND
 *	spam:domain.tld				FRIEND
 *	spam:tld				FRIEND
 *	spam:account@				FRIEND
 *
 *	from:account@some.sub.domain.tld
 *	from:some.sub.domain.tld
 *	from:sub.domain.tld
 *	from:domain.tld
 *	from:tld
 *	from:account@
 *
 *	account@some.sub.domain.tld
 *	some.sub.domain.tld
 *	sub.domain.tld
 *	domain.tld
 *	tld
 *	account@
 *
 * When a tag: entry is found, then the right-hand-side value is processed
 * as a pattern list and that result returned, else on the result of the
 * the right-hand-side is returned. Otherwise if no entry is found, then
 * SMDB_ACCESS_NOT_FOUND will be returned.
 *
 * @param work
 *	A pointer to a smfWork workspace.
 *
 * @param tag
 *	A C string tag that may be prefixed to access.db look-ups.
 *
 * @param rcpt
 *	A C string for the SMTP RCPT TO: address.
 *
 * @return
 *	One of SMDB_ACCESS_OK, SMDB_ACCESS_REJECT, SMDB_ACCESS_UNKNOWN,
 *	SMDB_ACCESS_NOT_FOUND, or SMDB_ACCESS_ERROR for a parse error in
 *	which case the SMTP reponse will also have been set.
 */
extern int smfAccessRcpt(smfWork *work, const char *tag, const char *rcpt);

extern int smfHeaderSet(SMFICTX *ctx, char *field, char *value, int index, int present);
extern int smfHeaderRemove(SMFICTX *ctx, char *field);

extern void smfAtExitCleanUp(void);
extern void smfSignalExit(int signum);
extern void smfOptions(smfInfo *smf, int argc, char **argv, void (*options)(int, char **));
extern int smfKillProcess(smfInfo *smf, int signal);
extern int smfMainStart(smfInfo *smf);
extern int smfSetFileOwner(smfInfo *smf, const char *file);
extern int smfSetProcessOwner(smfInfo *smf);
extern int smfStartBackgroundProcess(void);

/*@=exportlocal@*/

#ifdef  __cplusplus
}
#endif

#endif /* defined(HAVE_LIBMILTER_MFAPI_H) && defined(HAVE_PTHREAD_CREATE) */

#endif /* __com_snert_lib_mail_smf_h__ */

