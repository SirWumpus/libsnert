/*
 * Vector.c
 *
 * Copyright 2001, 2013 by Anthony Howe.  All rights reserved.
 */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <com/snert/lib/version.h>
#include <com/snert/lib/type/Object.h>
#include <com/snert/lib/type/Vector.h>

#ifdef DEBUG_MALLOC
#include <com/snert/lib/util/DebugMalloc.h>
#endif

#ifndef VECTOR_GROWTH
#define VECTOR_GROWTH		1000
#endif

/***********************************************************************
 *** Instance methods
 ***********************************************************************/

void **
VectorBase(Vector self)
{
	static const void *empty[] = { NULL };

	if (self == NULL) {
		errno = EFAULT;
		return (void **) empty;
	}

	return self->_base;
}

long
VectorCapacity(Vector self)
{
	if (self == NULL) {
		errno = EFAULT;
		return 0;
	}

	return self->_size;
}

long
VectorLength(Vector self)
{
	if (self == NULL) {
		errno = EFAULT;
		return 0;
	}

	return self->_length;
}

int
VectorIsEmpty(Vector self)
{
	if (self == NULL) {
		errno = EFAULT;
		return 1;
	}

	return self->_length == 0;
}

void
FreeStub(void *entry)
{
	/* Do nothing */
}

void
VectorSetDestroyEntry(Vector self, void (*fn)(void *))
{
	if (self == NULL)
		errno = EFAULT;
	else
		self->_free_entry = fn;
}

/*
 * Reverse range x..y inclusive.
 */
int
VectorReverseRange(Vector self, long x, long y)
{
	void *temp, **xp, **yp;

	if (self == NULL) {
		errno = EFAULT;
		return -1;
	}

	if (x < 0 || self->_length <= x)
		return -1;

	if (y < 0 || self->_length <= y)
		return -1;

	if (y < x)
		return -1;

	xp = &self->_base[x];
	yp = &self->_base[y];

	while (xp < yp) {
		temp = *xp;
		*xp++ = *yp;
		*yp-- = temp;
	}

	return 0;
}

/*
 * Move range start..finish inclusive before the given location.
 * Return -1 if the region to move is out of bounds or the target
 * falls within the region; 0 for success.
 *
 * (See Software Tools chapter 6.)
 */
int
VectorMoveRange(Vector self, long start, long finish, long to)
{
	if (self == NULL) {
		errno = EFAULT;
		return -1;
	}

	if (finish < start) {
		errno = EINVAL;
		return -1;
	}

	if (to < start) {
		(void) VectorReverseRange(self, to, start-1);
		(void) VectorReverseRange(self, start, finish);
		(void) VectorReverseRange(self, to, finish);
	} else if (finish < to && to <= self->_length) {
		(void) VectorReverseRange(self, start, finish);
		(void) VectorReverseRange(self, finish+1, to-1);
		(void) VectorReverseRange(self, start, to-1);
	}

	return 0;
}

int
VectorRemoveSome(Vector self, long index, long length)
{
	long i, j;
	Object obj;

	if (self == NULL) {
		errno = EFAULT;
		return -1;
	}

	if (self->_length < index + length) {
		errno = EINVAL;
		return -1;
	}

	for (i = index, j = index + length; i < j; i++) {
		if ((obj = self->_base[i]) != NULL) {
			/* Are we destroying objects or unknow types? */
			if (self->_free_entry == NULL)
				obj->destroy(obj);
			else
				(*self->_free_entry)(obj);
		}
	}

	/* Move vacant slots to end of array. */
	if (! VectorMoveRange(self, index, index + length - 1, self->_length))
		self->_length -= length;

	/* Maintain NULL terminated array of pointers. */
	self->_base[self->_length] = NULL;

	return 0;
}

void
VectorRemoveAll(Vector self)
{
	if (self != NULL)
		(void) VectorRemoveSome(self, 0, self->_length);
}

void
VectorDestroy(void *selfless)
{
	Vector self = selfless;

	if (self != NULL) {
		VectorRemoveAll(self);
		free(self->_base);
		free(self);
	}
}

