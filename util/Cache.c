/*
 * Cache.h
 *
 * Cache API
 *
 * An object that maps keys to values. A Cache cannot contain
 * duplicate keys; each non-null key can map to at most one non-null
 * value. Similar to Java's abstract Dictionary class.
 *
 * Copyright 2001, 2006 by Anthony Howe.  All rights reserved.
 */

#include <com/snert/lib/version.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <com/snert/lib/io/posix.h>
#include <com/snert/lib/type/Data.h>
#include <com/snert/lib/type/Hash.h>
#include <com/snert/lib/util/Cache.h>
#include <com/snert/lib/util/Properties.h>
#include <com/snert/lib/util/Text.h>

#if defined(HAVE_SYSLOG_H) && ! defined(__MINGW32__)
#  include <syslog.h>
#else
#  include <com/snert/lib/io/Log.h>
#endif

#ifdef DEBUG_MALLOC
# include <com/snert/lib/util/DebugMalloc.h>
#endif

#define REF_CACHE(v)		((Cache)(v))

/***********************************************************************
 *** Class variables.
 ***********************************************************************/

static int debug = 0;

/***********************************************************************
 *** Hash instance methods
 ***********************************************************************/

static void
CacheHashDestroy(void *self)
{
	HashDestroy(REF_CACHE(self)->_cache);
	free(REF_CACHE(self)->_name);
	free(self);
}

static Data
CacheHashGet(Cache self, Data key)
{
	Object value = HashGet(self->_cache, key);
	if (value == NULL)
		return NULL;
	return value->clone(value);
}

static long
CacheHashSize(Cache self)
{
	return HashSize(self->_cache);
}

static int
CacheHashIsEmpty(Cache self)
{
	return CacheHashSize(self) == 0;
}

static int
CacheHashPut(Cache self, Data key, Data value)
{
	if ((key = key->clone(key)) == NULL)
		goto error0;

	if ((value = value->clone(value)) == NULL)
		goto error1;

	if (HashPut(self->_cache, key, value))
		goto error2;

	return 0;
error2:
	value->destroy(value);
error1:
	key->destroy(key);
error0:
	return -1;
}

static int
CacheHashRemove(Cache self, Data key)
{
	return HashRemove(self->_cache, key);
}

static int
CacheHashRemoveAll(Cache self)
{
	HashRemoveAll(self->_cache);
	return 0;
}

static int
CacheHashSync(Cache self)
{
	/* Do nothing. */
	return 0;
}

static int
CacheHashWalk(Cache self, int (*function)(void *key, void *value, void *data), void *data)
{
	return HashWalk(self->_cache, function, data);
}

/***********************************************************************
 *** Properties instance methods
 ***********************************************************************/

static Data
CachePropGet(Cache self, Data key)
{
	Data value = PropertiesGetData(self->_cache, key);
	if (value == NULL)
		return NULL;
	return value->clone(value);
}

static long
CachePropSize(Cache self)
{
	return PropertiesSize(self->_cache);
}

static int
CachePropIsEmpty(Cache self)
{
	return PropertiesSize(self->_cache) == 0;
}

static int
CachePropPut(Cache self, Data key, Data value)
{
	return PropertiesSetData(self->_cache, key, value);
}

static int
CachePropRemove(Cache self, Data key)
{
	return PropertiesRemoveData(self->_cache, key);
}

static int
CachePropRemoveAll(Cache self)
{
	PropertiesRemoveAll(self->_cache);
	return 0;
}

static int
CachePropSync(Cache self)
{
	return PropertiesSave(self->_cache, self->_name);
}

static int
CachePropWalk(Cache self, int (*function)(void *key, void *value, void *data), void *data)
{
	return PropertiesWalk(self->_cache, function, data);
}

static void
CachePropDestroy(void *self)
{
	(void) CachePropSync(self);
	PropertiesDestroy(REF_CACHE(self)->_cache);
	free(REF_CACHE(self)->_name);
	free(self);
}

/***********************************************************************
 *** Berkeley DB instance methods
 ***********************************************************************/

#include <com/snert/lib/berkeley_db.h>

#if defined(HAVE_DB_H)
# define DEFAULT_HANDLER	"bdb"
#else
# define DEFAULT_HANDLER	"flatfile"
#endif

#ifdef HAVE_DB_H

#ifdef ENABLE_FILE_LOCKING
static int
CacheBdbLock(Cache self, int mode)
{
	mode = mode == 0 ? LOCK_SH : LOCK_EX;

	/* Wait until we get the file lock. */
	do
		errno = 0;
	while (flock(self->_lockfd, mode) && errno == EINTR);

	return -(errno != 0);
}

