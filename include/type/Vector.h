/*
 * Vector.h
 *
 * Copyright 2001, 2006 by Anthony Howe.  All rights reserved.
 */

#ifndef __com_snert_lib_type_Vector_h__
#define __com_snert_lib_type_Vector_h__	1

#ifdef __cplusplus
extern "C" {
#endif

#include <com/snert/lib/type/Object.h>

typedef struct vector {
	OBJECT_OBJECT;

	int (*add)(struct vector *self, /*@keep@*/ void *object) /*@modifies self@*/;
	int (*insert)(struct vector *self, long before, /*@keep@*/ void *object) /*@modifies self@*/;
	/*@observer@*//*@notnull@*/ void **(*base)(struct vector *self);
	long (*capacity)(struct vector *self);
	/*@exposed@*//*@null@*/ void *(*get)(struct vector *self, long index);
	/*@falsewhennull@*/ int (*isEmpty)(struct vector *self);
	int (*moveRange)(struct vector *self, long start, long finish, long to);
	int (*remove)(struct vector *self, long index);
	void (*removeAll)(/*@null@*/ struct vector *self);
	int (*removeSome)(struct vector *self, long index, long length);
	/*@exposed@*/ void *(*replace)(struct vector *self, long index, /*@keep@*/ void *object);
	int (*reverseRange)(struct vector *self, long start, long finish);
	void (*set)(struct vector *self, long index, /*@keep@*/ void *object);
	long (*size)(struct vector *self);
	void (*sort)(struct vector *self, int (*compare)(const void *a, const void *b));

	/**
	 * @param self
	 *	This object.
	 *
	 * @param function
	 *	A call-back function that is give the vector, current index, and the
	 *	object value at the current index. This should function returns 0
	 *	to stop walking, 1 to continue walking, or -1 to delete the current
	 *	index and continue walking.
	 *
	 * @param data
	 *	An obsecure data type passed to the callback function.
	 *
	 * @return
	 *	Return 0 on success, otherwise -1 on error.
	 */
	int (*walk)(struct vector *self, int (*function)(struct vector *self, long index, void *object, void *data), void *data);

	/**
	 * @param self
	 *	This object.
	 *
	 * @param function
	 *	A call-back function that is give the vector, current index, and
	 *	the object value at the curent index. This function returns true
	 *	to continue walking, false to stop walking.
	 *
	 * @param data
	 *	An obsecure data type passed to the callback function.
	 *
	 * @return
	 *	False if some element returns false; otherwise true if all elements
	 *	returned true.
	 */
	int (*all)(struct vector *self, int (*function)(struct vector *self, long index, void *object, void *data), void *data);

	/**
	 * @param self
	 *	This object.
	 *
	 * @param function
	 *	A call-back function that is give the vector, current index, and
	 *	the object value at the curent index. This function returns true
	 *	to stop walking, false to continue walking.
	 *
	 * @param data
	 *	An obsecure data type passed to the callback function.
	 *
	 * @return
	 *	True if some element returns true; otherwise false if all elements
	 *	returned false.
	 */
	int (*some)(struct vector *self, int (*function)(struct vector *self, long index, void *object, void *data), void *data);

	/* A Vector allows for unknown C objects, in which case this method
	 * sets the function used to destroy the object. If NULL, then its
	 * assumed the entries are objects and have a destroy method.
	 */
	void (*setDestroyEntry)(struct vector *self, /*@null@*/ void (*destroy)(/*@only@*//*@null@*/ void *entry));

	/*
	 * Private
	 */
	/*@null@*/ void (*_destroyEntry)(void *);
	long _capacity;
	long _length;
	/*@notnull@*/ Object *_base;
} *Vector;

/*@-exportlocal@*/

/**
 * Create a new and initialised Vector.
 *
 * @param capacity
 *	The initial capacity of the Vector.
 *
 * @return
 *	A new Vector object; otherwise null on error.
 */
extern /*@only@*//*@null@*/ Vector VectorCreate(long capacity);

/**
 * Convience function that replaces free() and does nothing.
 * Used with VectorSetDestroyEntry() to specify a destructor
 * that does not destroy vector entries.
 */
extern void FreeStub(void *);

/*
 * Backwards compatibility with previous API.
 */
extern int VectorAdd(Vector self, /*@keep@*/ void *data) /*@modifies self@*/;
extern /*@observer@*//*@notnull@*/ void **VectorBase(Vector self);
extern long VectorCapacity(Vector self);
extern void VectorDestroy(/*@only@*//*@null@*/ void *self);
extern /*@exposed@*//*@null@*/ void *VectorGet(Vector self, long index);
extern int VectorInsert(Vector self, long before, /*@keep@*/ void *data) /*@modifies self@*/;
extern /*@falsewhennull@*/ int VectorIsEmpty(Vector self);
extern int VectorMoveRange(Vector self, long start, long finish, long to);
extern int VectorRemove(Vector self, long index);
extern void VectorRemoveAll(/*@null@*/ Vector self);
extern int VectorRemoveSome(Vector self, long index, long length);
extern /*@only@*//*@null@*/ void *VectorReplace(Vector self, long index, /*@keep@*/ void *data);
extern int VectorReverseRange(Vector self, long x, long y);
extern void VectorSet(Vector self, long index, /*@keep@*/ void *data);
extern void VectorSetDestroyEntry(Vector self, /*@null@*/ void (*destroy)(void *entry));
extern long VectorLength(Vector self);
extern void VectorSort(Vector self, int (*compare)(const void *a, const void *b));
extern int VectorAll(Vector self, int (*function)(Vector self, long index, void *object, void *data), void *data);
extern int VectorSome(Vector self, int (*function)(Vector self, long index, void *object, void *data), void *data);
extern int VectorWalk(Vector self, int (*function)(Vector self, long index, void *object, void *data), void *data);

/*@=exportlocal@*/

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_type_Vector_h__ */

