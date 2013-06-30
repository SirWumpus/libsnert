/*
 * Hash.h
 *
 * An object that maps keys to values. A Dictionary cannot contain
 * duplicate keys; each non-null key can map to at most one non-null
 * value. Similar to Java's abstract Dictionary class.
 *
 * Copyright 2001, 2013 by Anthony Howe.  All rights reserved.
 */

#include <stdlib.h>

#include <com/snert/lib/version.h>
#include <com/snert/lib/type/Hash.h>

#ifdef DEBUG_MALLOC
# include <com/snert/lib/util/DebugMalloc.h>
#endif

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

typedef struct entry {
	/*@notnull@*/ Object key;
	/*@notnull@*/ Object value;
	/*@null@*/ struct entry *next;
} *HashEntry;

typedef /*@only@*/ struct entry *HashEntryOnly;

#define REF_HASH(v)		((Hash)(v))
#define REF_OBJECT(v)		((Object)(v))

/***********************************************************************
 *** Instance methods
 ***********************************************************************/

/*@access Object@*/

long
HashSize(Hash self)
{
	return self != NULL ? self->_size : 0;
}

int
HashIsEmpty(Hash self)
{
	return self == NULL || self->_size == 0;
}

/*@shared@*/
static HashEntryOnly *
HashFetch(Hash self, Object key)
{
	unsigned long hash;
	HashEntryOnly *prev;
	HashEntry curr;

	hash = (unsigned long) key->hashcode(key);
	hash %= TABLE_SIZE;

	for (prev = &((HashEntry *) self->_base)[hash], curr = *prev; curr != NULL; prev = &curr->next, curr = *prev) {
		if (key->equals(key, curr->key))
			break;
	}

	return prev;
}

void *
HashGet(Hash self, void *key)
{
	HashEntryOnly *prev;

	prev = HashFetch(self, key);
	if (*prev == NULL)
		return NULL;

	/* Return the value's reference, not a clone. The caller should
	 * know what is being returned and so can clone or duplicate
	 * the value if required.
	 */
	return (*prev)->value;
}

int
HashRemove(Hash self, void *key)
{
	HashEntryOnly *prev;
	HashEntry curr;

	prev = HashFetch(self, key);
	curr = *prev;

	if (curr == NULL)
		return -1;

	curr->value->destroy(curr->value);
	curr->key->destroy(curr->key);
	*prev = curr->next;
	self->_size--;
	free(curr);

	return 0;
}

void
HashRemoveAll(Hash self)
{
	long i;

	if (self == NULL)
		return;

	for (i = 0; i < TABLE_SIZE; i++) {
		HashEntry curr, next;
		for (curr = ((HashEntry *) self->_base)[i]; curr != NULL; curr = next) {
			curr->value->destroy(curr->value);
			curr->key->destroy(curr->key);
			next = curr->next;
			free(curr);
		}
		((HashEntry *) self->_base)[i] = NULL;
	}
	self->_size = 0;
}

int
HashPut(Hash self, void *key, void *value)
{
	HashEntryOnly *prev;
	HashEntry curr;

	if (key == NULL || value == NULL)
		return -1;

	prev = HashFetch(self, key);
	curr = *prev;

	/*@-branchstate@*/
	if (curr == NULL) {
		/* Assign a new entry to store the values. */
		/*@-mustfreeonly@*/
		if ((curr = malloc(sizeof (*curr))) == NULL)
			return -1;
		/*@=mustfreeonly@*/

		curr->next = *prev;
		self->_size++;
		*prev = curr;
	} else {
		/* Destroy previous key/value before being replaced. */
		if (value != curr->value)
			curr->value->destroy(curr->value);
		if (key != curr->key)
			curr->key->destroy(curr->key);
	}
	/*@=branchstate@*/

	curr->value = value;
	curr->key = key;

	return 0;
}

int
HashWalk(Hash self, int (*function)(void *, void *, void *), void *data)
{
	long i;
	HashEntry curr;
	HashEntryOnly *prev;

	for (i = 0; i < TABLE_SIZE; i++) {
		for (prev = &((HashEntry *) self->_base)[i], curr = *prev; curr != NULL; curr = *prev) {
			switch ((*function)(curr->key, curr->value, data)) {
			case 0:
				/* Stop. */
				return 0;
			case -1:
				/* Delete current entry, continue. */
				curr->value->destroy(curr->value);
				curr->key->destroy(curr->key);
				*prev = curr->next;
				self->_size--;
				free(curr);
				break;
			default:
				/* Next entry. */
				prev = &curr->next;
				break;
			}
		}
	}

	return 0;
}

int
HashAll(Hash self, int (*function)(void *, void *, void *), void *data)
{
	long i;
	HashEntry curr;

	for (i = 0; i < TABLE_SIZE; i++) {
		for (curr = ((HashEntry *) self->_base)[i]; curr != NULL; curr = curr->next) {
			if ((*function)(curr->key, curr->value, data) == 0)
				return 0;
		}
	}

	return 1;
}