static int
CacheBdbUnlock(Cache self)
{
	return flock(self->_lockfd, LOCK_UN);
}

#else
# define CacheBdbLock(c, l)
# define CacheBdbUnlock(c)
#endif

static int
CacheBdbOpen(Cache self, const char *name)
{
#if DB_VERSION_MAJOR == 1
# ifdef IGNORE_CORRUPT_CACHE_ISSUE_WITH_DB_185
	DB *db;

	if ((db = dbopen(name, O_CREAT|O_RDWR, 0660, DB_HASH, NULL)) == (DB *) 0) {
		syslog(LOG_ERR, "failed to open \"%s\": %s (%d)", name, strerror(errno), errno);
		return -1;
	}

	if ((self->_lockfd = db->fd(db)) == -1) {
		syslog(LOG_ERR, "get lock fd error \"%s\": %s (%d)", name, strerror(errno), errno);
		db->close(db);
		return -1;
	}

	self->_cache = db;
# else
	syslog(LOG_ERR, "LibSnert Cache API no longer supports Berkeley DB 1.85");
	return -1;
# endif
#else
{
	int rc;
	DB *db;

#ifdef THIS_CODE_PROBLEMATIC
{
	rc = db_create(&db, NULL, 0);
	if (rc != 0) {
		syslog(LOG_ERR, "db_create() error: %s", db_strerror(rc));
		return -1;
	}

	struct stat sb;
	if (stat(name, &sb) == 0) {
		rc = db->verify(db, name, NULL, NULL, 0);
		if (rc == DB_VERIFY_BAD) {
			syslog(LOG_ERR, "database \"%s\" corrupted, starting from zero", name);
			if (unlink(name)) {
				syslog(LOG_ERR, "failed to remove \"%s\"", name);
				return -1;
			}
		}

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
# if DB_VERSION_MAJOR < 4 || DB_VERSION_MINOR < 2
		(void) db->close(db, 0);
# endif
	}
}
#endif
	rc = db_create(&db, (DB_ENV *) 0, 0);
	if (rc != 0) {
		syslog(LOG_ERR, "db_create() error: %s", db_strerror(rc));
		return -1;
	}

	/*
	 * Specify the hash bucket density. The suggested rule given by the
	 * Berkely DB documentation is:
	 *
	 * 		pagesize - 32
	 *	---------------------------------------- = density
	 *	average_key_size + average_data_size + 8
	 *
	 * So pagesize is typically the disk block size, which for Linux
	 * appears to be 4096 bytes. The average_data_size is sizeof CacheEntry
	 * and average_key_size is the average email address length, which we'll
	 * guess is 50 bytes.  So...
	 *
	 *	(4096 - 32) / (50 + 8 + 8) = 61.57...
	 *
	 * This seems rather conservative. Now from observation of db_stat
	 * snapshot from 3 different mail servers (slow, moderate, heavy)
	 * running at least a week:
	 *
	 * 			keys / buckets	= keys_per_bucket (density)
	 *
	 *	slow: 		2526 / 25	= 101,04
	 *	moderate:	49357 / 432	= 114,2523148...
	 *	heavy:		204599 / 2500	= 81,8396
	 *
	 * So reworking the above rule to get average_key_size:
	 *
	 * 	(pagesize - 32) - (average_data_size + 8) * keys_per_bucket
	 *	----------------------------------------------------------- = average_key_size
	 *			keys_per_bucket
	 *
	 *	slow:		24,237623762376237623762376237624
	 *	moderate:	19,649122807017543859649122807018
	 *	heavy:		34,172839506172839506172839506173
	 *
	 * Now if we consider an average_key_size of 38, that is slightly larger
	 * than the value from the heavy use mail server, then we get a density of:
	 *
	 *	75,259259259259259259259259259259
	 *
	 * This looks good to me.
	 */
	(void) db->set_h_ffactor(db, 75);

	rc = db->open(db, DBTXN_ROAR name, NULL, DB_HASH, DB_CREATE|DB_NOMMAP, 0);
	if (rc != 0) {
		syslog(LOG_ERR, "failed to create or open \"%s\": %s", name, db_strerror(rc));
		return -1;
	}

	if (db->fd(db, &self->_lockfd)) {
		syslog(LOG_ERR, "get lock fd error \"%s\": %s", name, db_strerror(rc));
		(void) db->close(db, 0);
		return -1;;
	}

	self->_cache = db;
}
#endif
	return 0;
}