int
VectorAdd(Vector self, void *data)
/*@modifies self->_base, self->_length, self->_size @*/
{
	void **bigger;

	if (self == NULL) {
		errno = EFAULT;
		return -1;
	}

	/*@-branchstate@*/
	if (self->_size <= self->_length) {
		/*@-usereleased -compdef -mustfreeonly@*/
		bigger = realloc(self->_base, sizeof (void *) * (self->_size + VECTOR_GROWTH + 1));
		if (bigger == NULL)
			return -1;
		/*@-usereleased =compdef =mustfreeonly*/

		self->_size += VECTOR_GROWTH;
		self->_base = bigger;
	}
	/*@=branchstate@*/

	/*@-nullstate -compmempass -keeptrans -mustfreeonly@*/
	self->_base[self->_length++] = data;
	/* Maintain the NULL pointer sentinel at end of pointer array. */
	self->_base[self->_length] = NULL;

	return 0;
	/*@=nullstate =compmempass =keeptrans -mustfreeonly@*/
}

int
VectorInsert(Vector self, long before, void *data)
{
	long last;

	if (self == NULL) {
		errno = EFAULT;
		return -1;
	}

	last = self->_length;
	if (VectorAdd(self, data))
		return -1;

	return VectorMoveRange(self, last, last, before);
}

void *
VectorGet(Vector self, long index)
{
	if (self == NULL) {
		errno = EFAULT;
		return NULL;
	}

	if (0 <= index && index < self->_length)
		return self->_base[index];

	if (-self->_length <= index && index < 0)
		return self->_base[self->_length + index];

	return NULL;
}

void *
VectorReplace(Vector self, long index, void *data)
{
	void *old = NULL;

	if (self == NULL) {
		errno = EFAULT;
		return NULL;
	}

	/*@-keeptrans@*/
	if (0 <= index && index < self->_length) {
		old = self->_base[index];
		self->_base[index] = data;
	} else if (-self->_length <= index && index < 0) {
		old = self->_base[self->_length + index];
		self->_base[self->_length + index] = data;
	}
	/*@=keeptrans@*/

	return old;
}

void
VectorSet(Vector self, long index, void *data)
{
	Object value;

	value = VectorReplace(self, index, data);

	if (value != NULL && value != data) {
		/*@-branchstate@*/
		if (self->_free_entry == NULL)
			value->destroy(value);
		else
			(*self->_free_entry)(value);
		/*@=branchstate@*/
	}
}

int
VectorRemove(Vector self, long index)
{
	return VectorRemoveSome(self, index, 1);
}

void
VectorSort(Vector self, int (*compare)(const void *a, const void *b))
{
	if (self == NULL)
		errno = EFAULT;
	else
		qsort(self->_base, (size_t) self->_length, sizeof (void *), compare);
}

void
VectorUniq(Vector self, int (*compare)(const void *a, const void *b))
{
	long i;
	void *curr = NULL;

	if (self != NULL) {
		for (i = 0; i < self->_length; i++) {
			if ((*compare)(&curr, &self->_base[i]) == 0)
				VectorRemove(self, i--);
			else
				curr = self->_base[i];
		}
	}
}

/**
 * @param self
 *	This object.
 *
 * @param function
 *	A call-back function that is give the current index and vector
 *	value to examine. This function should return 0 to stop walking,
 *	1 to continue walking, or -1 to delete the current index and
 *	and continue walking.
 *
 * @param data
 *	An obsecure data type passed to the callback function.
 *
 * @return
 *	Return 0 on early stop, 1 on end of walk, otherwise -1 on error.
 */
int
VectorWalk(Vector self, int (*function)(Vector self, long index, void *object, void *data), void *data)
{
	long i;

	if (self == NULL) {
		errno = EFAULT;
		return -1;
	}

	for (i = self->_length; 0 <= --i; ) {
		/*@-nullpass@*/
		switch ((*function)(self, i, self->_base[i], data)) {
		/*@=nullpass@*/
		case 0:
			return 0;
		case -1:
			(void) VectorRemoveSome(self, i, 1);
			break;
		}
	}

	return 1;
}

