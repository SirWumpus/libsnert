/*
 * Hash.h
 *
 * An object that maps keys to values. A hash cannot contain
 * duplicate keys; each non-null key can map to at most one non-null
 * value. Similar to Java's abstract Dictionary class and HashMap.
 *
 * Copyright 2001, 2006 by Anthony Howe.  All rights reserved.
 */

#ifndef __com_snert_lib_type_Hash_h__
#define __com_snert_lib_type_Hash_h__	1

#ifdef __cplusplus
extern "C" {
#endif

#include <com/snert/lib/type/Object.h>

typedef /*@abstract@*/ struct hash {
	OBJECT_OBJECT;

	/**
	 * Lookup up a key and return its value.
	 *
	 * @param self
	 *	This object.
	 *
	 * @param key
	 *	The key object to lookup.
	 *
	 * @return
	 *	A pointer to an object. Null if no mapping found. The caller
	 *	MUST NOT destroy the object returned, instead use one of the
	 *	remove methods.
	 */
	/*@observer@*//*@null@*/ void *(*get)(struct hash *self, void *key);

	/**
	 * Return true if the Hash is empty.
	 *
	 * @param self
	 *	This object.
	 *
	 * @return
	 *	True if the Hash is empty, false otherwise.
	 */
	/*@falsewhennull@*/ int (*isEmpty)(struct hash *self);

	/**
	 * Put the key/value pair into the Hash. If the key has a previous
	 * value, it will be destroyed by the method. There is no concurrency
	 * synchronisation.
	 *
	 * @param self
	 *	This object.
	 *
	 * @param key
	 *	The object to use for lookups.
	 *
	 * @param value
	 *	The object associated with the key.
	 *
	 * @return
	 *	Zero on success, otherwise some other value on error.
	 */
	int (*put)(struct hash *self, /*@keep@*/ void *key, /*@keep@*/ void *value);

	/**
	 * Remove a key/value pair from the Hash. There is no concurrency
	 * synchronisation. The key/value pair are destroyed by the method.
	 *
	 * @param self
	 *	This object.
	 *
	 * @param key
	 *	The object to lookup.
	 *
	 * @return
	 *	Zero on success, otherwise some other value if the key was
	 *	not found.
	 */
	int (*remove)(struct hash *self, void *key);

	/**
	 * Remove all key/value pairs from the Hash. There is no concurrency
	 * synchronisation. The key and value objects are destroyed by the
	 * method.
	 *
	 *
	 * @param self
	 *	This object.
	 */
	void (*removeAll)(/*@null@*/ struct hash *self);

	/**
	 * Return how many key/value pairs are stored.
	 *
	 * @param self
	 *	This object.
	 *
	 * @return
	 *	Number of entries.
	 */
	long (*size)(struct hash *self);

	/**
	 * Walk the hash visiting every key/value pair.
	 * There is no concurrency synchronisation.
	 *
	 * @param self
	 *	This object.
	 *
	 * @param function
	 *	A call-back function that is given the current key/value pair
	 *	to examine. This function returns 0 to stop walking, 1 to
	 *	continue walking, or -1 remove the current key/value pair and
	 *	continue walking.
	 *
	 * @param data
	 *	An obsecure data type passed to the callback function.
	 *
	 * @return
	 *	Zero on success, otherwise some other value on error.
	 */
	int (*walk)(struct hash *self, int (*function)(void *key, void *value, void *data), void *data);

	/**
	 * Walk the hash visiting every key/value pair.
	 * There is no concurrency synchronisation.
	 *
	 * @param self
	 *	This object.
	 *
	 * @param function
	 *	A call-back function that is given the current key/value pair
	 *	to examine. This function returns true or false.
	 *
	 * @param data
	 *	An obsecure data type passed to the callback function.
	 *
	 * @return
	 *	False if some element returns false; otherwise true if all elements
	 *	returned true.
	 */
	int (*all)(struct hash *self, int (*function)(void *key, void *value, void *data), void *data);

	/**
	 * Walk the hash visiting every key/value pair.
	 * There is no concurrency synchronisation.
	 *
	 * @param self
	 *	This object.
	 *
	 * @param function
	 *	A call-back function that is given the current key/value pair
	 *	to examine. This function returns true or false.
	 *
	 * @param data
	 *	An obsecure data type passed to the callback function.
	 *
	 * @return
	 *	True if some element returns true; otherwise false if all elements
	 *	returned false.
	 */
	int (*some)(struct hash *self, int (*function)(void *key, void *value, void *data), void *data);

	/*
	 * Private
	 */
	void **_base;
	long _size;
} *Hash;

/*@-exportlocal@*/

/**
 * Create a new and initialised Hash.
 *
 * @return
 *	A new Hash object; otherwise null on error.
 */
extern /*@only@*//*@null@*/ Hash HashCreate(void);

/*
 * Backwards compatibility with previous API.
 */
extern void HashDestroy(/*@only@*//*@null@*/ void *self);
extern /*@observer@*//*@null@*/ void *HashGet(Hash self, void *key);
extern /*@falsewhennull@*/ int HashIsEmpty(Hash self);
extern int HashPut(Hash self, /*@keep@*/ void *key, /*@keep@*/ void *value);
extern int HashRemove(Hash self, void *key);
extern void HashRemoveAll(/*@null@*/ Hash self);
extern long HashSize(Hash self);
extern int HashWalk(Hash self, int (*function)(void *, void *, void *), void *data);
extern int HashAll(Hash self, int (*function)(void *, void *, void *), void *data);
extern int HashSome(Hash self, int (*function)(void *, void *, void *), void *data);

/*@=exportlocal@*/

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_type_Hash_h__ */

