/*
 * Hash.c
 *
 * Copyright 2002, 2006 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <stdlib.h>
#include <string.h>

#include <com/snert/lib/object/Object.h>

/*
 * The size of the hash table should be a small prime number:
 *
 *	449, 509, 673, 991, 997, 1021, 2039, 4093, 8191
 *
 * Using a prime number for the table size means that double
 * hashing or linear-probing can visit all possible entries.
 *
 * This is NOT a runtime option, because its not something I
 * want people to play with unless absolutely necessary.
 */
#ifndef TABLE_SIZE
#define TABLE_SIZE		4093
#endif

typedef struct {
	CLASS_ATOM;
} atom;

typedef struct map_entry {
	atom *key;
	atom *value;
	struct map_entry *next;
} map_entry;

static void *
hashmap_get(void *_self, void *key)
{
	hashmap *self = _self;
	unsigned long index;
	map_entry *entry;

	if (self == NULL || key == NULL)
		return NULL;

	index = ((atom *) key)->hashcode(key) % TABLE_SIZE;
	entry = ((map_entry **) self->base)[index];

	for ( ; entry != NULL; entry = entry->next) {
		if (((atom *) key)->compare(key, entry->key) == 0)
			return entry->value;
	}

	return NULL;
}

static int
hashmap_put(void *_self, void *key, void *value)
{
	atom *copy;
	unsigned long index;
	hashmap *self = _self;
	map_entry **prev, *entry;

	if (self == NULL || key == NULL || value == NULL)
		return MAP_ERROR;

	index = ((atom *) key)->hashcode(key) % TABLE_SIZE;
	entry = ((map_entry **) self->base)[index];
	prev = &((map_entry **) self->base)[index];

	for ( ; entry != NULL; prev = &entry->next, entry = entry->next) {
		if (((atom *) key)->compare(key, entry->key) == 0) {
			/* Replace current entry's value. */
			if ((copy = ((atom *) value)->clone(value)) == NULL)
				return MAP_ERROR;

			objectDestroy(entry->value);
			entry->value = copy;
			return MAP_OK;
		}
	}

	if ((entry = malloc(sizeof (*entry))) == NULL)
		return MAP_ERROR;

	entry->value = ((atom *) value)->clone(value);
	entry->key = ((atom *) key)->clone(key);

	if (entry->key == NULL || entry->value == NULL) {
		objectDestroy(entry->value);
		objectDestroy(entry->key);
		free(entry);
		return MAP_ERROR;
	}

	/* Insert or replace entry. */
	entry->next = *prev;
	*prev = entry;
	self->size++;

	return MAP_OK;
}

static int
hashmap_remove(void *_self, void *key)
{
	hashmap *self = _self;
	unsigned long index;
	map_entry **prev, *entry;

	if (self == NULL || key == NULL)
		return MAP_ERROR;

	index = ((atom *) key)->hashcode(key) % TABLE_SIZE;
	entry = ((map_entry **) self->base)[index];
	prev = &((map_entry **) self->base)[index];

	for ( ; entry != NULL; prev = &entry->next, entry = entry->next) {
		if (((atom *) key)->compare(key, entry->key) == 0) {
			objectDestroy(entry->value);
			objectDestroy(entry->key);
			*prev = entry->next;
			self->size--;
			free(entry);
			return MAP_OK;
		}
	}

	return MAP_NOT_FOUND;
}

static void *
hashmap_clone(void *_self)
{
	return objectCreate(Hashmap);
}

static void
hashmap_destroy(void *_self)
{
	unsigned long i;
	hashmap *self = _self;
	map_entry **table, *entry, *next;

	if (self != NULL && self->base != NULL) {
		table = (map_entry **) self->base;

		for (i = 0; i < TABLE_SIZE; i++) {
			for (entry = table[i]; entry != NULL; entry = next) {
				objectDestroy(entry->value);
				objectDestroy(entry->key);
				next = entry->next;
				free(entry);
			}
		}

		self->base = NULL;
		self->size = 0;
		free(table);
	}
}

static int
hashmap_compare(void *_self, void *_other)
{
	if (_other == NULL || ((object *) _other)->destroy != hashmap_destroy)
		return -1;

	if (_self == _other)
		return 0;

	if (((hashmap *) _self)->size == ((hashmap *) _other)->size)
		return (int) (_self - _other);

	return ((hashmap *) _self)->size - ((hashmap *) _other)->size;
}

static unsigned long
hashmap_hashcode(void *_self)
{
	return (unsigned long) _self;
}

static void *
hashmap_create(void *_self, va_list *args)
{
	hashmap *self = _self;

	if ((self->base = calloc(TABLE_SIZE, sizeof (void *))) == NULL)
		return NULL;

	self->size = 0;

	return self;
}

static const struct hashmap _Hashmap = {
	hashmap_destroy,
	hashmap_create,
	sizeof (const struct hashmap),
	hashmap_clone,
	hashmap_compare,
	hashmap_hashcode,
	hashmap_get,
	hashmap_put,
	hashmap_remove
};

const void *Hashmap = &_Hashmap;

/***********************************************************************
 *** END
 ***********************************************************************/
