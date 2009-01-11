/*
 * smdb.c
 *
 * Sendmail Database Support
 *
 * Copyright 2002, 2006 by Anthony Howe. All rights reserved.
 */

#ifndef SMCF_LINE_SIZE
#define SMCF_LINE_SIZE	1024
#endif

#ifndef SMDB_KEY_SIZE
#define SMDB_KEY_SIZE	512
#endif

#define NDEBUG 1

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#endif
#ifdef HAVE_SYS_FILE_H
# include <sys/file.h>
#endif

#ifdef __WIN32__
# if defined(__VISUALC__)
#  define _WINSOCK2API_
# endif
# include <windows.h>
# include <winsock2.h>
# include <com/snert/lib/io/Log.h>
#else
# include <syslog.h>
# include <sys/socket.h>
#endif

#if defined(S_SPLINT_S) && defined(__CYGWIN__)
# define WITHOUT_SYSLOG
# include <com/snert/lib/io/Log.h>
#endif

#include <com/snert/lib/util/Text.h>
#include <com/snert/lib/util/Token.h>
#include <com/snert/lib/util/setBitWord.h>
#include <com/snert/lib/mail/limits.h>
#include <com/snert/lib/mail/smdb.h>
#include <com/snert/lib/sys/pthread.h>

#define USAGE_SMDB_KEY_HAS_NUL						\
  "Key lookups must include the terminating NUL byte. Intended for\n"	\
"# Postfix with postmap(1) generated .db files; experimental.\n"	\
"#"

Option smdbOptDebug	= { "smdb-debug", 	"-", "Enable debugging of smdb routines." };
Option smdbOptKeyHasNul	= { "smdb-key-has-nul",	"-",  USAGE_SMDB_KEY_HAS_NUL };
Option smdbOptUseStat	= { "smdb-use-stat",	"-", "Use stat() instead of fstat() to monitor .db file updates; experimental." };

Option *smdbOptTable[] = {
	&smdbOptDebug,
	&smdbOptKeyHasNul,
	&smdbOptUseStat,
	NULL
};

/***********************************************************************
 *** smdb open routines. These 3 functions are a critical section.
 ***********************************************************************/

void
smdbSetDebug(int flag)
{
	smdbOptDebug.value = flag;
}

void
smdbSetKeyHasNul(smdb *sm, int flag)
{
	if (sm != NULL)
		sm->key_has_nul = flag != 0;
}

void
smdbClose(void *db)
{
#ifdef HAVE_DB_H
	smdb *sm = (smdb *) db;

	if (sm == NULL)
		return;

	if (sm->db != NULL) {
# if DB_VERSION_MAJOR == 1
		(void) sm->db->close(sm->db);
# else
		(void) sm->db->close(sm->db, 0);
# endif
	}

	(void) pthread_mutex_destroy(&sm->mutex);
	free(sm);
#endif /* HAVE_DB_H */
}

