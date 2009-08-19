/*
 * smdb.c
 *
 * Copyright 2002, 2007 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
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

static const char usage_smdb_key_has_nul[] =
  "Key lookups must include the terminating NUL byte. Intended for\n"
"# Postfix with postmap(1) generated .db files; experimental.\n"
"#"
;

Option smdbOptDebug	= { "smdb-debug", 	"-", "Enable debugging of smdb routines." };
Option smdbOptKeyHasNul	= { "smdb-key-has-nul",	"-",  usage_smdb_key_has_nul };
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
 *** Internal API for single and double key lookups.
 ***********************************************************************/

/*
 * Reduce an IPv4 or IPv6 address right-to-left, by one delimited
 * segment (octet for IPv4 or 16-bit word for IPv6). Compact and
 * IPv4-in-IPv6 are also permitted.
 *
 *	tag:123.45.67.89\0
 *	tag:123.45.67\0
 *	tag:123.45\0
 *	tag:123\0
 *
 *	tag:a:b:c:d:e:f:g:h\0
 *	tag:a:b:c:d:e:f:g\0
 *	tag:a:b:c:d:e:f\0
 *	tag:a:b:c:d:e\0
 *	tag:a:b:c:d\0
 *	tag:a:b:c\0
 *	tag:a:b\0
 *	tag:a\0
 *
 *	tag:2001:0DB8::1234\0
 *	tag:2001:0DB8\0
 *	tag:2001\0
 *
 *	tag:2001:0DB8::123.45.67.89\0
 *	tag:2001:0DB8::123.45.67\0
 *	tag:2001:0DB8::123.45\0
 *	tag:2001:0DB8::123\0
 *	tag:2001:0DB8\0
 *	tag:2001\0
 */
static int
reduceIp(size_t prefix, kvm_data *key)
{
	if (key->data[prefix] == 'i')
		prefix += sizeof ("ipv6:")-1;

	for ( ; prefix < key->size; key->size--) {
		if (key->data[key->size] == '.' || key->data[key->size] == ':') {
			if (key->data[key->size-1] == ':')
				key->size--;
			key->data[key->size] = '\0';
			return 1;
		}
	}

	return 0;
}

/*
 * Reduce a domain one label at  time from left to right.
 *
 *	tag:sub.domain.tld\0
 *	tag:domain.tld\0
 *	tag:tld\0
 */
static int
reduceDomain(size_t prefix, kvm_data *key)
{
	int span, eos;
	unsigned char *base;

	if (prefix < key->size) {
		/* size=18 tag:sub.domain.tld\0
		 *         ^
		 * size=14 tag:tag:domain.tld\0
		 *             ^
		 * size=7  tag:tag:domtag:tld\0
		 *                    ^
		 * size=4  tag:tag:domtag:tag:
		 *                        ^
		 */
		base = key->data+prefix;
		span = strcspn((char *) base, *base == '[' ? "]" : ".");

		/* Include delimiter as well. */
		span += base[span] != '\0';

		/* Now are was at the end of string? */
		eos = base[span] == '\0';

		/* Move the tag up AND erase the null byte if we're
		 * at the end of the string. This is for reduceMail's
		 * second string for the user account.
		 */
		memmove(base+span+eos-prefix, key->data, prefix);
		key->data = base+span+eos-prefix;
		key->size -= span;
	}

	return prefix < key->size;
}

/*
 * Reduce an email address.
 *
 *	tag:local.part@sub.domain.tld\0
 *	tag:sub.domain.tld\0
 *	tag:domain.tld\0
 *	tag:tld\0
 *	tag:local.part@\0
 *
 * Assumes original key format is:
 *
 *	tag:local.part@sub.domain.tld\0local.part@\0
 */
static int
reduceMail(size_t prefix, kvm_data *key)
{
	int span;
	unsigned char *base;

	if (key->data[key->size-1] == '@')
		return 0;

	base = key->data+prefix;
	span = strcspn((char *) base, "@");
	if (base[span] == '@') {
		span++;
		memmove(base+span-prefix, key->data, prefix);
		key->data = base+span-prefix;
		key->size -= span;
		return 1;
	}

	/* size=28 tag:user.name@sub.domain.tld\0user.name@\0
	 * size=18 user.ntag:sub.domain.tld\0user.name@\0
	 * size=14 user.ntag:tag:domain.tld\0user.name@\0
	 * size=7  user.ntag:tag:domtag:tld\0user.name@\0
	 * size=4  user.ntag:tag:domtag:tag:user.name@\0
	 */
	if (reduceDomain(prefix, key))
		return 1;

	key->size = strlen((char *) key->data);
	/* size=14 user.ntag:tag:domtag:tag:user.name@\0
	 */

	return prefix < key->size;
}

char *
tagInvalid(const char *tag1, const char *tag2)
{
	char *str;
	long length;
	size_t tag1_len, tag2_len = 0;

	tag1_len = strlen(tag1) + sizeof ("invalid");
	tag2_len = tag2 == NULL ? 0 : (strlen(tag2) + sizeof ("invalid"));

	if ((str = malloc(tag1_len + tag2_len)) != NULL) {
		length = TextCopy(str, tag1_len, tag1);
		length += TextCopy(str+length, tag1_len-length, "invalid");

		if (tag2 != NULL) {
			length += TextCopy(str+length, tag1_len+tag2_len-length, tag2);
			length += TextCopy(str+length, tag1_len+tag2_len-length, "invalid");
		}
	}

	return str;
}

