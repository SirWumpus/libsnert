/*
 * smdb.c
 *
 * Sendmail Database Support
 *
 * Copyright 2002, 2007 by Anthony Howe. All rights reserved.
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
# if defined(HAVE_GRP_H)
#  include <grp.h>
# endif
# if defined(HAVE_PWD_H)
#  include <pwd.h>
# endif
# if defined(HAVE_SYSLOG_H)
#  include <syslog.h>
# endif
# if defined(HAVE_SYS_WAIT_H)
#  include <sys/wait.h>
# endif
#endif

#include <com/snert/lib/io/Log.h>
#include <com/snert/lib/util/Text.h>
#include <com/snert/lib/util/Token.h>
#include <com/snert/lib/mail/limits.h>
#include <com/snert/lib/mail/smdb.h>

#define USAGE_SMDB_KEY_HAS_NUL						\
  "Key lookups must include the terminating NUL byte. Intended for\n"	\
"# Postfix with postmap(1) generated .db files; experimental.\n"	\
"#"

Option smdbOptDebug	= { "smdb-debug", 	"-", "Enable debugging of smdb routines." };
Option smdbOptKeyHasNul	= { "smdb-key-has-nul",	"-",  USAGE_SMDB_KEY_HAS_NUL };
Option smdbOptUseStat	= { "smdb-use-stat",	"-", "Use stat() instead of fstat() to monitor .db file updates; experimental." };
Option smdbOptRelayOk	= { "smdb-relay-ok",	"-", "Treat a RELAY value same as OK (white-list), else is unknown." };

Option *smdbOptTable[] = {
	&smdbOptDebug,
	&smdbOptKeyHasNul,
	&smdbOptRelayOk,
	&smdbOptUseStat,
	NULL
};

smdb *smdbAccess;
smdb *smdbVuser;

/***********************************************************************
 *** smdb open routines. These 3 functions are a critical section.
 ***********************************************************************/

void
smdbSetDebug(int level)
{
	smdbOptDebug.value = level;
}

void
smdbSetKeyHasNul(smdb *sm, int flag)
{
	if (sm != NULL)
		sm->_mode = flag ? (sm->_mode |= KVM_MODE_KEY_HAS_NUL) : (sm->_mode &= ~KVM_MODE_KEY_HAS_NUL);
}

void
smdbClose(void *sm)
{
	if (sm != NULL) {
		((smdb *) sm)->close(sm);
	}
}

smdb *
smdbOpen(const char *dbfile, int rdonly)
{
	smdb *sm;
	int mode = 0;
	char *table, *delim, *file;

	errno = 0;

	if (dbfile == NULL || dbfile[0] == '\0') {
		errno = EFAULT;
		goto error0;
	}

	if ((file = strdup(dbfile)) == NULL)
		goto error0;

	if (rdonly)
		mode |= KVM_MODE_READ_ONLY;

	if ((delim = strchr(file, KVM_DELIM)) != NULL && strchr(delim+1, KVM_DELIM) != NULL) {
		table = file;
		*delim++ = '\0';

		if (0 < TextInsensitiveStartsWith(delim, "read-only" KVM_DELIM_S)) {
			delim += sizeof ("read-only" KVM_DELIM_S)-1;
			mode |= KVM_MODE_READ_ONLY;
		}
	} else if (0 < TextSensitiveEndsWith(file, "access.db")) {
		table = "access";
	} else if (0 < TextSensitiveEndsWith(file, "mailertable.db")) {
		table = "mailertable";
	} else if (0 < TextSensitiveEndsWith(file, "virtusertable.db")) {
		table = "virtuser";
	} else {
		table = "unknown";
	}

	if ((sm = kvmOpen(table, delim == NULL ? file : delim, mode)) == NULL)
		goto error1;

	smdbSetKeyHasNul(sm, smdbOptKeyHasNul.value);
	free(file);

	return sm;
error1:
	free(file);
error0:
	if (1 < smdbOptDebug.value)
		syslog(LOG_ERR, "smdbOpen(%s, %d) failed: %s (%d)", dbfile, rdonly, strerror(errno), errno);

	return NULL;
}

static char *
smdbGetValue2(smdb *sm, const char *key, size_t length)
{
	int rc;
	kvm_data k, v;

	k.data = (unsigned char *) key;
	k.size = length;

	memset(&v, 0, sizeof (v));

	if ((rc = sm->fetch(sm, &k, &v)) != KVM_OK) {
		free(v.data);
		v.data = NULL;
	}

	if (0 < smdbOptDebug.value)
		syslog(LOG_DEBUG, "map=\"%s\" key=%lu:\"%s\" value=\"%s\" rc=%d", sm->_table, (unsigned long) length, key, TextEmpty((char *) v.data), rc);

	return (char *) v.data;
}