#ifdef HAVE_DB_H
static int
smdb_open(smdb *sm)
{
	struct stat finfo;
#if DB_VERSION_MAJOR == 1
	if ((sm->db = dbopen(sm->dbfile, O_RDONLY, 0, DB_HASH, NULL)) == NULL) {
		syslog(LOG_ERR, "open error \"%s\": %s (%d)", sm->dbfile, strerror(errno), errno);
		goto error0;
	}

	if ((sm->lockfd = sm->db->fd(sm->db)) == -1) {
		syslog(LOG_ERR, "get lock fd error \"%s\": %s (%d)", sm->dbfile, strerror(errno), errno);
		goto error1;
	}
#else
{
	int rc;

#ifdef ENABLE_DB_VERIFY
	rc = db_create(&sm->db, NULL, 0);
	if (rc != 0 || sm->db == NULL) {
		syslog(LOG_ERR, "db_create() error: %s", db_strerror(rc));
		goto error0;
	}

	if (smdbOptDebug.value) {
		sm->db->set_errpfx(sm->db, sm->dbfile);
		sm->db->set_errfile(sm->db, stderr);
	}

	rc = sm->db->verify(sm->db, sm->dbfile, NULL, NULL, 0);

	/* BDB 4.2.52 documentation:
	 *
	 *	The DB handle may not be accessed again after
	 *	DB->verify is called, regardless of its return.
	 *
	 * In previous releases, applications calling the DB->verify
	 * method had to explicitly discard the DB handle by calling
	 * the DB->close method. Further, using the DB handle in other
	 * ways after calling the DB->verify method was not prohibited
	 * by the documentation, although such use was likely to lead
	 * to problems.
	 *
	 * For consistency with other Berkeley DB methods, DB->verify
	 * method has been documented in the current release as a DB
	 * handle destructor. Applications using the DB handle in any
	 * way (including calling the DB->close method) after calling
	 * DB->verify should be updated to make no further use of any
	 * kind of the DB handle after DB->verify returns.
	 */
#if DB_VERSION_MAJOR < 4 || DB_VERSION_MINOR < 2
	(void) sm->db->close(sm->db, 0);
#endif
	if (rc != 0) {
		syslog(LOG_ERR, "\"%s\" failed verification: %s", sm->dbfile, db_strerror(rc));
		goto error0;
	}
#endif /* ENABLE_DB_VERIFY */

	rc = db_create(&sm->db, NULL, 0);
	if (rc != 0 || sm->db == NULL) {
		syslog(LOG_ERR, "db_create() error: %s", db_strerror(rc));
		goto error0;
	}

	if (smdbOptDebug.value) {
		sm->db->set_errpfx(sm->db, sm->dbfile);
		sm->db->set_errfile(sm->db, stderr);
	}

	/* Use the same default cache size as Sendmail. */
	rc = sm->db->set_cachesize(sm->db, 0, 1024*1024, 0);
	if (rc != 0)
		syslog(LOG_WARNING, "warning: failed to set cache size for \"%s\": %s", sm->dbfile, db_strerror(rc));

	rc = sm->db->open(sm->db, DBTXN sm->dbfile, NULL, DB_UNKNOWN, DB_RDONLY, 0);
	if (rc != 0) {
		syslog(LOG_ERR, "failed to open \"%s\": %s", sm->dbfile, db_strerror(rc));
		goto error1;
	}

	rc = sm->db->fd(sm->db, &sm->lockfd);
	if (rc != 0) {
		syslog(LOG_ERR, "get lock fd error \"%s\": %s", sm->dbfile, db_strerror(rc));
		goto error1;
	}
}
#endif
	if (fstat(sm->lockfd, &finfo)) {
		syslog(LOG_ERR, "stat error \"%s\": %s (%d)", sm->dbfile, strerror(errno), errno);
		goto error1;
	}

	sm->mtime = finfo.st_mtime;

	return 0;
error1:
#if DB_VERSION_MAJOR == 1
	(void) sm->db->close(sm->db);
#else
	(void) sm->db->close(sm->db, 0);
#endif
error0:
	sm->db = NULL;

	return -1;
}
#endif /* HAVE_DB_H */

smdb *
smdbOpen(const char *dbfile, int rdonly)
{
#ifdef HAVE_DB_H
	smdb *sm;
	size_t length;

	if (dbfile == NULL || dbfile[0] == '\0')
		goto error0;

	/* Allocate the structure and filename at once, since it
	 * never changes during the life time of the structure.
	 */
	length = strlen(dbfile);
	if ((sm = calloc(1, sizeof (*sm) + length + 1)) == NULL)
		goto error0;

	smdbSetKeyHasNul(sm, smdbOptKeyHasNul.value);
	sm->dbfile = (char *)(sm + 1);
	strcpy(sm->dbfile, dbfile);

	if (pthread_mutex_init(&sm->mutex, NULL)) {
		syslog(LOG_ERR, "mutex init in smdbOpen() failed: %s (%d)", strerror(errno), errno);
		goto error1;
	}

	if (smdb_open(sm))
		goto error1;

	return sm;
error1:
	smdbClose(sm);
error0:
#endif /* HAVE_DB_H */

	return NULL;
}

/***********************************************************************
 *** Common Routines
 ***********************************************************************/

#ifdef HAVE_DB_H

