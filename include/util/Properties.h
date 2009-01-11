/*
 * Properties.h
 *
 * Copyright 2001, 2006 by Anthony Howe.  All rights reserved.
 */

#ifndef __com_snert_lib_util_Properties_h__
#define __com_snert_lib_util_Properties_h__	1

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <com/snert/lib/type/Data.h>

typedef struct properties {
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
	char *(*getProperty)(struct properties *self, const char *key);
	Data (*getData)(struct properties *self, Data key);

	/**
	 * Put the key/value pair into the object. If the key's has a previous
	 * value, it will be destroyed by the method. There is no concurrency
	 * synchronisation.
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
	int (*setProperty)(struct properties *self, const char *key, const char *value);
	int (*setData)(struct properties *self, Data key, Data value);

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
	int (*removeProperty)(struct properties *self, const char *key);
	int (*removeData)(struct properties *self, Data key);

	/**
	 * Remove all key/value pairs from the object. There is no concurrency
	 * synchronisation. The key and value objects are destroyed by the
	 * method.
	 *
	 *
	 * @param self
	 *	This object.
	 */
	void (*removeAll)(struct properties *self);

	/**
	 * Return how many key/value pairs are stored.
	 *
	 * @param self
	 *	This object.
	 *
	 * @return
	 *	Number of entries.
	 */
	long (*size)(struct properties *self);

	/**
	 * Walk the object visiting every key/value pair.
	 * There is no concurrency synchronisation.
	 *
	 * @param self
	 *	This object.
	 *
	 * @param function
	 *	A call-back function that is give the current key/value pair
	 *	to examine. This function returns 0 to stop walking, 1 to
	 *	continue, walking, or -1 remove the current key/pair and
	 *	continue walking.
	 *
	 * @param data
	 *	An obsecure data type passed to the callback function.
	 *
	 * @return
	 *	Zero on success, otherwise some other value on error.
	 */
	int (*walk)(struct properties *self, int (*function)(void *key, void *value, void *data), void *data);

	/**
	 * Load the properties from a flat text file of key/value pairs.
	 * The method will attempt to lock the file before proceeding to
	 * read.
	 *
	 * @param self
	 *	This object.
	 *
	 * @param input
	 *	An already opened input stream.
	 *
	 * @return
	 *	Zero on success, otherwise some other value on error.
	 */
	int (*load)(struct properties *self, const char *file);

	/**
	 * Save the properties to a flat text file of key/value pairs. The
	 * method will attempt to lock the file before proceeding to write.
	 *
	 * @param self
	 *	This object.
	 *
	 * @param input
	 *	An already opened output stream.
	 *
	 * @return
	 *	Zero on success, otherwise some other value on error.
	 */
	int (*save)(struct properties *self, const char *file);

	/*
	 * Private
	 */
	void *_properties;
} *Properties;

extern Properties PropertiesCreate(void);

extern void PropertiesDestroy(void *self);
extern Data PropertiesGetData(Properties self, Data key);
extern char *PropertiesGetProperty(Properties self, const char *key);
extern int PropertiesLoad(Properties self, const char *file);
extern void PropertiesRemoveAll(Properties self);
extern int PropertiesRemoveData(Properties self, Data key);
extern int PropertiesRemoveProperty(Properties self, const char *key);
extern int PropertiesSave(Properties self, const char *file);
extern int PropertiesSetData(Properties self, Data key, Data value);
extern int PropertiesSetProperty(Properties self, const char *key, const char *value);
extern int PropertiesWalk(Properties self, int (*function)(void *key, void *value, void *data), void *data);
extern long PropertiesSize(Properties self);

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_util_Properties_h__ */
