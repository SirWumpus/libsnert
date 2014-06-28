/*
 * hash2.h
 *
 * An object that maps keys to values. A hash cannot contain
 * duplicate keys; each non-null key can map to at most one non-null
 * value. Similar to Java's abstract Dictionary class and hash_map.
 *
 * Copyright 2014 by Anthony Howe.  All rights reserved.
 */

#ifndef __com_snert_lib_type_hash_h__
#define __com_snert_lib_type_hash_h__	1

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>

#ifndef SIZE_MAX
#define SIZE_MAX	(~(size_t)0 >> 1)
#endif

typedef int (*HashWalkFn)(void *key, void *value, void *data);

typedef size_t (*HashFn)(void *);

typedef struct hash {
	void (*_free)(void *);
	void (*_free_value)(void *);
	size_t _length;
	size_t _size;
	void **_base;
} Hash;

#ifdef HASH_FUNCTIONS
extern size_t hash_size(Hash *);
extern size_t hash_length(Hash *);
extern int hash_is_empty(Hash *);
extern void *hash_get(Hash *, const char *);
extern int hash_put(Hash *, const char *, void *);
extern int hash_remove(Hash *, const char *);
#else
# define hash_size(h)			(h)->_size
# define hash_length(h)			(h)->_length
# define hash_is_empty(h)		((h)->_length == 0)
# define hash_get(h, k)			hash_getk(h, k, SIZE_MAX)
# define hash_put(h, k, v)		hash_putk(h, k, SIZE_MAX, v)
# define hash_remove(h, k)		hash_removek(h, k, SIZE_MAX)
#endif

/**
 * Create a new and initialised Hash using the default hash size.
 *
 * @return
 *	A new Hash pointer; otherwise null on error.
 */
extern Hash *hash_create(void);

/**
 * Create a new and initialised Hash.
 *
 * @param size
 *	The hash table size. The size of the hash table should be a
 *	small prime number:
 *
 *	449, 509, 673, 991, 997, 1021, 2039, 4093, 8191
 *
 * @return
 *	A new Hash pointer; otherwise null on error.
 */
extern Hash *hash_create_size(unsigned prime_size);

/**
 * @param self
 *	A Hash *structure, previously returned by HashCreate(),
 *	to be freed.
 */
extern void hash_destroy(void *self);

/**
 * Lookup up a key and return its value.
 *
 * @param self
 *	This object.
 *
 * @param key
 *	The key object to lookup.
 *
 * @param klen
 *	Length of key string, otheriwse until terminating NUL byte.
 *
 * @return
 *	A pointer to an object. Null if no mapping found. The caller
 *	MUST NOT destroy the object returned, instead use one of the
 *	remove methods.
 */
extern void *hash_getk(Hash *self, const char *key, size_t klen);

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
 * @param klen
 *	Length of key string, otheriwse until terminating NUL byte.
 *
 * @return
 *	Zero on success, otherwise some other value on error.
 */
extern int hash_putk(Hash *self, const char *key, size_t klen, void *value);

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
 * @param klen
 *	Length of key string, otheriwse until terminating NUL byte.
 *
 * @return
 *	Zero on success, otherwise some other value if the key was
 *	not found.
 */
extern int hash_removek(Hash *self, const char *key, size_t klen);

/**
 * Remove all key/value pairs from the Hash. There is no concurrency
 * synchronisation. The key and value objects are destroyed by the
 * method.
 *
 *
 * @param self
 *	This object.
 */
extern void hash_remove_all(Hash *self);

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
extern int hash_walk(Hash *self, HashWalkFn func, void *data);

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_type_hash_h__ */