/**
 * @param self
 *	This object.
 *
 * @param function
 *	A call-back function that is give the current index and vector
 *	value to examine. This should function returns true or false.
 *	True to continue walking, false to stop walking.
 *
 * @param data
 *	An obsecure data type passed to the callback function.
 *
 * @return
 *	False if some element returns false; otherwise true if all elements
 *	returned true.
 */
int
VectorAll(Vector self, int (*function)(Vector self, long index, void *object, void *data), void *data)
{
	long i;

	if (self == NULL) {
		errno = EFAULT;
		return 0;
	}

	for (i = self->_length; 0 <= --i; ) {
		if ((*function)(self, i, self->_base[i], data) == 0)
			return 0;
	}

	return 1;
}

/**
 * @param self
 *	This object.
 *
 * @param function
 *	A call-back function that is give the current index and vector
 *	value to examine. This should function returns true or false.
 *	False to continue walking, true to stop walking.
 *
 * @param data
 *	An obsecure data type passed to the callback function.
 *
 * @return
 *	True if some element returns true; otherwise false if all elements
 *	returned false.
 */
int
VectorSome(Vector self, int (*function)(Vector self, long index, void *object, void *data), void *data)
{
	long i;

	if (self == NULL) {
		errno = EFAULT;
		return 0;
	}

	for (i = self->_length; 0 <= --i; ) {
		if ((*function)(self, i, self->_base[i], data) == 1)
			return 1;
	}

	return 0;
}

/***********************************************************************
 *** Class methods
 ***********************************************************************/

Vector
VectorCreate(long capacity)
{
	Vector self;

	if (capacity < 1)
		capacity = 1;

	if ((self = malloc(sizeof (*self))) == NULL)
		return NULL;

	/* Allocate an extra element so that the vector can
	 * maintain a null pointer at the end. The vector is
	 * an array of pointers, so a null pointer at the end
	 * can be used for an end condition when the vector
	 * is iterated over.
	 */
	self->_base = calloc((size_t)(capacity + 1), sizeof (void *));
	if (self->_base == NULL) {
		free(self);
		return NULL;
	}

	self->_free_entry = FreeStub;
	self->_size = capacity;
	self->_length = 0;

	return self;
}

/***********************************************************************
 *** Test
 ***********************************************************************/

#ifdef TEST

#include <stdio.h>
#include <com/snert/lib/version.h>
#include <com/snert/lib/type/Integer.h>

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

void
dump(Vector a)
{
	int i;
	Integer x;

	for (i = 0; i < VectorLength(a); i++) {
		x = VectorGet(a, i);
		printf("get(%d) = %ld\n", i, x->value);
	}
}