static char *
smdbGetValueInternal(smdb *sm, DBT *key)
{
	int rc;
	DBT value;
	char *string;
	struct stat finfo;

	string = NULL;

	/* Leave BDB with the responsibility of allocating and
	 * releasing the data passed back in a DBT. This is the
	 * only possibilty in BDB 1.85 (used by FreeBSD), but
	 * later versions have different memory management options.
	 *
	 * We don't have BDB 4 allocate this simply because we
	 * would have to reallocate it anyways to make room for
	 * a terminating null byte.
	 *
	 * As a result of FreeBSD being pricks by still using
	 * old BDB code, we have to use a mutex to protect this
	 * chunk of code.
	 */
	if (pthread_mutex_lock(&sm->mutex)) {
		syslog(LOG_ERR, "mutex lock in smdbGetValueInternal() failed: %s (%d)", strerror(errno), errno);
		goto error0;
	}

	/* The use of fstat() is more efficient IMHO, but some people
	 * think stat() will be just as efficient on most operating
	 * systems, which maintain a cache of file meta data in memory.
	 * stat() will be more reliable in detecting database file updates
	 * that use a build & swap instead of a simple overwrite.
	 *
	 * See Debian /etc/mail/Makefile concerning access.db updates.
	 */
	rc = smdbOptUseStat.value
		? stat(sm->dbfile, &finfo)
		: (sm->db == NULL ? -1 : fstat(sm->lockfd, &finfo));

	if (rc == 0 && sm->mtime != finfo.st_mtime) {
		/* If the file has been updated since the last access,
		 * then reopen the database to discard any local state.
		 */
		syslog(LOG_INFO, "reopening \"%s\"", sm->dbfile);
#if DB_VERSION_MAJOR == 1
		(void) sm->db->close(sm->db);
#else
		(void) sm->db->close(sm->db, 0);
#endif
		sm->db = NULL;
	}

	if (sm->db == NULL && smdb_open(sm))
		goto error1;

	/* Wait until we get the file lock. */
	do
		errno = 0;
	while (flock(sm->lockfd, LOCK_SH) && errno == EINTR);

	if (smdbOptDebug.value)
		syslog(LOG_DEBUG, "checking \"%s\" for \"%s\"", sm->dbfile, (char *) key->data);

	memset(&value, 0, sizeof (value));

	/* Postfix 2.3 with milter support. postmap(1) saves the terminating
	 * NUL byte as part of the key and value strings. Sendmail does not.
	 * So for key lookups we need to add one for the NUL byte. Then
	 * remove one afterwards so that the smdbAccess routines that loop
	 * over IP and domain string segments continue to function.
	 */
	key->size += sm->key_has_nul;

	/*@-compdef@*/
#if DB_VERSION_MAJOR == 1
	rc = sm->db->get(sm->db, key, &value, 0);
#else
	rc = sm->db->get(sm->db, NULL, key, &value, 0);
#endif
	/*@=compdef@*/

	key->size -= sm->key_has_nul;

	if (rc == DB_NOTFOUND)
		goto error2;

	if (rc != 0) {
		syslog(LOG_ERR, "sendmail db get \"%s\" error: %s", sm->dbfile, db_strerror(rc));
		goto error2;
	}

	if ((string = malloc(value.size+1)) == NULL)
		goto error2;

	(void) memcpy(string, value.data, (size_t) value.size);
	string[value.size] = '\0';

	if (smdbOptDebug.value)
		syslog(LOG_DEBUG, "access DB key=\"%s\" value=\"%s\"", (char *) key->data, string);
error2:
	if (flock(sm->lockfd, LOCK_UN)) {
		syslog(LOG_ERR, "drop shared-lock on \"%s\" failed: %s (%d)", sm->dbfile, strerror(errno), errno);
		free(string);
		string = NULL;
	}
error1:
	if (pthread_mutex_unlock(&sm->mutex)) {
		syslog(LOG_ERR, "mutex unlock in smdbGetValueInternal() failed: %s (%d)", strerror(errno), errno);
		free(string);
		string = NULL;
	}
error0:
	return string;
}

#endif /* HAVE_DB_H */

char *
smdbGetValue(smdb *sm, const char *key)
{
#ifdef HAVE_DB_H
	DBT k;

	if (sm == NULL || key == NULL)
		return NULL;

	memset(&k, 0, sizeof (k));
	k.size = (u_int32_t) strlen(key);
	k.data = (char *) key;

	return smdbGetValueInternal(sm, &k);
#else
	return NULL;
#endif
}

/***********************************************************************
 *** smdbAliases Routines
 ***********************************************************************/

int
smdbAliasesHasUser(Vector smdbAliasesList, char *user)
{
#ifdef HAVE_DB_H
	long i;
	/*@dependent@*/ DBT key;
	char *value;

	if (smdbAliasesList == NULL)
		return 0;

	/*** NOTE alaises database keys _include_ the null byte as part of
	 *** the key, which is different from the access database keys.
	 ***/
	memset(&key, 0, sizeof (key));
	key.size = (u_int32_t)(strlen(user) + 1);
	key.data = user;

	for (i = 0; i  < VectorLength(smdbAliasesList); i++) {
		smdb *entry = VectorGet(smdbAliasesList, i);
		if (entry == NULL) {
			syslog(LOG_CRIT, "null pointer for aliases database structure, index %ld", i);
			return -1;
		}

		if ((value = smdbGetValueInternal(entry, &key)) != NULL) {
			free(value);
			return 1;
		}
	}
#endif /* HAVE_DB_H */

	return 0;
}