char *
smdbGetValue(smdb *sm, const char *key)
{
	if (sm == NULL || key == NULL)
		return NULL;

	return smdbGetValue2(sm, key, strlen(key));
}

/***********************************************************************
 *** smdbAccess Routines
 ***********************************************************************/

static int
smdbAccessStub(smdb *sm, const char *tag, const char *find, char **keyp, char **valuep)
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
	case SMDB_ACCESS_FRIEND:
		return SMDB_ACCESS_OK;

	case SMDB_ACCESS_RELAY:
		if (smdbOptRelayOk.value)
			return SMDB_ACCESS_OK;
		break;

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

/*
 * Pass back allocated key/value strings or free them.
 * Return the SMDB_ACCESS_* code for the value string.
 */
static int
smdbAccessResult(char *key, char *value, char **keyp, char **valuep)
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
smdbAccessIp(smdb *sm, const char *tag, const char *ip, char **keyp, char **valuep)
{
	char *k, *v;
	int delim, code;
	size_t tlength, klength, length;

	if (1 < smdbOptDebug.value)
		syslog(LOG_DEBUG, "enter smdbAccessIp(%lx, %s, %s, %lx, %lx)", (long) sm, TextNull(tag), TextNull(ip), (long) keyp, (long) valuep);

	if (sm == NULL || ip == NULL)
		goto error0;

	if (tag == NULL)
		tag = "";
	tlength = strlen(tag);

	/* Allocate enough room to hold the largest possible string. */
	length = tlength + IPV6_TAG_LENGTH + IPV6_STRING_LENGTH + 2;
	if ((k = calloc(1, length)) == NULL)
		goto error0;

	if (strchr(ip, ':') != NULL) {
		ip += strncmp(ip, IPV6_TAG, IPV6_TAG_LENGTH) == 0 ? IPV6_TAG_LENGTH : 0;
		klength = snprintf(k, length, "%sipv6:%s:", tag, ip);
		tlength += IPV6_TAG_LENGTH;
		delim = ':';
	} else {
		klength = snprintf(k, length, "%s%s.", tag, ip);
		delim = '.';
	}

	/* Some older (broken) versions of snprintf() return -1 when
	 * the string is too large for the buffer. This can cause
	 * havoc here, causing an almost infinite loop and probably
	 * a crash. This can occur when the supplied IP address string
	 * is NOT an IP address, but some other string like a domain
	 * name. This should guard against such mistakes.
	 */
	if (length <= klength)
		goto error1;

	/* Note the trailing senteniel colon or dot delimiter is removed
	 * on the first pass through the loop. It triggers the first lookup.
	 */
	do {
		if (2 < smdbOptDebug.value)
			syslog(LOG_DEBUG, "tlength=%lu klength=%lu k={%s}", (unsigned long) tlength, (unsigned long) klength, k);

		if (k[klength] == delim) {
			k[klength] = '\0';

			if ((v = smdbGetValue2(sm, k, klength)) != NULL) {
				code = smdbAccessResult(k, v, keyp, valuep);

				if (1 < smdbOptDebug.value)
					syslog(LOG_DEBUG, "exit smdbAccessIp(%lx, %s, %s, %lx, %lx) code=%d", (long) sm, TextNull(tag), TextNull(ip), (long) keyp, (long) valuep, code);

				return code;
			}
		}
	} while (tlength < --klength);
error1:
	free(k);
error0:
	if (1 < smdbOptDebug.value)
		syslog(LOG_DEBUG, "exit smdbAccessIp(%lx, %s, %s, %lx, %lx) code=0", (long) sm, TextNull(tag), TextNull(ip), (long) keyp, (long) valuep);

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
smdbAccessDomain(smdb *sm, const char *tag, const char *domain, char **keyp, char **valuep)
{
	char *k, *v;
	kvm_data key;
	int resolved, code;
	size_t tlength, klength;

	if (1 < smdbOptDebug.value)
		syslog(LOG_DEBUG, "enter smdbAccessDomain(%lx, %s, %s, %lx, %lx)", (long) sm, TextNull(tag), TextNull(domain), (long) keyp, (long) valuep);

	if (sm == NULL || domain == NULL || *domain == '\0')
		goto error0;

	if (tag == NULL)
		tag = "";
	tlength = strlen(tag);

	/* Allocate enough room to hold the largest possible string. */
	klength = tlength + SMTP_DOMAIN_LENGTH + 1;
	if ((k = calloc(1, klength)) == NULL)
		goto error0;

	if (klength <= (size_t) snprintf(k, klength, "%s", tag))
		goto error1;

	/* If the domain didn't resolve, then its an ip as domain name form
	 * so we only want to do one lookup on the whole and avoid the parent
	 * domain lookups.
	 */
	resolved = domain[0] != '[';

	/* Assume that the domain starts with a leading dot (.) */
	domain--;
	key.data = (unsigned char *) k;

	do {
		(void) strncpy(k + tlength, domain+1, klength - tlength);
		key.size = strlen(k);
		TextLower(k, -1);

		/* Remove trailing (root) dot just before the '\0' from domain name. */
		if (1 < key.size && k[key.size - 1] == '.')
			k[--key.size] = '\0';

		if (2 < smdbOptDebug.value)
			syslog(LOG_DEBUG, "tlength=%lu key-size=%lu k={%s} ", (unsigned long) tlength, key.size, k);

		if ((v = smdbGetValue2(sm, (char *) key.data, key.size)) != NULL) {
			code = smdbAccessResult(k, v, keyp, valuep);

			if (1 < smdbOptDebug.value)
				syslog(LOG_DEBUG, "exit smdbAccessDomain(%lx, %s, %s, %lx, %lx) code=%d", (long) sm, TextNull(tag), TextNull(domain), (long) keyp, (long) valuep, code);

			return code;
		}
	} while (resolved && (domain = strchr(domain+1, '.')) != NULL && domain[1] != '\0');
error1:
	free(k);
error0:
	if (1 < smdbOptDebug.value)
		syslog(LOG_DEBUG, "exit smdbAccessDomain(%lx, %s, %s, %lx, %lx) code=0", (long) sm, TextNull(tag), TextNull(domain), (long) keyp, (long) valuep);

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
smdbAccessMail(smdb *sm, const char *tag, const char *mail, char **keyp, char **valuep)
{
	char *k, *v;
	kvm_data key;
	int code, atsign;
	size_t tlength, klength;

	if (1 < smdbOptDebug.value)
		syslog(LOG_DEBUG, "enter smdbAccessMail(%lx, %s, %s, %lx, %lx)", (long) sm, TextNull(tag), TextNull(mail), (long) keyp, (long) valuep);

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
	key.size = snprintf(k, klength, "%s%s", tag, mail);
	if (klength <= key.size)
		goto error1;

	TextLower(k, -1);
	key.data = (unsigned char *) k;

	if ((v = smdbGetValue2(sm, (char *) key.data, key.size)) != NULL)
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
		key.size = strlen(k);
		key.data = (unsigned char *) k;

		if ((v = smdbGetValue2(sm, (char *) key.data, key.size)) != NULL) {
result:
			code = smdbAccessResult(k, v, keyp, valuep);

			if (1 < smdbOptDebug.value)
				syslog(LOG_DEBUG, "exit smdbAccessMail(%lx, %s, %s, %lx, %lx) code=%d", (long) sm, TextNull(tag), TextNull(mail), (long) keyp, (long) valuep, code);

			return code;
		}
	}
error1:
	free(k);
error0:
	if (1 < smdbOptDebug.value)
		syslog(LOG_DEBUG, "exit smdbAccessMail(%lx, %s, %s, %lx, %lx) code=0", (long) sm, TextNull(tag), TextNull(mail), (long) keyp, (long) valuep);

	return smdbAccessStub(sm, tag, mail, keyp, valuep);
}

#ifdef ENABLE_SENDMAIL_TAGLESS_RECORDS
int
smdbAccessIp2(smdb *sm, const char *tag, const char *ip, char **keyp, char **valuep)
{
	int rc;

	if ((rc = smdbAccessIp(sm, tag, ip, keyp, valuep)) == SMDB_ACCESS_NOT_FOUND)
		rc = smdbAccessIp(sm, NULL, ip, keyp, valuep);

	return rc;
}

int
smdbAccessDomain2(smdb *sm, const char *tag, const char *domain, char **keyp, char **valuep)
{
	int rc;

	if ((rc = smdbAccessDomain(sm, tag, domain, keyp, valuep)) == SMDB_ACCESS_NOT_FOUND)
		rc = smdbAccessDomain(sm, NULL, domain, keyp, valuep);

	return rc;
}

int
smdbAccessMail2(smdb *sm, const char *tag, const char *mail, char **keyp, char **valuep)
{
	int rc;

	if ((rc = smdbAccessMail(sm, tag, mail, keyp, valuep)) == SMDB_ACCESS_NOT_FOUND)
		rc = smdbAccessMail(sm, NULL, mail, keyp, valuep);

	return rc;
}
#endif

/***********************************************************************
 *** END
 ***********************************************************************/

