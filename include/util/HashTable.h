/*
 * HashTable.h
 *
 * Copyright 2003, 2004 by Anthony Howe.  All rights reserved.
 */

#ifndef __com_snert_lib_util_HashTable_h__
#define __com_snert_lib_util_HashTable_h__	1

#ifndef __com_snert_lib_util_Vector_h__
#include <com/snert/lib/util/Vector.h>
#endif

#ifndef HashFunctionType
#define HashFunctionType	1
typedef unsigned long (*HashFunction)(void *);
#endif

#ifndef WALK_OK
#define	WALK_OK			0
#define WALK_STOP		1
#define WALK_REMOVE_ENTRY	2
#endif

typedef int (*HashTableWalkFunction)(void *key, void *value, void **data);
typedef void (*HashTableRemoveFunction)(void *key, void *value, void **data);

typedef void *HashTable;

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Create a new hash table. The default hash and compare functions
 * assume the keys will be C strings.
 */
extern HashTable HashTableCreate(void);

/*
 * Destroy a hash table.
 */
extern void HashTableDestroy(HashTable);

/*
 * Return the hash value of a C string. Use a POSIX CRC-32 function.
 */
extern unsigned long HashString(void *string);

/*
 * Specify the hash function applied to keys by HashTableGet(),
 * HashTableSet(), and HashTableRemove().
 */
extern void HashTableOnHash(HashTable, HashFunction);

/*
 * Specify the compare function applied to keys by HashTableGet(),
 * HashTableSet(), and HashTableRemove().
 */
extern void HashTableOnCompare(HashTable, CompareFunction);

/*
 * Specify a function to be called by HashTableGet(), HashTableSet(),
 * and HashTableRemove() when walking along key/value pairs that do
 * NOT match the key being search for.
 *
 * Can be used to define an expire/collection operation during lookups.
 * Specify a null function pointer to disable.
 */
extern void HashTableOnNext(HashTable, HashTableWalkFunction, void **data);

/*
 * Specify a function to be called when a key/value pair are removed
 * from the hash table. Intended to release the key and value objects.
 */
extern void HashTableOnRemove(HashTable, HashTableRemoveFunction, void **data);

/*
 * Return the number of entries held by the hash table.
 */
extern long HashTableCount(HashTable);

/*
 * Return the value for an existing key.
 */
extern void *HashTableGet(HashTable, void *key);

/*
 * Modify or insert a key and value.
 * Return 0 on success, otherwise -1 if malloc() fails.
 */
extern int HashTableSet(HashTable, void *key, void *value);

/*
 * Remove a key/value pair from the hash table. If HashTableOnRemove()
 * has not be previously set with a cleanup handler, then abort().
 */
extern void HashTableRemove(HashTable, void *key);

/*
 * Walk through every key/value pair, applying the supplied function
 * to every key, value, and data objects.
 */
extern void HashTableWalk(HashTable, HashTableWalkFunction, void **data);

/*
 * Return a vector of all the values.
 */
extern Vector HashTableKeys(HashTable);

/*
 * Return a vector of all the keys.
 */
extern Vector HashTableValues(HashTable);

/*
 * Predefined HashTableWalkFunction
 */
extern int HashTableWalkIgnore(void *key, void *value, void **data);
extern int HashTableCollectKeys(void *key, void *value, void **data);
extern int HashTableCollectValues(void *key, void *value, void **data);

/*
 * Predefined HashTableRemoveFunction
 */
extern void HashTableRemoveAbort(void *key, void *value, void **data);
extern void HashTableRemoveTrivial(void *key, void *value, void **data);

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_util_HashTable_h__ */