/***********************************************************************
 *** smdbAccess Routines
 ***********************************************************************/

static int
smdbAccessStub(smdb *sm, /*@null@*/ const char *tag, /*@unique@*/ const char *find, /*@out@*//*@null@*/ char **keyp, char /*@out@*//*@null@*/ **valuep)
{
	if (keyp != NULL)
		*keyp = NULL;

	if (valuep != NULL)
		*valuep = NULL;

	return SMDB_ACCESS_NOT_FOUND;
}

/*
 * @param value
 *	An access database right-hand-side value.
 *
 * @return
 *	An SMDB_ACCESS_* code.
 */
int
smdbAccessCode(const char *value)
{
	long xyz;
	char *stop;

	if (value == NULL)
		return SMDB_ACCESS_NOT_FOUND;

	switch (toupper(value[0])) {
	case SMDB_ACCESS_DISCARD:
		/* Postfix 2.3 DUNNO same as Sendmail SKIP */
		if (toupper(value[2]) == 'U')
			return SMDB_ACCESS_SKIP;
		/*@fallthrough@*/

	case SMDB_ACCESS_OK:
	case SMDB_ACCESS_FRIEND:
	case SMDB_ACCESS_HATER:
	case SMDB_ACCESS_VERIFY:
		return toupper(value[0]);

	case 'E':
	case 'S':
		switch (toupper(value[1])) {
		case SMDB_ACCESS_ERROR:
		case SMDB_ACCESS_ENCR:
		case SMDB_ACCESS_SKIP:
		case SMDB_ACCESS_SUBJECT:
			return toupper(value[1]);
		}
		break;

	case 'R':
		if (value[1] == '\0')
			break;

		switch (toupper(value[2])) {
		case SMDB_ACCESS_RELAY:
		case SMDB_ACCESS_REJECT:
			return toupper(value[2]);
		}
		break;
	}

	xyz = strtol(value, &stop, 10);
	if (400 <= xyz && xyz < 600 && (isspace(*stop) || *stop == '\0'))
		return SMDB_ACCESS_ERROR;

	return SMDB_ACCESS_UNKNOWN;
}

/*
 * Return one of SMDB_ACCESS_UNKNOWN, SMDB_ACCESS_OK, or SMDB_ACCESS_REJECT
 * generalised from a specific SMDB_ACCESS_* code.
 */
int
smdbAccessIsOk(int status)
{
	switch (status) {
	case SMDB_ACCESS_OK:
	case SMDB_ACCESS_RELAY:
	case SMDB_ACCESS_FRIEND:
		return SMDB_ACCESS_OK;

	case SMDB_ACCESS_DISCARD:
		/* A DISCARD is technically an accept message followed
		 * by siliently dropping it in the bit bucket.
		 */
		return SMDB_ACCESS_OK;

	case SMDB_ACCESS_ERROR:
	case SMDB_ACCESS_HATER:
	case SMDB_ACCESS_REJECT:
		return SMDB_ACCESS_REJECT;

	case SMDB_ACCESS_SKIP:
		/* Used to short circuit a subnet/subdomain search without
		 * accepting or rejecting. For example:
		 *
		 *	Connect:128.32.2                SKIP
		 *	Connect:128.32                  RELAY
		 *
		 * Relay for all of 128.32.0.0/16 except 128.32.2.0/8,
		 * which skips the search without making a decision.
		 */
		return SMDB_ACCESS_UNKNOWN;

	default:
		if (status < 0)
			return SMDB_ACCESS_REJECT;
	}

	return SMDB_ACCESS_UNKNOWN;
}

#ifdef HAVE_DB_H
/*
 * Pass back allocated key/value strings or free them.
 * Return the SMDB_ACCESS_* code for the value string.
 */
static int
smdbAccessResult(char *key, char *value, /*@out@*//*@null@*/ char **keyp, char /*@out@*//*@null@*/ **valuep)
{
	int code = smdbAccessCode(value);

	if (keyp != NULL)
		*keyp = key;
	else
		free(key);

	if (valuep != NULL)
		*valuep = value;
	else
		free(value);

	return code;
}
#endif

