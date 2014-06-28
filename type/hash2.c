/*
 * hash2.c
 *
 * An object that maps keys to values. A Dictionary cannot contain
 * duplicate keys; each key string can map to at most one value.
 * value. Similar to Java's abstract Dictionary class.
 *
 * Copyright 2014 by Anthony Howe.  All rights reserved.
 */

#include <stdlib.h>
#include <string.h>

#include <com/snert/lib/version.h>
#include <com/snert/lib/type/hash2.h>
#include <com/snert/lib/util/Text.h>

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
#define TABLE_SIZE		1021
#endif

typedef struct hash_item {
	char *key;
	void *value;
	struct hash_item *next;
} HashItem;

/*
 * D.J. Bernstien Hash version 2 (+ replaced by ^).
 */
static size_t
djb_hash_index(const char *key, size_t klen, size_t table_size)
{
        size_t hash = 5381;

	for ( ; 0 < klen-- && *key != '\0'; key++) {
        	/* hash(i) = hash(i-1) * 33 ^ buffer[i] */
                hash = ((hash << 5) + hash) ^ *(unsigned char *)key;
	}

	/* Table size must be a prime number. */
        return hash % table_size;
}

static void
free_stub(void *entry)
{
	/* Do nothing */
}

/***********************************************************************
 *** Instance methods
 ***********************************************************************/

#ifdef HASH_FUNCTIONS
size_t
hash_size(Hash *self)
{
	return self != NULL ? self->_size : 0;
}

size_t
hash_length(Hash *self)
{
	return self != NULL ? self->_length : 0;
}

int
hash_is_empty(Hash *self)
{
	return self == NULL || self->_length == 0;
}
#endif

void
hash_set_free_value(Hash *self, void (*freefn)(void*))
{
	if (self != NULL)
		self->_free_value = freefn == NULL ? free_stub : freefn;
}

static HashItem **
hash_fetch(Hash *self, const char *key, size_t klen)
{
	unsigned long hash;
	HashItem **prev, *curr;

	hash = (unsigned long) djb_hash_index(key, klen, hash_size(self));

	/* Walk the list found at this hash. */
	for (prev = &((HashItem **) self->_base)[hash], curr = *prev; curr != NULL; prev = &curr->next, curr = *prev) {
		if (strncmp(key, curr->key, klen) == 0)
			break;
	}

	return prev;
}

void *
hash_getk(Hash *self, const char *key, size_t klen)
{
	HashItem **item;

	if ((item = hash_fetch(self, key, klen)) == NULL)
		return NULL;

	return (*item)->value;
}

#ifdef HASH_FUNCTIONS
void *
hash_get(Hash *self, const char *key, size_t klen)
{
	return hash_getk(self, key, SIZE_MAX);
}
#endif

int
hash_removek(Hash *self, const char *key, size_t klen)
{
	HashItem **prev, *curr;

	prev = hash_fetch(self, key, klen);
	curr = *prev;

	if (curr == NULL)
		return -1;

	(*self->_free_value)(curr->value);
	*prev = curr->next;
	self->_length--;

	/* Discard the HashItem and its key. */
	free(curr);

	return 0;
}

#ifdef HASH_FUNCTIONS
int
hash_remove(Hash *self, const char *key)
{
	return hash_removek(self, key, SIZE_MAX);
}
#endif

void
hash_remove_all(Hash *self)
{
	size_t i;

	if (self == NULL)
		return;

	for (i = 0; i < self->_size; i++) {
		HashItem *curr, *next;
		for (curr = ((HashItem **) self->_base)[i]; curr != NULL; curr = next) {
			(*self->_free_value)(curr->value);
			next = curr->next;
			free(curr);
		}
		self->_base[i] = NULL;
	}
	self->_length = 0;
}

int
hash_putk(Hash *self, const char *key, size_t klen, void *value)
{
	HashItem **prev, *curr;

	if (key == NULL)
		return -1;

	prev = hash_fetch(self, key, klen);
	curr = *prev;

	if (curr == NULL) {
		/* Allocate a HashItem and key string. */
		if ((curr = malloc(sizeof (*curr) + klen + 1)) == NULL)
			return -1;

		curr->key = (char *)&curr[1];
		(void) TextCopy(curr->key, klen+1, key);
		curr->next = *prev;
		self->_length++;
		*prev = curr;
	} else {
		/* Destroy previous value before replacing. */
		if (value != curr->value)
			(*self->_free_value)(curr->value);
	}

	curr->value = value;

	return 0;
}