int
main(int argc, char **argv)
{
	Integer x, y;
	Vector a;

	printf("\n--Vector--\n");

	printf("create dynamic object");
	isNotNull((a = VectorCreate(0)));

	printf("destroy dynamic object\n");
	VectorDestroy(a);

	printf("\ncreate vector a, capacity 10");
	isNotNull((a = VectorCreate(10)));

	printf("isEmpty=%d...%s\n", VectorIsEmpty(a), VectorIsEmpty(a) ? "OK" : "FAIL");
	printf("size=%ld...%s\n", VectorLength(a), VectorLength(a) == 0 ? "OK" : "FAIL");
	printf("capacity=%ld...%s\n", VectorCapacity(a), VectorCapacity(a) == 10 ? "OK" : "FAIL");

	printf("--add 5 Integers\n");
	VectorAdd(a, IntegerCreate(0));
	VectorAdd(a, IntegerCreate(1));
	VectorAdd(a, IntegerCreate(2));
	VectorAdd(a, IntegerCreate(3));
	VectorAdd(a, IntegerCreate(4));

	printf("size=%ld...%s\n", VectorLength(a), VectorLength(a) == 5 ? "OK" : "FAIL");
	printf("capacity=%ld ...%s\n", VectorCapacity(a), VectorCapacity(a) == 10 ? "OK" : "FAIL");

	printf("--add Integers just beyond capacity\n");
	VectorAdd(a, IntegerCreate(5));
	VectorAdd(a, IntegerCreate(6));
	VectorAdd(a, IntegerCreate(7));
	VectorAdd(a, IntegerCreate(8));
	VectorAdd(a, IntegerCreate(9));
	VectorAdd(a, IntegerCreate(10));

	printf("size=%ld...%s\n", VectorLength(a), VectorLength(a) == 11 ? "OK" : "FAIL");
	printf("capacity=%ld ...%s\n", VectorCapacity(a), VectorCapacity(a) == VECTOR_GROWTH + 10 ? "OK" : "FAIL");

	printf("--get/set replace same object\n");
	x = VectorGet(a, 5);
	printf("get(5) == 5...%s\n", x->value == 5  ? "OK" : "FAIL");

	x->value = 555;
	VectorSet(a, 5, x);
	printf("set(5, 555)...OK\n");
	y = VectorGet(a, 5);

	printf("get(5) == 555...%s\n", y->value == 555  ? "OK" : "FAIL");
	printf("x and y refer to same object...%s\n", x == y ?  "OK" : "FAIL");

	x->value = 5;
	x = VectorGet(a, 5);
	printf("get(5) == 5...%s\n", x->value == 5  ? "OK" : "FAIL");

	printf("--get/set replace different object\n");
	VectorSet(a, 5, IntegerCreate(5));
	printf("set(5, 5)...OK\n");
	y = VectorGet(a, 5);
	printf("get(5) == 5...%s\n", y->value == 5  ? "OK" : "FAIL");
	printf("x and y refer to different object...%s\n", x != y ?  "OK" : "FAIL");

	printf("--reverse all\n");
	VectorReverseRange(a, 0, VectorLength(a)-1);
	dump(a);

	printf("--reverse all\n");
	VectorReverseRange(a, 0, VectorLength(a)-1);
	dump(a);

	x = VectorGet(a, 2);
	printf("get(2) == 2...%s\n", x->value == 2  ? "OK" : "FAIL");

	printf("--removeSome\n");
	VectorRemoveSome(a, 1, 3);
	printf("size=%ld...%s\n", VectorLength(a), VectorLength(a) == 8 ? "OK" : "FAIL");
	printf("capacity=%ld ...%s\n", VectorCapacity(a), VectorCapacity(a) == VECTOR_GROWTH + 10 ? "OK" : "FAIL");
	dump(a);

	printf("--get & moveRange\n");
	x = VectorGet(a, 1);
	printf("get(1) == 4...%s\n", x->value == 4  ? "OK" : "FAIL");

	VectorMoveRange(a, 2, 6, 0);
	dump(a);

	x = VectorGet(a, 4);
	printf("get(4) == 9...%s\n", x->value == 9  ? "OK" : "FAIL");

	VectorMoveRange(a, 0, 4, 8);
	dump(a);

	x = VectorGet(a, 2);
	printf("get(2) == 2...%s\n", x->value == 10  ? "OK" : "FAIL");

	VectorMoveRange(a, 6, 7, 2);
	dump(a);

	x = VectorGet(a, 3);
	printf("get(3) == 9...%s\n", x->value == 9  ? "OK" : "FAIL");

	printf("--reverse all\n");
	VectorReverseRange(a, 0, VectorLength(a)-1);
	x = VectorGet(a, 2);
	dump(a);
	printf("get(2) == 5...%s\n", x->value == 5  ? "OK" : "FAIL");

	printf("--removeAll\n");
	VectorRemoveAll(a);
	printf("isEmpty=%d...%s\n", VectorIsEmpty(a), VectorIsEmpty(a) ? "OK" : "FAIL");
	printf("size=%ld...%s\n", VectorLength(a), VectorLength(a) == 0 ? "OK" : "FAIL");

	printf("destroy a\n");
	VectorDestroy(a);

	printf("\n--DONE--\n");

	return 0;
}

#endif /* TEST */