/*
 * Lookup
 *
 *	tag:a.b.c.d
 *	tag:a.b.c
 *	tag:a.b
 *	tag:a
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
int
smdbAccessIp(smdb *sm, /*@null@*/ const char *tag, /*@unique@*/ const char *ip, /*@out@*//*@null@*/ char **keyp, char /*@out@*//*@null@*/ **valuep)
{
#ifdef HAVE_DB_H
	DBT key;
	char *k, *v;
	int delim, code;
	size_t tlength, klength;

#ifndef NDEBUG
	if (smdbOptDebug.value)
		syslog(LOG_DEBUG, "enter smdbAccessIp(%lx, %s, %s, %lx, %lx)", (long) sm, tag, ip, (long) keyp, (long) valuep);
#endif
	if (sm == NULL || ip == NULL)
		goto error0;

	memset(&key, 0, sizeof (key));

	if (tag == NULL)
		tag = "";
	tlength = strlen(tag);

	/* Allocate enough room to hold the largest possible string. */
	klength = tlength + IPV6_TAG_LENGTH + IPV6_STRING_LENGTH + 2;
	if ((k = calloc(1, klength)) == NULL)
		goto error0;

	if (strchr(ip, ':') != NULL) {
		ip += strncmp(ip, IPV6_TAG, IPV6_TAG_LENGTH) == 0 ? IPV6_TAG_LENGTH : 0;
		key.size = snprintf(k, klength, "%sipv6:%s:", tag, ip);
		tlength += IPV6_TAG_LENGTH;
		delim = ':';
	} else {
		key.size = snprintf(k, klength, "%s%s.", tag, ip);
		delim = '.';
	}

	/* Some older (broken) versions of snprintf() return -1 when
	 * the string is too large for the buffer. This can cause
	 * havoc here, causing an almost infinite loop and probably
	 * a crash. This can occur when the supplied IP address string
	 * is NOT an IP address, but some other string like a domain
	 * name. This should guard against such mistakes.
	 */
	if (klength <= key.size)
		goto error1;

	key.data = k;

	/* Note the trailing senteniel colon or dot delimiter is removed
	 * on the first pass through the loop. It triggers the first lookup.
	 */
	do {
#ifndef NDEBUG
		if (smdbOptDebug.value)
			syslog(LOG_DEBUG, "tlength=%lu key-size=%lu k={%s} ", tlength, key.size, k);
#endif
		if (k[key.size] == delim) {
			k[key.size] = '\0';

			if ((v = smdbGetValueInternal(sm, &key)) != NULL) {
				code = smdbAccessResult(k, v, keyp, valuep);
#ifndef NDEBUG
				if (smdbOptDebug.value)
					syslog(LOG_DEBUG, "exit smdbAccessIp(%lx, %s, %s, %lx, %lx) code=%d", (long) sm, tag, ip, (long) keyp, (long) valuep, code);
#endif
				return code;
			}
		}
	} while (tlength < --key.size);
error1:
	free(k);
error0:
#ifndef NDEBUG
	if (smdbOptDebug.value)
		syslog(LOG_DEBUG, "exit smdbAccessIp(%lx, %s, %s, %lx, %lx) code=0", (long) sm, tag, ip, (long) keyp, (long) valuep);
#endif
#endif /* HAVE_DB_H */

	return smdbAccessStub(sm, tag, ip, keyp, valuep);
}

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
int
smdbAccessDomain(smdb *sm, /*@null@*/ const char *tag, /*@unique@*/ const char *domain, /*@out@*/ char **keyp, char /*@out@*/ **valuep)
{
#ifdef HAVE_DB_H
	DBT key;
	char *k, *v;
	int resolved, code;
	size_t tlength, klength;

#ifndef NDEBUG
	if (smdbOptDebug.value)
		syslog(LOG_DEBUG, "enter smdbAccessDomain(%lx, %s, %s, %lx, %lx)", (long) sm, tag, domain, (long) keyp, (long) valuep);
#endif
	if (sm == NULL || domain == NULL)
		goto error0;

	if (tag == NULL)
		tag = "";
	tlength = strlen(tag);

	/* Allocate enough room to hold the largest possible string. */
	klength = tlength + SMTP_DOMAIN_LENGTH + 1;
	if ((k = calloc(1, klength)) == NULL)
		goto error0;

	memset(&key, 0, sizeof (key));
	if (klength <= (size_t) snprintf(k, klength, "%s", tag))
		goto error1;

	/* If the domain didn't resolve, then its an ip as domain name form
	 * so we only want to do one lookup on the whole and avoid the parent
	 * domain lookups.
	 */
	resolved = domain[0] != '[';

	/* Assume that the domain starts with a leading dot (.) */
	domain--;

	do {
		(void) strncpy(k + tlength, domain+1, klength - tlength);
		key.size = (u_int32_t) strlen(k);
		TextLower(k, -1);
		key.data = k;

		/* Remove trailing (root) dot just before the '\0' from domain name. */
		if (1 < key.size && k[key.size - 1] == '.')
			k[--key.size] = '\0';
#ifndef NDEBUG
		if (smdbOptDebug.value)
			syslog(LOG_DEBUG, "tlength=%lu key-size=%lu k={%s} ", tlength, key.size, k);
#endif
		if ((v = smdbGetValueInternal(sm, &key)) != NULL) {
			code = smdbAccessResult(k, v, keyp, valuep);
#ifndef NDEBUG
			if (smdbOptDebug.value)
				syslog(LOG_DEBUG, "exit smdbAccessDomain(%lx, %s, %s, %lx, %lx) code=%d", (long) sm, tag, domain, (long) keyp, (long) valuep, code);
#endif
			return code;
		}
	} while (resolved && (domain = strchr(domain+1, '.')) != NULL && domain[1] != '\0');
error1:
	free(k);
error0:
#ifndef NDEBUG
	if (smdbOptDebug.value)
		syslog(LOG_DEBUG, "exit smdbAccessDomain(%lx, %s, %s, %lx, %lx) code=0", (long) sm, tag, domain, (long) keyp, (long) valuep);
#endif
#endif /* HAVE_DB_H */

	return smdbAccessStub(sm, tag, domain, keyp, valuep);
}

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
int
smdbAccessMail(smdb *sm, /*@null@*/ const char *tag, /*@unique@*/ const char *mail, /*@out@*/ char **keyp, char /*@out@*/ **valuep)
{
#ifdef HAVE_DB_H
	DBT key;
	char *k, *v;
	int code, atsign;
	size_t tlength, klength;

#ifndef NDEBUG
	if (smdbOptDebug.value)
		syslog(LOG_DEBUG, "enter smdbAccessMail(%lx, %s, %s, %lx, %lx)", (long) sm, tag, mail, (long) keyp, (long) valuep);
#endif
	if (sm == NULL || mail == NULL)
		goto error0;

	if (tag == NULL)
		tag = "";
	tlength = strlen(tag);

	/* Allocate enough room to hold the largest possible string. */
	klength = tlength + SMTP_PATH_LENGTH + 1;
	if ((k = calloc(1, klength)) == NULL)
		goto error0;

	memset(&key, 0, sizeof (key));

	/* Lookup tag:mail */
	key.size = (u_int32_t) snprintf(k, klength, "%s%s", tag, mail);
	if (klength <= key.size)
		goto error1;

	TextLower(k, -1);
	key.data = k;

	if ((v = smdbGetValueInternal(sm, &key)) != NULL)
		goto result;

	atsign = strcspn(k, "@");
	if (k[atsign] == '@') {
		/* Lookup tag:domain */
		if ((code = smdbAccessDomain(sm, tag, k + atsign + 1, keyp, valuep)) != SMDB_ACCESS_NOT_FOUND) {
			free(k);
			return code;
		}

		/* Lookup tag:account@ */
		atsign = strcspn(k, "+@");
		k[atsign] = '@';
		k[atsign+1] = '\0';
		key.size = (u_int32_t) strlen(k);
		key.data = k;

		if ((v = smdbGetValueInternal(sm, &key)) != NULL) {
result:
			code = smdbAccessResult(k, v, keyp, valuep);
#ifndef NDEBUG
			if (smdbOptDebug.value)
				syslog(LOG_DEBUG, "exit smdbAccessMail(%lx, %s, %s, %lx, %lx) code=%d", (long) sm, tag, mail, (long) keyp, (long) valuep, code);
#endif
			return code;
		}
	}
error1:
	free(k);
error0:
#ifndef NDEBUG
	if (smdbOptDebug.value)
		syslog(LOG_DEBUG, "exit smdbAccessMail(%lx, %s, %s, %lx, %lx) code=0", (long) sm, tag, mail, (long) keyp, (long) valuep);
#endif
#endif /* HAVE_DB_H */

	return smdbAccessStub(sm, tag, mail, keyp, valuep);
}