static void
CacheBdbDestroy(void *selfless)
{
	Cache self = (Cache) selfless;

	if (self != NULL) {
		DB *db = (DB *) self->_cache;
		if (db != NULL)
#if DB_VERSION_MAJOR == 1
			db->close(db);
#else
			db->close(db, 0);
#endif
		free(self->_name);
		free(self);
	}
}

static Data
CacheBdbGet(Cache self, Data key)
{
	Data value = NULL;
	DBT dkey, dvalue;
	DB *db = (DB *) self->_cache;

	if (db == NULL || key == NULL) {
		errno = EFAULT;
		goto error0;
	}

	memset(&dkey, 0, sizeof (dkey));
	memset(&dvalue, 0, sizeof (dvalue));

	dkey.data = key->base(key);
	dkey.size = key->length(key);

	CacheBdbLock(self, 0);

#if DB_VERSION_MAJOR == 1
	if (db->get(db, &dkey, &dvalue, 0) != 0)
		goto error1;

	/* Because BDB 1.85 manages the memory assignment of returned
	 * values, we have to create Data object which is a copy of
	 * the value returned, as it will change on the next call.
	 */
	if ((value = DataCreateCopyBytes(dvalue.data, dvalue.size)) == NULL)
		goto error1;
#else
	dvalue.flags = DB_DBT_MALLOC;

	if (db->get(db, NULL, &dkey, &dvalue, 0) != 0)
		goto error1;

	/* BDB 3.2 or better can be told to allocate the memory for us,
	 * but it then becomes our responsiblity, so here we create a
	 * Data object and simply assign the value return from db->get().
	 * When we destroy the Data object, it will be released.
	 */
	if ((value = DataCreateWithBytes(dvalue.data, dvalue.size)) == NULL)
		free(dvalue.data);
#endif
error1:
	CacheBdbUnlock(self);
error0:
	return value;
}

static int
CacheBdbPut(Cache self, Data key, Data value)
{
	int rc = -1;
	DBT dkey, dvalue;
	DB *db = (DB *) self->_cache;

	if (db == NULL || key == NULL || value == NULL) {
		errno = EFAULT;
		goto error0;
	}

	memset(&dkey, 0, sizeof (dkey));
	memset(&dvalue, 0, sizeof (dvalue));

	dkey.data = key->base(key);
	dkey.size = key->length(key);

	dvalue.data = value->base(value);
	dvalue.size = value->length(value);

	CacheBdbLock(self, 1);

#if DB_VERSION_MAJOR == 1
	if (db->put(db, &dkey, &dvalue, 0) != 0)
		goto error1;
#else
	if (db->put(db, (DB_TXN *) 0, &dkey, &dvalue, 0) != 0)
		goto error1;
#endif

#ifdef ALWAYS_SYNC
	/* We must ALWAYS sync the database to disk, because there
	 * is no clean way to shutdown a milter (quickly) such that
	 * the atexit() handler is called in order to properly close
	 * the database. The alternative is to use Berkely DB
	 * transactions only available in 4.1.
	 */
	rc = db->sync(db, 0);
#else
	rc = 0;
#endif
error1:
	CacheBdbUnlock(self);
error0:
	return rc;
}

static int
CacheBdbRemove(Cache self, Data key)
{
	int rc = -1;
	DBT dkey;
	DB *db = (DB *) self->_cache;

	if (db == NULL || key == NULL) {
		errno = EFAULT;
		goto error0;
	}

	memset(&dkey, 0, sizeof (dkey));

	dkey.data = key->base(key);
	dkey.size = key->length(key);

	CacheBdbLock(self, 1);

#if DB_VERSION_MAJOR == 1
	if (db->del(db, &dkey, 0) != 0)
		goto error1;
#else
	if (db->del(db, NULL, &dkey, 0) != 0)
		goto error1;
#endif
	rc = 0;
error1:
	CacheBdbUnlock(self);
error0:
	return rc;
}

static int
CacheBdbSync(Cache self)
{
	int rc;
	DB *db = (DB *) self->_cache;

	if (db == NULL) {
		errno = EFAULT;
		return -1;
	}

	CacheBdbLock(self, 1);
	rc = db->sync(db, 0);
	CacheBdbUnlock(self);

	return rc;
}