#ifdef HASH_FUNCTIONS
int
hash_put(Hash *self, const char *key, size_t klen, void *value)
{
	return hash_putk(self, key, SIZE_MAX, value);
}
#endif

int
hash_walk(Hash *self, HashWalkFn func, void *data)
{
	long i;
	HashItem **prev, *curr;

	for (i = 0; i < self->_size; i++) {
		for (prev = &((HashItem **) self->_base)[i], curr = *prev; curr != NULL; curr = *prev) {
			switch ((*func)(curr->key, curr->value, data)) {
			case 0:
				/* Stop. */
				return 0;
			case -1:
				/* Delete current entry, continue. */
				(*self->_free_value)(curr->value);
				*prev = curr->next;
				self->_length--;
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

void
hash_destroy(void *_self)
{
	hash_remove_all(_self);
	free(_self);
}

/***********************************************************************
 *** Class methods
 ***********************************************************************/

Hash *
hash_create_size(size_t prime_size)
{
	Hash *self;

	if ((self = malloc(sizeof (*self))) == NULL)
		return NULL;

	self->_base = calloc(prime_size, sizeof (void *));
	if (self->_base == NULL) {
		free(self);
		return NULL;
	}

	/* Assumes value was returned by malloc(), see HashSetFreeValue() to change. */
	self->_free_value = free;
	self->_free = hash_destroy;
	self->_size = prime_size;
	self->_length = 0;

	return self;
}

Hash *
hash_create(void)
{
	return hash_create_size(TABLE_SIZE);
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
		printf("...NULL\n");
		exit(1);
	}

	printf("...OK\n");
}

int
countProperties(void *key, void *value, void *data)
{
	(*(long *) data)++;
	printf("count=%ld %s=%s\n", *(long *) data, (char *)key, (char *)value);

	return 1;
}

void
isEqual(char *a, char * b)
{
	if (a == NULL) {
		printf("...argument 1 NULL\n");
		exit(1);
	}
	if (b == NULL) {
		printf("...argument b NULL\n");
		exit(1);
	}
	printf("...%s\n", strcmp(a, b) == 0 ? "OK" : "FAIL");
}

int
main(int argc, char **argv)
{
	Hash *a;
	long count;

	printf("\n--Hash--\n");

	printf("create hash A");
	isNotNull((a = hash_create_size(7)));

	printf("isEmpty=%d...%s\n", hash_is_empty(a), hash_is_empty(a) ? "TRUE" : "FALSE");
	printf("size=%ld...%s\n", (unsigned long) hash_length(a), hash_length(a) == 0 ? "OK" : "FAIL");

	printf("--put 5 strings\n");
	hash_put(a, ("key1"), strdup("value1"));
	hash_put(a, ("key2"), strdup("value2"));
	hash_put(a, ("key3"), strdup("value3"));
	hash_put(a, ("key4"), strdup("value4"));
	hash_put(a, ("key5"), strdup("value5"));

	printf("isEmpty=%d...%s\n", hash_is_empty(a), hash_is_empty(a) ? "TRUE" : "FALSE");
	printf("size=%lu...%s\n", (unsigned long) hash_length(a), hash_length(a) == 5 ? "OK" : "FAIL");

	printf("--get 5 strings\n");
	printf("value1..."); isEqual("value1", hash_get(a, ("key1")));
	printf("value2..."); isEqual("value2", hash_get(a, ("key2")));
	printf("value3..."); isEqual("value3", hash_get(a, ("key3")));
	printf("value4..."); isEqual("value4", hash_get(a, ("key4")));
	printf("value5..."); isEqual("value5", hash_get(a, ("key5")));

	count = 0;
	hash_walk(a, countProperties, &count);

	printf("remove some\n");
	hash_remove(a, "key3");
	hash_remove(a, "key2");
	count = 0;
	hash_walk(a, countProperties, &count);

	printf("destroy hash\n");
	hash_destroy(a);

	printf("\n--DONE--\n");

	return 0;



	return 0;
}

#endif /* TEST */