int
smdbAccessIp2(smdb *sm, /*@null@*/ const char *tag, /*@unique@*/ const char *ip, /*@out@*//*@null@*/ char **keyp, char /*@out@*//*@null@*/ **valuep)
{
	int rc;

	if ((rc = smdbAccessIp(sm, tag, ip, keyp, valuep)) == SMDB_ACCESS_NOT_FOUND)
		rc = smdbAccessIp(sm, NULL, ip, keyp, valuep);

	return rc;
}

int
smdbAccessDomain2(smdb *sm, /*@null@*/ const char *tag, /*@unique@*/ const char *domain, /*@out@*//*@null@*/ char **keyp, char /*@out@*//*@null@*/ **valuep)
{
	int rc;

	if ((rc = smdbAccessDomain(sm, tag, domain, keyp, valuep)) == SMDB_ACCESS_NOT_FOUND)
		rc = smdbAccessDomain(sm, NULL, domain, keyp, valuep);

	return rc;
}

int
smdbAccessMail2(smdb *sm, /*@null@*/ const char *tag, /*@unique@*/ const char *mail, /*@out@*//*@null@*/ char **keyp, char /*@out@*//*@null@*/ **valuep)
{
	int rc;

	if ((rc = smdbAccessMail(sm, tag, mail, keyp, valuep)) == SMDB_ACCESS_NOT_FOUND)
		rc = smdbAccessMail(sm, NULL, mail, keyp, valuep);

	return rc;
}

