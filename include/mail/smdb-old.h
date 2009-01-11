/*
 * smdb.h
 *
 * Copyright 2002, 2006 by Anthony Howe.  All rights reserved.
 */

#ifndef __com_snert_lib_mail_smdb_h__
#define __com_snert_lib_mail_smdb_h__	1

#ifdef __cplusplus
extern "C" {
#endif

/*@-exportlocal@*/

/***********************************************************************
 *** Constants
 ***********************************************************************/

/*@+charintliteral@*/
#define SMDB_ACCESS_NOT_FOUND	'_'	/* No key/value found */
#define SMDB_ACCESS_UNKNOWN	'?'	/* Key found with unknown value */
#define SMDB_ACCESS_OK		'O'	/* OK		O     */
#define SMDB_ACCESS_DISCARD	'D'	/* DISCARD	D     */
#define SMDB_ACCESS_ERROR	'R'	/* ERROR	 R    */
#define SMDB_ACCESS_FRIEND	'F'	/* FRIEND	F     */
#define SMDB_ACCESS_HATER	'H'	/* HATER	H     */
#define SMDB_ACCESS_RELAY	'L'	/* RELAY	  L   */
#define SMDB_ACCESS_REJECT	'J'	/* REJECT	  J   */
#define SMDB_ACCESS_SKIP	'K'	/* SKIP		 K    = DUNNO postfix 2.3 */
#define SMDB_ACCESS_SUBJECT	'U'	/* SUBJECT	 U    */
#define SMDB_ACCESS_VERIFY	'V'	/* VERIFY	V     */
#define SMDB_ACCESS_ENCR	'N'	/* ENCR		 N    */

/***********************************************************************
 *** Global Variables
 ***********************************************************************/

#ifdef S_SPLINT_S
# define __CYGWIN__	1
#endif

#include <sys/types.h>
#include <com/snert/lib/version.h>
#include <com/snert/lib/berkeley_db.h>
#include <com/snert/lib/sys/pthread.h>
#include <com/snert/lib/type/Vector.h>
#include <com/snert/lib/util/option.h>

#ifndef HAVE_DB_H

typedef void DB;
typedef void DBT;

#endif /* HAVE_DB_H */

#define SMDB_DEBUG_ALL			(~0)
#define smdbSetDebugMask		smdbSetDebug

typedef struct {
	DB *db;
	int lockfd;
	char *dbfile;
	time_t mtime;
	int key_has_nul;
	pthread_mutex_t mutex;
} smdb;

extern smdb *smdbOpen(const char *dbfile, int rdonly);
extern char *smdbGetValue(smdb *sm, const char *key);
extern void smdbSetKeyHasNul(smdb *sm, int flag);
extern void smdbSetDebug(int flag);

extern Option smdbOptDebug;
extern Option smdbOptUseStat;
extern Option smdbOptKeyHasNul;
extern Option *smdbOptTable[];

extern void smdbClose(void *sm);

/*
 * Return a generalised result, one of:
 *
 *	SMDB_ACCESS_UNKNOWN
 *	SMDB_ACCESS_OK
 *	SMDB_ACCESS_REJECT
 */
extern int smdbAccessIsOk(int status);

/*
 * @param value
 *	An access database right-hand-side value.
 *
 * @return
 *	An SMDB_ACCESS_* code.
 */
extern int smdbAccessCode(const char *value);

/*
 * Lookup
 *
 *	tag:a.b.c.d
 *	tag:a.b.c
 *	tag:a.b
 *	tag:a
 *
 * or
 *
 *	tag:ipv6:a:b:c:d:e:f:g:h
 *	tag:ipv6:a:b:c:d:e:f:g
 *	tag:ipv6:a:b:c:d:e:f
 *	tag:ipv6:a:b:c:d:e
 *	tag:ipv6:a:b:c:d
 *	tag:ipv6:a:b:c
 *	tag:ipv6:a:b
 *	tag:ipv6:a
 *
 * @param sm
 *	The access database handle.
 *
 * @param tag
 *	The tag to prepend to the search key. May be NULL.
 *
 * @param ip
 *	The IPv4 or IPv6 address string to search on. For an IPv6 address
 * 	it may be prefixed with sendmail's IPv6: tag or not.
 *
 * @param key (out)
 *	A pointer to C string pointer. May be NULL. If this pointer is
 *	not NULL, then pass back the pointer to an allocated C string
 *	corresponding to the key found. Its the  responsibilty of the
 *	caller to release this memory. If SMDB_ACCESS_NOT_FOUND is
 *	return, then NULL is passed back.
 *
 * @param value (out)
 *	A pointer to C string pointer. May be NULL. If this pointer is
 *	not NULL, then pass back the pointer to an allocated C string
 *	corresponding to the value found. Its the  responsibilty of the
 *	caller to release this memory. If SMDB_ACCESS_NOT_FOUND is
 *	return, then NULL is passed back.
 *
 * @return
 *	An SMDB_ACCESS_* code.
 */
extern int smdbAccessIp(smdb *sm, /*@null@*/ const char *tag, /*@unique@*/ const char *ip, /*@out@*/ char **keyp, char /*@out@*/ **valuep);

/*
 * Lookup
 *
 *	tag:[ip]
 *	tag:[ipv6:ip]
 *	tag:some.sub.domain.tld
 *	tag:sub.domain.tld
 *	tag:domain.tld
 *	tag:tld
 *
 * @param sm
 *	The access database handle.
 *
 * @param tag
 *	The tag to prepend to the search key. May be NULL.
 *
 * @param domain
 *	The domain string to search on.
 *
 * @param key (out)
 *	A pointer to C string pointer. May be NULL. If this pointer is
 *	not NULL, then pass back the pointer to an allocated C string
 *	corresponding to the key found. Its the  responsibilty of the
 *	caller to release this memory. If SMDB_ACCESS_NOT_FOUND is
 *	return, then NULL is passed back.
 *
 * @param value (out)
 *	A pointer to C string pointer. May be NULL. If this pointer is
 *	not NULL, then pass back the pointer to an allocated C string
 *	corresponding to the value found. Its the  responsibilty of the
 *	caller to release this memory. If SMDB_ACCESS_NOT_FOUND is
 *	return, then NULL is passed back.
 *
 * @return
 *	An SMDB_ACCESS_* code.
 */
extern int smdbAccessDomain(smdb *sm, /*@null@*/ const char *tag, /*@unique@*/ const char *domain, /*@out@*/ char **keyp, char /*@out@*/ **valuep);

/*
 * Lookup
 *
 *	tag:account@some.sub.domain.tld
 *	tag:some.sub.domain.tld
 *	tag:sub.domain.tld
 *	tag:domain.tld
 *	tag:tld
 *	tag:account@
 *
 * @param sm
 *	The access database handle.
 *
 * @param tag
 *	The tag to prepend to the search key. May be NULL.
 *
 * @param email
 *	The email address string to search on.
 *
 * @param key (out)
 *	A pointer to C string pointer. May be NULL. If this pointer is
 *	not NULL, then pass back the pointer to an allocated C string
 *	corresponding to the key found. Its the  responsibilty of the
 *	caller to release this memory. If SMDB_ACCESS_NOT_FOUND is
 *	return, then NULL is passed back.
 *
 * @param value (out)
 *	A pointer to C string pointer. May be NULL. If this pointer is
 *	not NULL, then pass back the pointer to an allocated C string
 *	corresponding to the value found. Its the  responsibilty of the
 *	caller to release this memory. If SMDB_ACCESS_NOT_FOUND is
 *	return, then NULL is passed back.
 *
 * @return
 *	An SMDB_ACCESS_* code.
 */
extern int smdbAccessMail(smdb *sm, /*@null@*/ const char *tag, /*@unique@*/ const char *mail, /*@out@*/ char **keyp, char /*@out@*/ **valuep);

/*
 * Lookup
 *
 *	tag:a.b.c.d
 *	tag:a.b.c
 *	tag:a.b
 *	tag:a
 *
 *	a.b.c.d
 *	a.b.c
 *	a.b
 *	a
 *
 * or
 *
 *	tag:ipv6:a:b:c:d:e:f:g:h
 *	tag:ipv6:a:b:c:d:e:f:g
 *	tag:ipv6:a:b:c:d:e:f
 *	tag:ipv6:a:b:c:d:e
 *	tag:ipv6:a:b:c:d
 *	tag:ipv6:a:b:c
 *	tag:ipv6:a:b
 *	tag:ipv6:a
 *
 *	ipv6:a:b:c:d:e:f:g:h
 *	ipv6:a:b:c:d:e:f:g
 *	ipv6:a:b:c:d:e:f
 *	ipv6:a:b:c:d:e
 *	ipv6:a:b:c:d
 *	ipv6:a:b:c
 *	ipv6:a:b
 *	ipv6:a
 *
 * @param sm
 *	The access database handle.
 *
 * @param tag
 *	The tag to prepend to the search key. May be NULL.
 *
 * @param ip
 *	The IPv4 or IPv6 address string to search on.
 *
 * @param key (out)
 *	A pointer to C string pointer. May be NULL. If this pointer is
 *	not NULL, then pass back the pointer to an allocated C string
 *	corresponding to the key found. Its the  responsibilty of the
 *	caller to release this memory. If SMDB_ACCESS_NOT_FOUND is
 *	return, then NULL is passed back.
 *
 * @param value (out)
 *	A pointer to C string pointer. May be NULL. If this pointer is
 *	not NULL, then pass back the pointer to an allocated C string
 *	corresponding to the value found. Its the  responsibilty of the
 *	caller to release this memory. If SMDB_ACCESS_NOT_FOUND is
 *	return, then NULL is passed back.
 *
 * @return
 *	An SMDB_ACCESS_* code.
 */
extern int smdbAccessIp2(smdb *sm, /*@null@*/ const char *tag, /*@unique@*/ const char *ip, /*@out@*/ char **keyp, char /*@out@*/ **valuep);

/*
 * Lookup
 *
 *	tag:[ip]
 *	tag:[ipv6:ip]
 *	tag:some.sub.domain.tld
 *	tag:sub.domain.tld
 *	tag:domain.tld
 *	tag:tld
 *
 *	[ip]
 *	[ipv6:ip]
 *	some.sub.domain.tld
 *	sub.domain.tld
 *	domain.tld
 *	tld
 *
 * @param sm
 *	The access database handle.
 *
 * @param tag
 *	The tag to prepend to the search key. May be NULL.
 *
 * @param domain
 *	The domain string to search on.
 *
 * @param key (out)
 *	A pointer to C string pointer. May be NULL. If this pointer is
 *	not NULL, then pass back the pointer to an allocated C string
 *	corresponding to the key found. Its the  responsibilty of the
 *	caller to release this memory. If SMDB_ACCESS_NOT_FOUND is
 *	return, then NULL is passed back.
 *
 * @param value (out)
 *	A pointer to C string pointer. May be NULL. If this pointer is
 *	not NULL, then pass back the pointer to an allocated C string
 *	corresponding to the value found. Its the  responsibilty of the
 *	caller to release this memory. If SMDB_ACCESS_NOT_FOUND is
 *	return, then NULL is passed back.
 *
 * @return
 *	An SMDB_ACCESS_* code.
 */
extern int smdbAccessDomain2(smdb *sm, /*@null@*/ const char *tag, /*@unique@*/ const char *domain, /*@out@*/ char **keyp, char /*@out@*/ **valuep);

/*
 * Lookup
 *
 *	tag:account@some.sub.domain.tld
 *	tag:some.sub.domain.tld
 *	tag:sub.domain.tld
 *	tag:domain.tld
 *	tag:tld
 *	tag:account@
 *
 *	account@some.sub.domain.tld
 *	some.sub.domain.tld
 *	sub.domain.tld
 *	domain.tld
 *	tld
 *	account@
 *
 * @param sm
 *	The access database handle.
 *
 * @param tag
 *	The tag to prepend to the search key. May be NULL.
 *
 * @param email
 *	The email address string to search on.
 *
 * @param key (out)
 *	A pointer to C string pointer. May be NULL. If this pointer is
 *	not NULL, then pass back the pointer to an allocated C string
 *	corresponding to the key found. Its the  responsibilty of the
 *	caller to release this memory. If SMDB_ACCESS_NOT_FOUND is
 *	return, then NULL is passed back.
 *
 * @param value (out)
 *	A pointer to C string pointer. May be NULL. If this pointer is
 *	not NULL, then pass back the pointer to an allocated C string
 *	corresponding to the value found. Its the  responsibilty of the
 *	caller to release this memory. If SMDB_ACCESS_NOT_FOUND is
 *	return, then NULL is passed back.
 *
 * @return
 *	An SMDB_ACCESS_* code.
 */
extern int smdbAccessMail2(smdb *sm, /*@null@*/ const char *tag, /*@unique@*/ const char *email, /*@out@*/ char **keyp, char /*@out@*/ **valuep);

/*
 * Parse the sendmail.cf and initialise global variables.
 *
 *	smdbAccess		(can be NULL)
 *	smdbAliases		(can be NULL)
 * 	smMasqueradeAs
 *	smClientPortInet4
 *	smClientPortInet6
 *
 * @param cf
 *	The sendmail.cf file path.
 *
 * @param flags
 *	A bit mask of flags used to control which sendmail databases
 *	should be opened.
 *
 * @return
 *	Zero (0) for success, otherwise -1 on error.
 */
extern int readSendmailCf(char *cf, long flags);

#define SMDB_OPEN_ALL		(~0)
#define SMDB_OPEN_ACCESS	1
#define SMDB_OPEN_ALIASES	2
#define SMDB_OPEN_VIRTUSER	4

/*
 * There can be multiple access.db and aliases.db files in Sendmail 8.
 */
extern /*@null@*/ smdb *smdbVuser;
extern /*@null@*/ smdb *smdbAccess;
extern /*@null@*/ Vector smdbAliases;

extern char *smMasqueradeAs;

typedef struct {
	long port;
	int family;
	/*@null@*/ char *address;
	int useForHelo;
	int dontUseAuth;
	int dontUseStartTls;
} SmClientPortOptions;

extern SmClientPortOptions smClientPortInet4;
extern SmClientPortOptions smClientPortInet6;

/*@=exportlocal@*/

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_mail_smdb_h__ */