int
HashSome(Hash self, int (*function)(void *, void *, void *), void *data)
{
	long i;
	HashEntry curr;

	for (i = 0; i < TABLE_SIZE; i++) {
		for (curr = ((HashEntry *) self->_base)[i]; curr != NULL; curr = curr->next) {
			if ((*function)(curr->key, curr->value, data) == 1)
				return 1;
		}
	}

	return 0;
}

void
HashDestroy(void *selfless)
{
	HashRemoveAll(selfless);
	free(selfless);
}

/***********************************************************************
 *** Class methods
 ***********************************************************************/

Hash
HashCreate(void)
{
	Hash self;
	static struct hash model;

	if ((self = malloc(sizeof (*self))) == NULL)
		return NULL;

	if (model.objectName == NULL) {
		/* Setup the super class. */
		ObjectInit(&model);

		/* Overrides */
		model.objectSize = sizeof (struct hash);
		model.objectName = "Hash";
		model.destroy = HashDestroy;

		/* Methods */
		model.get = HashGet;
		model.isEmpty = HashIsEmpty;
		model.put = HashPut;
		model.remove = HashRemove;
		model.removeAll = HashRemoveAll;
		model.size = HashSize;
		model.walk = HashWalk;
		model.all = HashAll;
		model.some = HashSome;
		model.objectMethodCount += 9;

		/* Instance Variables. */
		model._base = NULL;
		model._size = 0;
	}

	*self = model;

	/*@-mustfreeonly@*/
	self->_base = calloc(TABLE_SIZE, sizeof (void *));
	/*@=mustfreeonly@*/
	if (self->_base == NULL) {
		free(self);
		return NULL;
	}

	return self;
}

/***********************************************************************
 *** Test
 ***********************************************************************/

#ifdef TEST

#include <stdio.h>
#include <com/snert/lib/version.h>
#include <com/snert/lib/type/Data.h>

#ifdef USE_DEBUG_MALLOC
# define WITHOUT_SYSLOG			1
# include <com/snert/lib/io/Log.h>
# include <com/snert/lib/util/DebugMalloc.h>
#endif

void
isNotNull(void *ptr)
{
	if (ptr == NULL) {
		printf("...NULL\n");
		exit(1);
	}

	printf("...OK\n");
}

int
countProperties(void *key, void *value, void *data)
{
	(*(long *) data)++;
	printf("count=%ld %s=%s\n", *(long *) data, ((Data) key)->_base, ((Data) value)->_base);

	return 1;
}

void
isEqual(Data a, Data b)
{
	if (a == NULL) {
		printf("...argument 1 NULL\n");
		exit(1);
	}
	if (b == NULL) {
		printf("...argument b NULL\n");
		exit(1);
	}
	printf("...%s\n", a->equals(a, b) ? "OK" : "FAIL");
}

int
main(int argc, char **argv)
{
	Hash a;
	long count;
	Data v1, v2, v3, v4, v5;

	printf("\n--Hash--\n");

	printf("create hash A");
	isNotNull((a = HashCreate()));

	printf("isEmpty=%d...%s\n", a->isEmpty(a), a->isEmpty(a) ? "TRUE" : "FALSE");
	printf("size=%ld...%s\n", a->size(a), a->size(a) == 0 ? "OK" : "FAIL");

	printf("--put 5 strings\n");
	a->put(a, DataCreateCopyString("key1"), v1 = DataCreateCopyString("value1"));
	a->put(a, DataCreateCopyString("key2"), v2 = DataCreateCopyString("value2"));
	a->put(a, DataCreateCopyString("key3"), v3 = DataCreateCopyString("value3"));
	a->put(a, DataCreateCopyString("key4"), v4 = DataCreateCopyString("value4"));
	a->put(a, DataCreateCopyString("key5"), v5 = DataCreateCopyString("value5"));

	printf("isEmpty=%d...%s\n", a->isEmpty(a), a->isEmpty(a) ? "TRUE" : "FALSE");
	printf("size=%ld...%s\n", a->size(a), a->size(a) == 5 ? "OK" : "FAIL");

	printf("--get 5 strings\n");
	printf("value1..."); isEqual(v1, a->get(a, DataCreateCopyString("key1")));
	printf("value2..."); isEqual(v2, a->get(a, DataCreateCopyString("key2")));
	printf("value3..."); isEqual(v3, a->get(a, DataCreateCopyString("key3")));
	printf("value4..."); isEqual(v4, a->get(a, DataCreateCopyString("key4")));
	printf("value5..."); isEqual(v5, a->get(a, DataCreateCopyString("key5")));

	count = 0;
	a->walk(a, countProperties, &count);

	printf("destroy hash A\n");
	a->destroy(a);

	printf("\n--DONE--\n");

	return 0;



	return 0;
}

#endif /* TEST */