static smdb_result
singleKey(smdb *sm, char **keyp, char **valuep, const char *tag1, const char *key1, int (*reduceKey)(size_t prefix, kvm_data *key))
{
	char *str;
	kvm_data k, v;
	smdb_result rc;
	int span, plus_sign;
	size_t tag1_len, key1_len, str_len;

	*valuep = NULL;
	if (keyp != NULL)
		*keyp = NULL;
	rc = SMDB_ERROR;

#ifndef TEST
	if (sm == NULL || key1 == NULL)
		goto error0;
#endif
	if (tag1 == NULL)
		tag1 = "";

	tag1_len = strlen(tag1);
	key1_len = strlen(key1);
	str_len = tag1_len + key1_len;

	if ((str = malloc(str_len + str_len + 2)) == NULL)
		goto error0;

	/* Join the tag and key into a new string. */
	TextCopy(str, tag1_len+1, tag1);
	TextCopy(str + tag1_len, key1_len+1, key1);

	memset(&k, 0, sizeof (k));
	k.data = (unsigned char *) str;
	k.size = str_len;
	TextLower((char *) k.data, k.size);

	if (reduceKey == reduceMail) {
		for (plus_sign = span = 0; span < key1_len; span++) {
			if (key1[span] == '@')
				break;
			if (key1[span] == '+')
				plus_sign = span;
		}

		if (0 < key1_len && span == key1_len) {
			if (0 < plus_sign)
				span = plus_sign;
			/* Convert unqualified address to
			 *
			 *	tag1:local-part@\0.
			 */
			str[tag1_len+span] = '@';
			str[tag1_len+span+1] = '\0';
			k.size = tag1_len + span + 1;
		} else {
			/* When key1 is an email address containing an
			 * at-sign, then for reduceMail() build a string
			 * of the form:
			 *
			 *	tag1:local@domain\0local@\0
			 */
			if (0 < plus_sign)
				span = plus_sign;

			str[str_len+span+1] = '@';
			str[str_len+span+2] = '\0';
			TextLower(memcpy(str + str_len + 1, key1, span), span);
		}
	}

	do {
		k.data[k.size] = '\0';
#ifdef TEST
		printf("size=%lu data=\"%s\"\n", k.size, k.data);
#else
		memset(&v, 0, sizeof (v));

		rc = (smdb_result) sm->fetch(sm, &k, &v);
		if (0 < smdbOptDebug.value)
			syslog(LOG_DEBUG, "map=\"%s\" key=%lu:\"%s\" value=\"%s\"", sm->_table, k.size, k.data, TextEmpty((char *) v.data));
		if (rc == SMDB_ERROR)
			break;
		if (rc == SMDB_OK) {
			if (keyp != NULL)
				*keyp = strdup((char *) k.data);
			*valuep = (char *) v.data;
			break;
		}
		free(v.data);
#endif
	} while ((*reduceKey)(tag1_len, &k));

	free(str);
error0:
	if (rc == SMDB_ERROR) {
		if (keyp != NULL)
			*keyp = tagInvalid(tag1, NULL);
		*valuep = strdup("TEMPFAIL");
	}
	return rc;
}

static smdb_result
doubleKey(smdb *sm, char **keyp, char **valuep, const char *tag1, const char *key1,  int (*reduce1)(size_t, kvm_data *), const char *tag2, const char *key2,  int (*reduce2)(size_t, kvm_data *))
{
	char *str;
	kvm_data k;
	smdb_result rc;
	size_t tag1_len, key1_len, tag2_len, str_len;

	*valuep = NULL;
	if (keyp != NULL)
		*keyp = NULL;
	rc = SMDB_ERROR;

#ifndef TEST
	if (sm == NULL || key1 == NULL || key2 == NULL)
		goto error0;
#endif
	if (tag1 == NULL)
		tag1 = "";
	if (tag2 == NULL)
		tag2 = SMDB_COMBO_TAG_DELIM;

	tag1_len = strlen(tag1);
	key1_len = strlen(key1);
	tag2_len = strlen(tag2);

	/* Make sure we have more than enough space for a string that looks
	 * like:
	 *
	 *	tag1:key1:tag2:\0
	 */
	str_len = tag1_len + key1_len + tag2_len;

	if ((str = malloc(str_len+1)) == NULL)
		goto error0;

	/* Build the initial part of the key (tag1:key1) */
	memcpy(str, tag1, tag1_len);
	memcpy(str + tag1_len, key1, key1_len);

	memset(&k, 0, sizeof (k));
	k.data = (unsigned char *) str;
	k.size = tag1_len + key1_len;

	do {
		/* Append :tag2: to tag1:key1 so that we have:
		 *
		 *	tag1:key1:tag2:\0
		 */
		memcpy(k.data + k.size, tag2, tag2_len + 1);
		k.size += tag2_len;

		if ((rc = singleKey(sm, keyp, valuep, (char *) k.data, key2, reduce2)) != SMDB_NOT_FOUND)
			break;

		/* Remove :tag2: before reducing tag1:key1. */
		k.size -= tag2_len;
		k.data[k.size] = '\0';
	} while ((*reduce1)(tag1_len, &k));

	free(str);
error0:
	if (rc == SMDB_ERROR && *valuep == NULL) {
		if (keyp != NULL)
			*keyp = tagInvalid(tag1, tag2);
		*valuep = strdup("TEMPFAIL");
	}
	return rc;
}

