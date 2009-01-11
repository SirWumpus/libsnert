/*
 * Cache.h
 *
 * An object that maps keys to values. A Cache cannot contain
 * duplicate keys; each non-null key can map to at most one non-null
 * value. Similar to Java's abstract Dictionary class.
 *
 * Copyright 2001, 2006 by Anthony Howe.  All rights reserved.
 */

#ifndef __com_snert_lib_util_Cache_h__
#define __com_snert_lib_util_Cache_h__	1

#ifdef __cplusplus
extern "C" {
#endif

#include <com/snert/lib/type/Data.h>

typedef struct cache {
	OBJECT_OBJECT;

	/**
	 * Lookup up a key and return a copy of its value.
	 *
	 * @param self
	 *	This cache object.
	 *
	 * @param key
	 *	The key object to lookup.
	 *
	 * @return
	 *	A pointer to the value object. Null if no mapping found.
	 *	The object must be destroyed by the caller when done.
	 */
	/*@only@*//*@null@*/ Data (*get)(struct cache *self, Data key);

	/**
	 * Return true if the Cache is empty.
	 *
	 * @param self
	 *	This cache object.
	 *
	 * @return
	 *	True if the Cache is empty, false otherwise.
	 */
	int (*isEmpty)(struct cache *self);

	/**
	 * Put a copy of the key/value pair into the Cache. The key's previous
	 * value will be destroyed. There is no concurrency synchronisation.
	 *
	 * @param self
	 *	This object.
	 *
	 * @param key
	 *	The object to used for lookups.
	 *
	 * @param value
	 *	The object associated with the key.
	 *
	 * @return
	 *	Zero on success, otherwise some other value on error.
	 */
	int (*put)(struct cache *self, Data key, Data value);

	/**
	 * Remove a key/value pair from the Cache. There is no
	 * concurrency synchronisation.
	 *
	 * @param self
	 *	This cache object.
	 *
	 * @param key
	 *	The object to lookup.
	 *
	 * @return
	 *	Zero on success, otherwise some other value on error.
	 *
	 * @see
	 *	CacheCreate(), Cache->removeAll(), Cache->setRemoveEntry()
	 */
	int (*remove)(struct cache *self, Data key);

	/**
	 * Remove all key/value pairs from the Cache. There is no
	 * concurrency synchronisation.
	 *
	 * @param self
	 *	This cache object.
	 *
	 * @return
	 *	Zero on success, otherwise some other value on error.
	 *
	 * @see
	 *	CacheCreate(), Cache->remove(), Cache->setRemoveEntry()
	 */
	int (*removeAll)(struct cache *self);

	/**
	 * Return how many key/value pairs are stored.
	 *
	 * @param self
	 *	This cache object.
	 *
	 * @return
	 *	Number of entries.
	 */
	long (*size)(struct cache *self);

	/**
	 * If the underlying handler is some form of database, synchronise
	 * it with the disk. There is no concurrency synchronisation.
	 *
	 * @param self
	 *	This cache object.
	 *
	 * @return
	 *	Zero on success, otherwise some other value on error.
	 */
	int (*sync)(struct cache *self);

	/**
	 * Walk the Cache visiting every key/value pair.
	 * There is no concurrency synchronisation.
	 *
	 * @param self
	 *	This cache object.
	 *
	 * @param function
	 *	A call-back function that is give the current key/value pair
	 *	to examine. This function returns true to continue walking or
	 *	false to stop walking.
	 *
	 * @return
	 *	Zero on success, otherwise some other value on error.
	 */
	int (*walk)(struct cache *self, int (*function)(void *key, void *value, void *data), void *data);

	void (*setDebug)(int flag);

	/*
	 * Private
	 */
	void *_cache;
	char *_name;
	int _lockfd;
	int _debug;
} *Cache;

/*@-exportlocal@*/

extern void CacheSetDebug(int flag);

/**
 * Create a new Cache.
 *
 * @param handler
 *	Currently supported handlers are "hash" and "bdb". If a null pointer
 *	is given, then the default handler is choosen. The "hash" type is
 *	always available, but not persistent across application restarts.
 *
 * @param name
 *	If the underlying handler is a database, then self is the database
 *	name to operate on.
 */
extern /*@only@*//*@null@*/ Cache CacheCreate(const char *handler, const char *name);

/*@=exportlocal@*/

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_util_Cache_h__ */