static int
CacheBdbWalk(Cache self, int (*function)(void *key, void *value, void *data), void *data)
{
	int rc = -1;
	DBT dkey, dvalue;
	struct data key, value;
	DB *db = (DB *) self->_cache;
#if DB_VERSION_MAJOR == 1
	unsigned next;
#else
	DBC *cursor;
#endif

	if (db == NULL || function == NULL) {
		errno = EFAULT;
		goto error0;
	}

	DataInit(&key);
	DataInit(&value);

	memset(&dkey, 0, sizeof (dkey));
	memset(&dvalue, 0, sizeof (dvalue));

#ifndef NDEBUG
	if (debug)
		syslog(LOG_DEBUG, "CacheBdbWalk(): get first cursor entry");
#endif
#if DB_VERSION_MAJOR == 1
	next = R_FIRST;
	while (db->seq(db, &dkey, &dvalue, next) == 0) {
		next = R_NEXT;
#else
	dkey.flags = DB_DBT_REALLOC;
	dvalue.flags = DB_DBT_REALLOC;

{
	int err;
	if ((err = db->cursor(db, NULL, &cursor, 0)) != 0) {
		syslog(LOG_ERR, "CacheBdbWalk(): failed to setup cursor: %s", db_strerror(err));
		goto error0;
	}
}
	CacheBdbLock(self, 1);

	while (cursor->c_get(cursor, &dkey, &dvalue, DB_NEXT) == 0) {
#endif
		if (dkey.data == NULL) {
			syslog(LOG_ERR, "CacheBdbWalk(): c_get returned a NULL key");
			goto error1;
		}

		/* Convert from DBT to Data object. */
		key._base = dkey.data;
		key._length = dkey.size;

		if (dvalue.data == NULL) {
			syslog(LOG_ERR, "CacheBdbWalk(): c_get returned a NULL value");
			goto error1;
		}

		/* Convert from DBT to Data object. */
		value._base = dvalue.data;
		value._length = dvalue.size;

#ifndef NDEBUG
		if (debug)
			syslog(LOG_DEBUG, "CacheBdbWalk(): call-back walk function");
#endif
		switch ((*function)(&key, &value, data)) {
		case 0:
#ifndef NDEBUG
			if (debug)
				syslog(LOG_DEBUG, "CacheBdbWalk(): stop walking");
#endif
			goto stop;
		case -1:
#ifndef NDEBUG
			if (debug)
				syslog(LOG_DEBUG, "CacheBdbWalk(): delete entry at cursor");
#endif
#if DB_VERSION_MAJOR == 1
# ifdef TRIED_THIS_ALREADY
			if (db->del(db, &dkey, 0) != 0)
# else
			if (db->del(db, NULL, R_CURSOR) != 0)
# endif
#else
			if (cursor->c_del(cursor, 0) != 0)
#endif
				goto error1;
			break;
		}
#ifndef NDEBUG
		if (debug)
			syslog(LOG_DEBUG, "CacheBdbWalk(): get next cursor entry");
#endif
	}
stop:
	rc = 0;
error1:
	CacheBdbUnlock(self);

#if DB_VERSION_MAJOR > 1
	free(dkey.data);
	free(dvalue.data);
	(void) cursor->c_close(cursor);
#endif
error0:
	return rc;
}

static int
CacheBdbRemoveAnyEntry(void *key, void *value, void *data)
{
	return -1;
}

static int
CacheBdbRemoveAll(Cache self)
{
	return CacheBdbWalk(self, CacheBdbRemoveAnyEntry, NULL);
}

static int
CacheBdbCount(void *key, void *value, void *data)
{
	if (key != NULL && data != NULL)
		(*(long *) data)++;
	return 1;
}

static long
CacheBdbSize(Cache self)
{
	long count = 0;

	(void) CacheBdbWalk(self, CacheBdbCount, &count);

	return count;
}

static int
CacheBdbIsEmpty(Cache self)
{
	return CacheBdbSize(self) == 0;
}

#endif /* HAVE_DB_H */

/***********************************************************************
 *** Class methods
 ***********************************************************************/

void
CacheSetDebug(int flag)
{
	debug = flag;
}

Cache
CacheCreate(const char *handler, const char *name)
{
	Cache cache;

	if (handler == NULL || *handler == '\0')
		goto error0;

	if ((cache = malloc(sizeof (*cache))) == NULL)
		goto error0;

	if ((cache->_name = strdup(name)) == NULL)
		goto error1;

	ObjectInit(cache);
	cache->objectName = "Cache";
	cache->objectMethodCount += 9;
	cache->objectSize = sizeof (*cache);
	cache->setDebug = CacheSetDebug;

	if (TextInsensitiveCompare(handler, "bdb") == 0) {
#if defined(HAVE_DB_H)
		CacheBdbOpen(cache, name);

		cache->destroy = CacheBdbDestroy;
		cache->get = CacheBdbGet;
		cache->isEmpty = CacheBdbIsEmpty;
		cache->put = CacheBdbPut;
		cache->remove = CacheBdbRemove;
		cache->removeAll = CacheBdbRemoveAll;
		cache->size = CacheBdbSize;
		cache->sync = CacheBdbSync;
		cache->walk = CacheBdbWalk;
#else
		goto error2;
#endif
	} else if (TextInsensitiveCompare(handler, "flatfile") == 0) {
		cache->_cache = PropertiesCreate();

		if (PropertiesLoad(cache->_cache, name)) {
			FILE *fp;

			if (errno != ENOENT)
				goto error3;

			if ((fp = fopen(name, "a")) == NULL)
				goto error3;

			(void) fclose(fp);
		}

		cache->destroy = CachePropDestroy;
		cache->get = CachePropGet;
		cache->isEmpty = CachePropIsEmpty;
		cache->put = CachePropPut;
		cache->remove = CachePropRemove;
		cache->removeAll = CachePropRemoveAll;
		cache->size = CachePropSize;
		cache->sync = CachePropSync;
		cache->walk = CachePropWalk;
	} else if (TextInsensitiveCompare(handler, "hash") == 0) {
		cache->_cache = HashCreate();

		cache->destroy = CacheHashDestroy;
		cache->get = CacheHashGet;
		cache->isEmpty = CacheHashIsEmpty;
		cache->put = CacheHashPut;
		cache->remove = CacheHashRemove;
		cache->removeAll = CacheHashRemoveAll;
		cache->size = CacheHashSize;
		cache->sync = CacheHashSync;
		cache->walk = CacheHashWalk;
	}

	if (cache->_cache == NULL)
		goto error2;

	return cache;
error3:
	cache->destroy(cache);
error2:
	free(cache->_name);
error1:
	free(cache);
error0:
	return NULL;
}


/***********************************************************************
 *** Test
 ***********************************************************************/

#ifdef TEST

#include <stdio.h>
#include <com/snert/lib/version.h>

#ifdef USE_DEBUG_MALLOC
# define WITHOUT_SYSLOG			1
# include <com/snert/lib/io/Log.h>
# include <com/snert/lib/util/DebugMalloc.h>
#endif

void
isNotNull(void *ptr)
{
	if (ptr == NULL) {
		printf("NULL\n");
		exit(1);
	}

	printf("OK\n");
}

int
printKeyValue(void *key, void *value, void *data)
{
	(*(long *) data)++;
	printf("count=%ld %s=%s\n", *(long *) data, ((Data) key)->_base, ((Data) value)->_base);

	return 1;
}

void
TestCache(Cache cache)
{
	long count;
	Data k, v, x;
	char key[40], value[40];

	printf("size=%ld\n", count = cache->size(cache));

	snprintf(key, sizeof (key), "key%ld", count);
	printf("new key=%s...", key);
	isNotNull(k = DataCreateCopyString(key));

	snprintf(value, sizeof (value), "value%ld", count);
	printf("new value=%s...", value);
	isNotNull(v = DataCreateCopyString(value));

	printf("put...%s\n", cache->put(cache, k, v) ? "FAIL" : "OK");

	printf("get...");
	isNotNull(x = cache->get(cache, k));
	printf("x->length=%ld v->length=%ld\n", x->length(x), v->length(v));
	printf("equals...%s\n", x->equals(x, v) ? "OK" : "FAIL");

	k->destroy(k);
	v->destroy(v);

	count = 0;
	printf("walk...\n");
	cache->walk(cache, printKeyValue, &count);
	printf("count equals size...%s\n", count != cache->size(cache) ? "FAIL" : "OK");

	printf("sync...%s\n", cache->sync(cache) ? "FAIL" : "OK");
	printf("destroy\n");
	cache->destroy(cache);
}

int
main(int argc, char **argv)
{
	Cache a;

	printf("\n--Cache--\n");

	printf("create default cache...");
	isNotNull((a = CacheCreate(NULL, "cache.default")));
	TestCache(a);

	printf("\ncreate flat file cache...");
	isNotNull(a = CacheCreate("flatfile", "cache.properties"));
	TestCache(a);

	printf("\ncreate hash cache...");
	isNotNull(a = CacheCreate("hash", "cache.hash"));
	TestCache(a);

	printf("\ncreate bdb cache...");
	if ((a = CacheCreate("bdb", "cache.bdb")) == NULL) {
		printf("FAIL (no BDB support maybe)\n");
	} else {
		printf("OK\n");
		TestCache(a);
	}

	printf("\n--DONE--\n");

	return 0;
}

#endif /* TEST */