static smdb_code
singleKeyGetCode(smdb *sm, char **keyp, char **valuep, const char *tag1, const char *key1,  int (*reduce1)(size_t, kvm_data *))
{
	char *value;
	smdb_code code;

	if (singleKey(sm, keyp, &value, tag1, key1, reduce1) == SMDB_NOT_FOUND)
		return SMDB_ACCESS_NOT_FOUND;

	code = smdbAccessCode(value);
	if (valuep != NULL)
		*valuep = value;
	else
		free(value);

	return code;
}

static smdb_code
doubleKeyGetCode(smdb *sm, char **keyp, char **valuep, const char *tag1, const char *key1,  int (*reduce1)(size_t, kvm_data *), const char *tag2, const char *key2,  int (*reduce2)(size_t, kvm_data *))
{
	char *value;
	smdb_code code;

	if (doubleKey(sm, keyp, &value, tag1, key1, reduce1, tag2, key2, reduce2) == SMDB_NOT_FOUND)
		return SMDB_ACCESS_NOT_FOUND;

	code = smdbAccessCode(value);
	if (valuep != NULL)
		*valuep = value;
	else
		free(value);

	return code;
}

/***********************************************************************
 *** Public API for lookups and processing results.
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

	/* Look for "table!type!path" first. */
	if ((delim = strchr(file, KVM_DELIM)) != NULL && strchr(delim+1, KVM_DELIM) != NULL) {
		table = file;
		*delim++ = '\0';

		if (0 < TextInsensitiveStartsWith(delim, "read-only" KVM_DELIM_S)) {
			delim += sizeof ("read-only" KVM_DELIM_S)-1;
			mode |= KVM_MODE_READ_ONLY;
		}
	}

	/* Otherwise, we have either "type!path" or "path". Try to
	 * identify common table names, which is important for
	 * socketmap types when contacting a socketmap server that
	 * replies differently based on the table name given in
	 * the query.
	 */
	else if (strstr(file, "access.") != NULL) {
		table = "access";
	} else if (strstr(file, "mailertable.") != NULL) {
		table = "mailertable";
	} else if (strstr(file, "virtusertable.") != NULL) {
		table = "virtuser";
	} else {
		table = "unknown";
	}

	if ((sm = kvmOpen(table, (delim == NULL || *delim == KVM_DELIM) ? file : delim, mode)) == NULL)
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

/*
 * @param value
 *	An access database right-hand-side value.
 *
 * @return
 *	An SMDB_ACCESS_* code.
 */