/***********************************************************************
 *** Sendmail cf parse routines.
 ***********************************************************************/

smdb * smdbMailer;

smdb * smdbVuser;
smdb * smdbAccess;
Vector smdbAliases;

#ifdef HAVE_DB_H

static int
parseCfKline(smdb **sm, char *line)
{
	Vector words;
	int rc, dbtype;
	/*@exposed@*/ char *word;
	char *filename;

	rc = -1;

	if (smdbOptDebug.value)
		syslog(LOG_DEBUG, "parseCfKline(%s)", line);

	words = TextSplit(line, " \t", 0);
	if (words == NULL) {
		syslog(LOG_ERR, "failed to split line, %s: %s", strerror(errno), line);
		goto error0;
	}

	if ((word = VectorGet(words, VectorLength(words)-1)) == NULL) {
		syslog(LOG_ERR, "failed to get last word");
		goto error1;
	}

	/*@-branchstate@*/
	if (TextInsensitiveEndsWith(word, ".db") < 0) {
		size_t n = strlen(word) + 4;
		/*@-exposetrans -dependenttrans@*/
		if ((word = realloc(word, n)) == NULL) {
			syslog(LOG_ERR, "failed to realloc() word");
			goto error1;
		}
		/*=dependenttrans@*/
		(void) VectorReplace(words, VectorLength(words)-1, word);
		/*@=exposetrans@*/
		(void) strncat(word, ".db", n);
	}
	/*@=branchstate@*/

	if (smdbOptDebug.value)
		syslog(LOG_DEBUG, "accessFile=%s", word);

	filename = word;
#if DB_VERSION_MAJOR == 1
	if ((word = VectorGet(words, 1)) == NULL) {
		syslog(LOG_ERR, "failed to get database type");
		goto error1;
	}

	if (TextInsensitiveCompare(word, "btree") == 0) {
		dbtype = DB_BTREE;
	} else if (TextInsensitiveCompare(word, "hash") == 0) {
		dbtype = DB_HASH;
	} else {
		syslog(LOG_ERR, "unsupported database type");
		goto error1;
	}
#else
	/* Later versions can determine the database type themselves. */
	dbtype = DB_UNKNOWN;
#endif
	if ((*sm = smdbOpen((const char *) filename, dbtype)) != NULL)
		rc = 0;
error1:
	VectorDestroy(words);
error0:
	return rc;
}

static int
parseCfAliasFile(char *line)
{
	smdb *sm;
	int dbtype;
	char *token;
	const char *next;

	if (smdbOptDebug.value)
		syslog(LOG_DEBUG, "parseCfAliasFile(%s)", line);

#if DB_VERSION_MAJOR == 1
	dbtype = DB_HASH;
#else
	dbtype = DB_UNKNOWN;
#endif
	next = line + sizeof("O AliasFile=") - 1;

	while ((token = TokenNext(next, &next, ", \t", 0)) != NULL) {
		if (TextInsensitiveEndsWith(token, ".db") < 0) {
			size_t n = strlen(token) + 4;
			if ((token = realloc(token, n)) == NULL) {
				syslog(LOG_ERR, "failed to realloc() token");
				goto error1;
			}
			(void) strncat(token, ".db", n);
		}

		if (smdbOptDebug.value)
			syslog(LOG_DEBUG, "aliasesFile=%s", token);

		if ((sm = smdbOpen((const char *) token, dbtype)) == NULL) {
			syslog(LOG_ERR, "failed to open aliasesFile=%s", token);
			goto error1;
		}

		/*@-nullpass@*/
		(void) VectorAdd(smdbAliases, (void *) sm);
		/*@=nullpass@*/
		free(token);
	}

	return 0;
	/*@-usereleased@*/
error1:
	free(token);
/* error0: */
	return -1;
	/*@=usereleased@*/
}

