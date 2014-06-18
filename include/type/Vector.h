/*
 * Vector.h
 *
 * Copyright 2001, 2011 by Anthony Howe.  All rights reserved.
 */

#ifndef __com_snert_lib_type_Vector_h__
#define __com_snert_lib_type_Vector_h__	1

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vector {
	long _size;
	long _length;
	void **_base;
	void (*_free_entry)(void *);
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
extern void VectorUniq(Vector self, int (*compare)(const void *a, const void *b));
extern int VectorAll(Vector self, int (*function)(Vector self, long index, void *object, void *data), void *data);
extern int VectorSome(Vector self, int (*function)(Vector self, long index, void *object, void *data), void *data);
extern int VectorWalk(Vector self, int (*function)(Vector self, long index, void *object, void *data), void *data);

/*@=exportlocal@*/

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_type_Vector_h__ */