smdb_code
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

	/* Because of smtpf defines TAG and TRAP, we cannot
	 * return 'T'. One day the smdb_code will have to
	 * be based on numerics instead of single letter
	 * mneumonics.
	 */
	case 'T':
		if (value[1] == '\0' || value[2] == '\0')
			break;

		switch (toupper(value[2])) {
		case SMDB_ACCESS_TEMPFAIL:
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
smdb_code
smdbAccessIsOk(smdb_code status)
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

	case SMDB_ACCESS_TEMPFAIL:
		return SMDB_ACCESS_TEMPFAIL;

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

smdb_result
smdbFetchValue(smdb *sm, const char *key, char **value)
{
	kvm_data k, v;
	smdb_result rc;

	if (key == NULL)
		return SMDB_NOT_FOUND;

	if (sm == NULL || value == NULL)
		return SMDB_ERROR;

	k.size = strlen(key);
	k.data = (unsigned char *) key;
	memset(&v, 0, sizeof (v));

	if ((rc = (smdb_result) sm->fetch(sm, &k, &v)) == SMDB_OK) {
		*value = (char *) v.data;
	} else {
		*value = rc == SMDB_ERROR ? strdup("TEMPFAIL") : NULL;
		free(v.data);
	}

	if (0 < smdbOptDebug.value)
		syslog(LOG_DEBUG, "map=\"%s\" key=%lu:\"%s\" value=\"%s\" rc=%d", sm->_table, k.size, key, TextEmpty(*value), rc);

	return rc;
}

char *
smdbGetValue(smdb *sm, const char *key)
{
	char *value;

	(void) smdbFetchValue(sm, key, &value);

	return  value;
}

smdb_code
smdbGetValueCode(smdb *sm, const char *key, char **valuep)
{
	char *value;
	smdb_code code;

	if ((value = smdbGetValue(sm, key)) == NULL)
		return SMDB_ACCESS_NOT_FOUND;

	code = smdbAccessCode(value);
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
smdb_code
smdbAccessIp(smdb *sm, const char *tag, const char *key, char **keyp, char **valuep)
{
	return singleKeyGetCode(sm, keyp, valuep, tag, key, reduceIp);
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
smdb_code
smdbAccessDomain(smdb *sm, const char *tag, const char *key, char **keyp, char **valuep)
{
	return singleKeyGetCode(sm, keyp, valuep, tag, key, reduceDomain);
}

/*
 * Lookup
 *
 *	tag:left+right@some.sub.domain.tld
 *	tag:some.sub.domain.tld
 *	tag:sub.domain.tld
 *	tag:domain.tld
 *	tag:tld
 *	tag:left@
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
smdb_code
smdbAccessMail(smdb *sm, const char *tag, const char *key, char **keyp, char **valuep)
{
	return singleKeyGetCode(sm, keyp, valuep, tag, key, reduceMail);
}

/*
 * Lookup order:
 *
 *	tag1:123.45.67.89:tag2:user1.account@example.com
 *	tag1:123.45.67.89:tag2:example.com
 *	tag1:123.45.67.89:tag2:com
 *	tag1:123.45.67.89:tag2:user1.account@
 *
 *	tag1:123.45.67:tag2:user1.account@example.com
 *	tag1:123.45.67:tag2:example.com
 *	tag1:123.45.67:tag2:com
 *	tag1:123.45.67:tag2:user1.account@
 *
 *	tag1:123.45:tag2:user1.account@example.com
 *	tag1:123.45:tag2:example.com
 *	tag1:123.45:tag2:com
 *	tag1:123.45:tag2:user1.account@
 *
 *	tag1:123:tag2:user1.account@example.com
 *	tag1:123:tag2:example.com
 *	tag1:123:tag2:com
 *	tag1:123:tag2:user1.account@
 *
 *	tag1:[123.45.67.89]:tag2:user1.account@example.com
 *	tag1:[123.45.67.89]:tag2:example.com
 *	tag1:[123.45.67.89]:tag2:com
 *	tag1:[123.45.67.89]:tag2:user1.account@
 *
 *	tag1:host.example.com:tag2:user1.account@example.com
 *	tag1:host.example.com:tag2:example.com
 *	tag1:host.example.com:tag2:com
 *	tag1:host.example.com:tag2:user1.account@
 *
 *	tag1:example.com:tag2:user1.account@example.com
 *	tag1:example.com:tag2:example.com
 *	tag1:example.com:tag2:com
 *	tag1:example.com:tag2:user1.account@
 *
 *	tag1:com:tag2:user1.account@example.com
 *	tag1:com:tag2:example.com
 *	tag1:com:tag2:com
 *	tag1:com:tag2:user1.account@
 *
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
smdb_code
smdbIpMail(smdb *sm, const char *tag1, const char *key1, const char *tag2, const char *key2, char **keyp, char **valuep)
{
	return doubleKeyGetCode(sm, keyp, valuep, tag1, key1, reduceIp, tag2, key2, reduceMail);
}

/*
 * Lookup order:
 *
 *	tag1:123.45.67.89:tag2:user2.account@other.com
 *	tag1:123.45.67.89:tag2:other.com
 *	tag1:123.45.67.89:tag2:com
 *	tag1:123.45.67.89:tag2:user2.account@
 *
 *	tag1:123.45.67:tag2:user2.account@other.com
 *	tag1:123.45.67:tag2:other.com
 *	tag1:123.45.67:tag2:com
 *	tag1:123.45.67:tag2:user2.account@
 *
 *	tag1:123.45:tag2:user2.account@other.com
 *	tag1:123.45:tag2:other.com
 *	tag1:123.45:tag2:com
 *	tag1:123.45:tag2:user2.account@
 *
 *	tag1:123:tag2:user2.account@other.com
 *	tag1:123:tag2:other.com
 *	tag1:123:tag2:com
 *	tag1:123:tag2:user2.account@
 *
 *	tag1:[123.45.67.89]:tag2:user2.account@other.com
 *	tag1:[123.45.67.89]:tag2:other.com
 *	tag1:[123.45.67.89]:tag2:com
 *	tag1:[123.45.67.89]:tag2:user2.account@
 *
 *	tag1:host.example.com:tag2:user2.account@other.com
 *	tag1:host.example.com:tag2:other.com
 *	tag1:host.example.com:tag2:com
 *	tag1:host.example.com:tag2:user2.account@
 *
 *	tag1:example.com:tag2:user2.account@other.com
 *	tag1:example.com:tag2:other.com
 *	tag1:example.com:tag2:com
 *	tag1:example.com:tag2:user2.account@
 *
 *	tag1:com:tag2:user2.account@other.com
 *	tag1:com:tag2:other.com
 *	tag1:com:tag2:com
 *	tag1:com:tag2:user2.account@
 *
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
smdb_code
smdbDomainMail(smdb *sm, const char *tag1, const char *key1, const char *tag2, const char *key2, char **keyp, char **valuep)
{
	return doubleKeyGetCode(sm, keyp, valuep, tag1, key1, reduceDomain, tag2, key2, reduceMail);
}

/*
 * Lookup order:
 *
 *	tag1:user1.account@host.example.com:tag2:user2.account@sub.other.com
 *	tag1:user1.account@host.example.com:tag2:sub.other.com
 *	tag1:user1.account@host.example.com:tag2:other.com
 *	tag1:user1.account@host.example.com:tag2:com
 *	tag1:user1.account@host.example.com:tag2:user2.account@
 *
 *	tag1:host.example.com:tag2:user2.account@sub.other.com
 *	tag1:host.example.com:tag2:sub.other.com
 *	tag1:host.example.com:tag2:other.com
 *	tag1:host.example.com:tag2:com
 *	tag1:host.example.com:tag2:user2.account@
 *
 *	tag1:example.com:tag2:user2.account@sub.other.com
 *	tag1:example.com:tag2:sub.other.com
 *	tag1:example.com:tag2:other.com
 *	tag1:example.com:tag2:com
 *	tag1:example.com:tag2:user2.account@
 *
 *	tag1:com:tag2:user2.account@sub.other.com
 *	tag1:com:tag2:sub.other.com
 *	tag1:com:tag2:other.com
 *	tag1:com:tag2:com
 *	tag1:com:tag2:user2.account@
 *
 *	tag1:user1.account@:tag2:user2.account@sub.other.com
 *	tag1:user1.account@:tag2:sub.other.com
 *	tag1:user1.account@:tag2:other.com
 *	tag1:user1.account@:tag2:com
 *	tag1:user1.account@:tag2:user2.account@
 *
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
smdb_code
smdbMailMail(smdb *sm, const char *tag1, const char *key1, const char *tag2, const char *key2, char **keyp, char **valuep)
{
	char *str;
	smdb_code rc = SMDB_ACCESS_NOT_FOUND, span;
	size_t tag1_len, key1_len, tag2_len, str_len;

	if (tag1 == NULL || key1 == NULL
	||  tag2 == NULL || key2 == NULL)
		goto error0;

	tag1_len = strlen(tag1);
	key1_len = strlen(key1);
	tag2_len = strlen(tag2);
	str_len = tag1_len + key1_len + tag2_len;

	if ((str = malloc(str_len+1)) == NULL)
		goto error0;

	(void) snprintf(str, str_len+1, "%s%s%s", tag1, key1, tag2);

	if ((rc = singleKeyGetCode(sm, keyp, valuep, str, key2, reduceMail)) != SMDB_ACCESS_NOT_FOUND)
		goto error1;

	span = strcspn(key1, "@");
	if (key1[span] == '@') {
		if ((rc = smdbDomainMail(sm, tag1, key1+span+1, tag2, key2, keyp, valuep)) != SMDB_ACCESS_NOT_FOUND)
			goto error1;

		span = strcspn(key1, "+@");
		str[tag1_len + span] = '@';
		memcpy(str + tag1_len + span + 1, tag2, tag2_len + 1);

		if ((rc = singleKeyGetCode(sm, keyp, valuep, str, key2, reduceMail)) != SMDB_ACCESS_NOT_FOUND)
			goto error1;
	}
error1:
	free(str);
error0:
	return rc;
}

/***********************************************************************
 ***
 ***********************************************************************/

#ifdef TEST
int
main(int argc, char **argv)
{
	printf("---- single empty keys ----\n\n");

	smdbAccessIp(NULL, "ip:", "", NULL, NULL);
	smdbAccessDomain(NULL, "domain:", "", NULL, NULL);
	smdbAccessMail(NULL, "mail:", "", NULL, NULL);

	printf("\n---- single key, no tag ----\n\n");

	smdbAccessMail(NULL, NULL, "achowe@snert.com", NULL, NULL);
	smdbAccessDomain(NULL, NULL, "snert.com", NULL, NULL);
	smdbAccessIp(NULL, NULL, "123.45.67.89", NULL, NULL);

	printf("\n---- single keys ----\n\n");

	smdbAccessIp(NULL, "connect:", "123.45.67.89", NULL, NULL);
	smdbAccessIp(NULL, "connect:", "a:b:c:d:e:f:g", NULL, NULL);
	smdbAccessIp(NULL, "connect:", "ipv6:a:b:c:d:e:f:g", NULL, NULL);
	smdbAccessIp(NULL, "connect:", "ipv6:2001:0DB8::1234:5678", NULL, NULL);
	smdbAccessIp(NULL, "connect:", "ipv6:2001:0DB8::FFFF:123.45.67.89", NULL, NULL);
	smdbAccessDomain(NULL, "connect:", "[123.45.67.89]", NULL, NULL);
	smdbAccessDomain(NULL, "connect:", "[ipv6:a:b:c:d:e:f:g]", NULL, NULL);
	smdbAccessDomain(NULL, "connect:", "sub.domain.tld", NULL, NULL);
	smdbAccessMail(NULL, "from:", "account.name+detail@sub.domain.tld", NULL, NULL);
	smdbAccessMail(NULL, "from:", "account.name+detail@[123.45.67.89]", NULL, NULL);
	smdbAccessMail(NULL, "from:", "account.name+detail@[ipv6:a:b:c:d:e:f:g]", NULL, NULL);

	printf("\n---- single keys with unqualified addresses ----\n\n");

	smdbAccessMail(NULL, "unqualified:", "account", NULL, NULL);
	smdbAccessMail(NULL, "unqualified:", "account.name", NULL, NULL);
	smdbAccessMail(NULL, "unqualified:", "account.name+detail", NULL, NULL);

	printf("\n---- double empty keys ----\n\n");

	smdbIpMail(NULL, "ip:", "", SMDB_COMBO_TAG_DELIM "ip:", "", NULL, NULL);
	smdbIpMail(NULL, "ip:", "", SMDB_COMBO_TAG_DELIM "domain:", "", NULL, NULL);
	smdbIpMail(NULL, "ip:", "", SMDB_COMBO_TAG_DELIM "mail:", "", NULL, NULL);

	smdbDomainMail(NULL, "domain:", "", SMDB_COMBO_TAG_DELIM "ip:", "", NULL, NULL);
	smdbDomainMail(NULL, "domain:", "", SMDB_COMBO_TAG_DELIM "domain:", "", NULL, NULL);
	smdbDomainMail(NULL, "domain:", "", SMDB_COMBO_TAG_DELIM "mail:", "", NULL, NULL);

	smdbMailMail(NULL, "mail:", "", SMDB_COMBO_TAG_DELIM "ip:", "", NULL, NULL);
	smdbMailMail(NULL, "mail:", "", SMDB_COMBO_TAG_DELIM "domain:", "", NULL, NULL);
	smdbMailMail(NULL, "mail:", "", SMDB_COMBO_TAG_DELIM "mail:", "", NULL, NULL);

	printf("\n---- double keys ----\n\n");

	smdbIpMail(NULL, "connect:", "123.45.67.89", SMDB_COMBO_TAG_DELIM "to:", "account.name+detail@sub.domain.tld", NULL, NULL);
	smdbIpMail(NULL, "connect:", "123.45.67.89", SMDB_COMBO_TAG_DELIM "to:", "50-235410152-fsg.com?sgsg@in137.bestintuition.com", NULL, NULL);
	smdbIpMail(NULL, "connect:", "a:b:c:d:e:f:g", SMDB_COMBO_TAG_DELIM "to:", "account.name+detail@sub.domain.tld", NULL, NULL);
	smdbIpMail(NULL, "connect:", "ipv6:a:b:c:d:e:f:g", SMDB_COMBO_TAG_DELIM "to:", "account.name+detail@sub.domain.tld", NULL, NULL);
	smdbIpMail(NULL, "connect:", "ipv6:2001:0DB8::1234:5678", SMDB_COMBO_TAG_DELIM "to:", "account.name+detail@sub.domain.tld", NULL, NULL);
	smdbIpMail(NULL, "connect:", "ipv6:2001:0DB8::FFFF:123.45.67.89", SMDB_COMBO_TAG_DELIM "from:", "account.name@sub.domain.tld", NULL, NULL);
	smdbDomainMail(NULL, "connect:", "[123.45.67.89]", SMDB_COMBO_TAG_DELIM "to:", "account.name+detail@sub.domain.tld", NULL, NULL);
	smdbDomainMail(NULL, "connect:", "[ipv6:a:b:c:d:e:f:g]", SMDB_COMBO_TAG_DELIM "to:", "account.name+detail@sub.domain.tld", NULL, NULL);
	smdbDomainMail(NULL, "connect:", "sub.domain.tld", SMDB_COMBO_TAG_DELIM "to:", "account.name+detail@sub.domain.tld", NULL, NULL);
	smdbMailMail(NULL, "from:", "account.name+detail@sub.domain.tld", SMDB_COMBO_TAG_DELIM "to:", "account.name+detail@sub.domain.tld", NULL, NULL);
	smdbMailMail(NULL, "from:", "118599134653498-17080700031-fsg.com?asdf@bounce.superode.com", SMDB_COMBO_TAG_DELIM "to:", "asdf@fsg.com", NULL, NULL);
	smdbMailMail(NULL, "from:", "account.name+detail@[123.45.67.89]", SMDB_COMBO_TAG_DELIM "to:", "account.name+detail@sub.domain.tld", NULL, NULL);
	smdbMailMail(NULL, "from:", "account.name+detail@[ipv6:a:b:c:d:e:f:g]", SMDB_COMBO_TAG_DELIM "to:", "account.name+detail@sub.domain.tld", NULL, NULL);

	printf("\n---- double keys with unqualified addresses ----\n\n");

	smdbIpMail(NULL, "connect:", "123.45.67.89", SMDB_COMBO_TAG_DELIM "to:", "account.name+detail", NULL, NULL);
	smdbIpMail(NULL, "connect:", "a:b:c:d:e:f:g", SMDB_COMBO_TAG_DELIM "to:", "account.name+detail", NULL, NULL);
	smdbIpMail(NULL, "connect:", "ipv6:a:b:c:d:e:f:g", SMDB_COMBO_TAG_DELIM "to:", "account.name+detail", NULL, NULL);
	smdbIpMail(NULL, "connect:", "ipv6:2001:0DB8::1234:5678", SMDB_COMBO_TAG_DELIM "to:", "account.name+detail", NULL, NULL);
	smdbIpMail(NULL, "connect:", "ipv6:2001:0DB8::FFFF:123.45.67.89", SMDB_COMBO_TAG_DELIM "from:", "account.name", NULL, NULL);
	smdbDomainMail(NULL, "connect:", "[123.45.67.89]", SMDB_COMBO_TAG_DELIM "to:", "account.name+detail", NULL, NULL);
	smdbDomainMail(NULL, "connect:", "[ipv6:a:b:c:d:e:f:g]", SMDB_COMBO_TAG_DELIM "to:", "account.name+detail", NULL, NULL);
	smdbDomainMail(NULL, "connect:", "sub.domain.tld", SMDB_COMBO_TAG_DELIM "to:", "account.name+detail", NULL, NULL);
	smdbMailMail(NULL, "from:", "account.name+detail@sub.domain.tld", SMDB_COMBO_TAG_DELIM "to:", "account.name+detail", NULL, NULL);
	smdbMailMail(NULL, "from:", "account.name+detail@[123.45.67.89]", SMDB_COMBO_TAG_DELIM "to:", "account.name+detail", NULL, NULL);
	smdbMailMail(NULL, "from:", "account.name+detail@[ipv6:a:b:c:d:e:f:g]", SMDB_COMBO_TAG_DELIM "to:", "account.name+detail", NULL, NULL);
	smdbMailMail(NULL, "from:", "account.name+detail", SMDB_COMBO_TAG_DELIM "to:", "account.name+detail", NULL, NULL);

	printf("\n---- double keys (host1:host2:) ----\n\n");

	doubleKey(NULL, NULL, NULL, "host1:", "123.45.67.89", reduceIp, SMDB_COMBO_TAG_DELIM "host2:", "192.0.2.1", reduceIp);
	doubleKey(NULL, NULL, NULL, "host1:", "123.45.67.89", reduceIp, SMDB_COMBO_TAG_DELIM "host2:", "a:b:c:d:e:f:g", reduceIp);
	doubleKey(NULL, NULL, NULL, "host1:", "123.45.67.89", reduceIp, SMDB_COMBO_TAG_DELIM "host2:", "ipv6:a:b:c:d:e:f:g", reduceIp);
	doubleKey(NULL, NULL, NULL, "host1:", "123.45.67.89", reduceIp, SMDB_COMBO_TAG_DELIM "host2:", "ipv6:2001:0DB8::1234:5678", reduceIp);
	doubleKey(NULL, NULL, NULL, "host1:", "123.45.67.89", reduceIp, SMDB_COMBO_TAG_DELIM "host2:", "ipv6:2001:0DB8::FFFF:123.45.67.89", reduceIp);
	doubleKey(NULL, NULL, NULL, "host1:", "123.45.67.89", reduceIp, SMDB_COMBO_TAG_DELIM "host2:", "[123.45.67.89]", reduceDomain);
	doubleKey(NULL, NULL, NULL, "host1:", "123.45.67.89", reduceIp, SMDB_COMBO_TAG_DELIM "host2:", "[ipv6:a:b:c:d:e:f:g]", reduceDomain);
	doubleKey(NULL, NULL, NULL, "host1:", "123.45.67.89", reduceIp, SMDB_COMBO_TAG_DELIM "host2:", "sub.domain.tld", reduceDomain);

	doubleKey(NULL, NULL, NULL, "host1:", "a:b:c:d:e:f:g", reduceIp, SMDB_COMBO_TAG_DELIM "host2:", "192.0.2.1", reduceIp);
	doubleKey(NULL, NULL, NULL, "host1:", "a:b:c:d:e:f:g", reduceIp, SMDB_COMBO_TAG_DELIM "host2:", "a:b:c:d:e:f:g", reduceIp);
	doubleKey(NULL, NULL, NULL, "host1:", "a:b:c:d:e:f:g", reduceIp, SMDB_COMBO_TAG_DELIM "host2:", "ipv6:a:b:c:d:e:f:g", reduceIp);
	doubleKey(NULL, NULL, NULL, "host1:", "a:b:c:d:e:f:g", reduceIp, SMDB_COMBO_TAG_DELIM "host2:", "ipv6:2001:0DB8::1234:5678", reduceIp);
	doubleKey(NULL, NULL, NULL, "host1:", "a:b:c:d:e:f:g", reduceIp, SMDB_COMBO_TAG_DELIM "host2:", "ipv6:2001:0DB8::FFFF:123.45.67.89", reduceIp);
	doubleKey(NULL, NULL, NULL, "host1:", "a:b:c:d:e:f:g", reduceIp, SMDB_COMBO_TAG_DELIM "host2:", "[123.45.67.89]", reduceDomain);
	doubleKey(NULL, NULL, NULL, "host1:", "a:b:c:d:e:f:g", reduceIp, SMDB_COMBO_TAG_DELIM "host2:", "[ipv6:a:b:c:d:e:f:g]", reduceDomain);
	doubleKey(NULL, NULL, NULL, "host1:", "a:b:c:d:e:f:g", reduceIp, SMDB_COMBO_TAG_DELIM "host2:", "sub.domain.tld", reduceDomain);

	doubleKey(NULL, NULL, NULL, "host1:", "ipv6:a:b:c:d:e:f:g", reduceIp, SMDB_COMBO_TAG_DELIM "host2:", "192.0.2.1", reduceIp);
	doubleKey(NULL, NULL, NULL, "host1:", "ipv6:a:b:c:d:e:f:g", reduceIp, SMDB_COMBO_TAG_DELIM "host2:", "a:b:c:d:e:f:g", reduceIp);
	doubleKey(NULL, NULL, NULL, "host1:", "ipv6:a:b:c:d:e:f:g", reduceIp, SMDB_COMBO_TAG_DELIM "host2:", "ipv6:a:b:c:d:e:f:g", reduceIp);
	doubleKey(NULL, NULL, NULL, "host1:", "ipv6:a:b:c:d:e:f:g", reduceIp, SMDB_COMBO_TAG_DELIM "host2:", "ipv6:2001:0DB8::1234:5678", reduceIp);
	doubleKey(NULL, NULL, NULL, "host1:", "ipv6:a:b:c:d:e:f:g", reduceIp, SMDB_COMBO_TAG_DELIM "host2:", "ipv6:2001:0DB8::FFFF:123.45.67.89", reduceIp);
	doubleKey(NULL, NULL, NULL, "host1:", "ipv6:a:b:c:d:e:f:g", reduceIp, SMDB_COMBO_TAG_DELIM "host2:", "[123.45.67.89]", reduceDomain);
	doubleKey(NULL, NULL, NULL, "host1:", "ipv6:a:b:c:d:e:f:g", reduceIp, SMDB_COMBO_TAG_DELIM "host2:", "[ipv6:a:b:c:d:e:f:g]", reduceDomain);
	doubleKey(NULL, NULL, NULL, "host1:", "ipv6:a:b:c:d:e:f:g", reduceIp, SMDB_COMBO_TAG_DELIM "host2:", "sub.domain.tld", reduceDomain);

	doubleKey(NULL, NULL, NULL, "host1:", "[123.45.67.89]", reduceDomain, SMDB_COMBO_TAG_DELIM "host2:", "192.0.2.1", reduceIp);
	doubleKey(NULL, NULL, NULL, "host1:", "[123.45.67.89]", reduceDomain, SMDB_COMBO_TAG_DELIM "host2:", "a:b:c:d:e:f:g", reduceIp);
	doubleKey(NULL, NULL, NULL, "host1:", "[123.45.67.89]", reduceDomain, SMDB_COMBO_TAG_DELIM "host2:", "ipv6:a:b:c:d:e:f:g", reduceIp);
	doubleKey(NULL, NULL, NULL, "host1:", "[123.45.67.89]", reduceDomain, SMDB_COMBO_TAG_DELIM "host2:", "ipv6:2001:0DB8::1234:5678", reduceIp);
	doubleKey(NULL, NULL, NULL, "host1:", "[123.45.67.89]", reduceDomain, SMDB_COMBO_TAG_DELIM "host2:", "ipv6:2001:0DB8::FFFF:123.45.67.89", reduceIp);
	doubleKey(NULL, NULL, NULL, "host1:", "[123.45.67.89]", reduceDomain, SMDB_COMBO_TAG_DELIM "host2:", "[123.45.67.89]", reduceDomain);
	doubleKey(NULL, NULL, NULL, "host1:", "[123.45.67.89]", reduceDomain, SMDB_COMBO_TAG_DELIM "host2:", "[ipv6:a:b:c:d:e:f:g]", reduceDomain);
	doubleKey(NULL, NULL, NULL, "host1:", "[123.45.67.89]", reduceDomain, SMDB_COMBO_TAG_DELIM "host2:", "sub.domain.tld", reduceDomain);

	doubleKey(NULL, NULL, NULL, "host1:", "[ipv6:a:b:c:d:e:f:g]", reduceDomain, SMDB_COMBO_TAG_DELIM "host2:", "192.0.2.1", reduceIp);
	doubleKey(NULL, NULL, NULL, "host1:", "[ipv6:a:b:c:d:e:f:g]", reduceDomain, SMDB_COMBO_TAG_DELIM "host2:", "a:b:c:d:e:f:g", reduceIp);
	doubleKey(NULL, NULL, NULL, "host1:", "[ipv6:a:b:c:d:e:f:g]", reduceDomain, SMDB_COMBO_TAG_DELIM "host2:", "ipv6:a:b:c:d:e:f:g", reduceIp);
	doubleKey(NULL, NULL, NULL, "host1:", "[ipv6:a:b:c:d:e:f:g]", reduceDomain, SMDB_COMBO_TAG_DELIM "host2:", "ipv6:2001:0DB8::1234:5678", reduceIp);
	doubleKey(NULL, NULL, NULL, "host1:", "[ipv6:a:b:c:d:e:f:g]", reduceDomain, SMDB_COMBO_TAG_DELIM "host2:", "ipv6:2001:0DB8::FFFF:123.45.67.89", reduceIp);
	doubleKey(NULL, NULL, NULL, "host1:", "[ipv6:a:b:c:d:e:f:g]", reduceDomain, SMDB_COMBO_TAG_DELIM "host2:", "[123.45.67.89]", reduceDomain);
	doubleKey(NULL, NULL, NULL, "host1:", "[ipv6:a:b:c:d:e:f:g]", reduceDomain, SMDB_COMBO_TAG_DELIM "host2:", "[ipv6:a:b:c:d:e:f:g]", reduceDomain);
	doubleKey(NULL, NULL, NULL, "host1:", "[ipv6:a:b:c:d:e:f:g]", reduceDomain, SMDB_COMBO_TAG_DELIM "host2:", "sub.domain.tld", reduceDomain);

	doubleKey(NULL, NULL, NULL, "host1:", "sub.domain.tld", reduceDomain, SMDB_COMBO_TAG_DELIM "host2:", "192.0.2.1", reduceIp);
	doubleKey(NULL, NULL, NULL, "host1:", "sub.domain.tld", reduceDomain, SMDB_COMBO_TAG_DELIM "host2:", "a:b:c:d:e:f:g", reduceIp);
	doubleKey(NULL, NULL, NULL, "host1:", "sub.domain.tld", reduceDomain, SMDB_COMBO_TAG_DELIM "host2:", "ipv6:a:b:c:d:e:f:g", reduceIp);
	doubleKey(NULL, NULL, NULL, "host1:", "sub.domain.tld", reduceDomain, SMDB_COMBO_TAG_DELIM "host2:", "ipv6:2001:0DB8::1234:5678", reduceIp);
	doubleKey(NULL, NULL, NULL, "host1:", "sub.domain.tld", reduceDomain, SMDB_COMBO_TAG_DELIM "host2:", "ipv6:2001:0DB8::FFFF:123.45.67.89", reduceIp);
	doubleKey(NULL, NULL, NULL, "host1:", "sub.domain.tld", reduceDomain, SMDB_COMBO_TAG_DELIM "host2:", "[123.45.67.89]", reduceDomain);
	doubleKey(NULL, NULL, NULL, "host1:", "sub.domain.tld", reduceDomain, SMDB_COMBO_TAG_DELIM "host2:", "[ipv6:a:b:c:d:e:f:g]", reduceDomain);
	doubleKey(NULL, NULL, NULL, "host1:", "sub.domain.tld", reduceDomain, SMDB_COMBO_TAG_DELIM "host2:", "sub.domain.tld", reduceDomain);

	return 0;
}
#endif