#endif /* HAVE_DB_H */

char *smMasqueradeAs;

static int
parseCfGetMacro(char *line, char *macro, char **value)
{
	if (*line == 'D' && 0 < TextSensitiveStartsWith(line+1, macro))
		*value = TextDup(line + 1 + strlen(macro));

	return 0;
}

SmClientPortOptions smClientPortInet4;
SmClientPortOptions smClientPortInet6;

static int
parseCfClientPortOptions(char *line)
{
	char *here, *p;
	SmClientPortOptions *cp;

	if (smdbOptDebug.value)
		syslog(LOG_DEBUG, "parseCfClientPortOptions(%s)", line);

#ifdef AF_INET6
	if (strstr(line, "Family=inet6") != NULL) {
		cp = &smClientPortInet6;
		cp->family = AF_INET6;
	} else
#endif
	{
		cp = &smClientPortInet4;
		/*@-unrecog@*/
		cp->family = AF_INET;
		/*@=unrecog@*/
	}

	cp->port = 25;
	cp->useForHelo = 0;
	cp->dontUseAuth = 0;
	cp->dontUseStartTls = 0;
	/*@-mustfreeonly@*/
	cp->address = NULL;
	/*@=mustfreeonly@*/

	if ((here = strstr(line, "Port=")) != NULL)
		/* No support for port by service name. */
		cp->port = strtol(here, NULL, (int) sizeof ("Port"));

	if ((here = strstr(line, "Address=")) != NULL)
		cp->address = TextSubstring(here, (long) sizeof ("Address"), (long) strcspn(here + sizeof ("Address"), ","));

	if ((here = strstr(line, "Modifier=")) != NULL) {
		for (p = here + sizeof ("Modifier"); *p != '\0' && *p != ','; p++) {
			switch (*p) {
			case 'h': cp->useForHelo = 1; break;
			case 'A': cp->dontUseAuth = 1; break;
			case 'S': cp->dontUseStartTls = 1; break;
			}
		}
	}

	return 0;
}

/*
 * Read the sendmail.cf file looking for support files.
 */
int
readSendmailCf(char *cf, long flags)
{
	int rc;
	FILE *fp;
	char *line;

	rc = -1;
	smdbVuser = NULL;
	smdbAccess = NULL;
	smdbAliases = NULL;

	if (cf == NULL) {
		syslog(LOG_ERR, "sendmail.cf file path undefined");
		goto error0;
	}

	if ((fp = fopen(cf, "r")) == NULL) {
		syslog(LOG_ERR, "%s: %s (%d)", cf, strerror(errno), errno);
		goto error0;
	}

	if ((line = malloc(SMCF_LINE_SIZE)) == NULL) {
		syslog(LOG_ERR, "readSendmailCf() %s (%d)", strerror(errno), errno);
		goto error1;
	}

	if ((smdbAliases = VectorCreate(5)) == NULL) {
		syslog(LOG_ERR, "readSendmailCf() %s (%d)", strerror(errno), errno);
		goto error2;
	}

	VectorSetDestroyEntry(smdbAliases, smdbClose);

	/*@-usedef@*/
	while (0 <= TextInputLine(fp, line, SMCF_LINE_SIZE)) {
	/*@=usedef@*/
#ifdef HAVE_DB_H
		if ((flags & SMDB_OPEN_ACCESS) && 0 < TextSensitiveStartsWith(line, "Kaccess")) {
			if (parseCfKline(&smdbAccess, line))
				goto error2;
			continue;
		}
		if ((flags & SMDB_OPEN_VIRTUSER) && 0 < TextSensitiveStartsWith(line, "Kvirtuser")) {
			if (parseCfKline(&smdbVuser, line))
				goto error2;
			continue;
		}
		if ((flags & SMDB_OPEN_ALIASES) && 0 < TextSensitiveStartsWith(line, "O AliasFile=")) {
			if (parseCfAliasFile(line)) {
				VectorDestroy(smdbAliases);
				goto error2;
			}
			continue;
		}
#endif /* HAVE_DB_H */
		if (0 < TextSensitiveStartsWith(line, "O ClientPortOptions=")) {
			if (parseCfClientPortOptions(line))
				goto error2;
			continue;
		}

		(void) parseCfGetMacro(line, "M", &smMasqueradeAs);
	}

	if (feof(fp))
		rc = 0;
error2:
	free(line);
error1:
	(void) fclose(fp);
error0:
	return rc;
}


/***********************************************************************
 *** END
 ***********************************************************************/

