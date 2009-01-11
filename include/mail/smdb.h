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
#define SMDB_ACCESS_OK		'O'	/* OK		O.    		*/
#define SMDB_ACCESS_DISCARD	'D'	/* DISCARD	D...... 	*/
#define SMDB_ACCESS_ERROR	'R'	/* ERROR	.R...  		*/
#define SMDB_ACCESS_FRIEND	'F'	/* FRIEND	F..... 		*/
#define SMDB_ACCESS_HATER	'H'	/* HATER	H....  		*/
#define SMDB_ACCESS_RELAY	'L'	/* RELAY	..L..  		*/
#define SMDB_ACCESS_REJECT	'J'	/* REJECT	..J... 		*/
#define SMDB_ACCESS_SKIP	'K'	/* SKIP		.K..    	= DUNNO postfix 2.3 */
#define SMDB_ACCESS_SUBJECT	'U'	/* SUBJECT	.U.....		*/
#define SMDB_ACCESS_VERIFY	'V'	/* VERIFY	V..... 		*/
#define SMDB_ACCESS_ENCR	'N'	/* ENCR		.N..    	*/
#define SMDB_ACCESS_TEMPFAIL	'P'	/* TEMPFAIL	...P....	*/

#define SMDB_COMBO_TAG_DELIM	":"

/***********************************************************************
 *** Global Variables
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <sys/types.h>

#include <com/snert/lib/type/kvm.h>
#include <com/snert/lib/util/option.h>

typedef struct kvm smdb;

extern smdb *smdbAccess;
extern smdb *smdbVuser;

extern smdb *smdbOpen(const char *dbfile, int rdonly);
extern char *smdbGetValue(smdb *sm, const char *key);
extern void smdbSetKeyHasNul(smdb *sm, int flag);

/* To be removed... */
#define SMDB_DEBUG_ALL			1
#define smdbSetDebugMask		smdbSetDebug

/* ...and replace by */
extern void smdbSetDebug(int flag);

extern Option smdbOptDebug;
extern Option smdbOptUseStat;
extern Option smdbOptKeyHasNul;
extern Option smdbOptRelayOk;
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
extern int smdbAccessIp(smdb *sm, const char *tag, const char *ip, char **keyp, char **valuep);

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
extern int smdbAccessDomain(smdb *sm, const char *tag, const char *domain, char **keyp, char **valuep);

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
extern int smdbAccessMail(smdb *sm, const char *tag, const char *mail, char **keyp, char **valuep);

/*
 * @param sm
 *	The access database handle.
 *
 * @param tag1
 *	The tag to prepend to the search key.
 *
 * @param ip
 *	The IPv4 or IPv6 address string to search on.
 *
 * @param domain
 *	The domain string to search on.
 *
 * @param tag2
 *	The tag to prepend to the search key.
 *
 * @param mel
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
extern int smdbIpMail(smdb *sm, const char *tag1, const char *key1, const char *tag2, const char *key2, char **keyp, char **valuep);

/*
 * @param sm
 *	The access database handle.
 *
 * @param tag1
 *	The tag to prepend to the search key.
 *
 * @param domain
 *	The domain string to search on.
 *
 * @param tag2
 *	The tag to prepend to the search key.
 *
 * @param mel
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
extern int smdbDomainMail(smdb *sm, const char *tag1, const char *key1, const char *tag2, const char *key2, char **keyp, char **valuep);

/*
 * @param sm
 *	The access database handle.
 *
 * @param tag1
 *	The tag to prepend to the search key.
 *
 * @param mail
 *	The email address string to search on.
 *
 * @param tag2
 *	The tag to prepend to the search key.
 *
 * @param mel
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
extern int smdbMailMail(smdb *sm, const char *tag1, const char *key1, const char *tag2, const char *key2, char **keyp, char **valuep);

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_mail_smdb_h__ */

