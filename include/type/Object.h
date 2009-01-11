/*
 * Object.h
 *
 * Copyright 2004 by Anthony Howe.  All rights reserved.
 */

#ifndef __com_snert_lib_type_Object_h__
#define __com_snert_lib_type_Object_h__	1

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The void * replaces Object * or struct object * since it truely accepts
 * all data pointer types, which is the basic idea behind an Object class.
 */
typedef /*@only@*//*@null@*/ void *(*CloneFunction)(void *self);
typedef int (*CompareFunction)(void *self, /*@null@*/ void *other);
typedef void (*DestoryFunction)(/*@only@*//*@null@*/ void *self);
typedef int (*EqualsFunction)(void *self, /*@null@*/ void *other);
typedef long (*HashcodeFunction)(void *self);

#define OBJECT_OBJECT							\
	/*@observer@*/ long 			objectSize;		\
	/*@observer@*/ long 			objectMethodCount;	\
	/*@observer@*/ const char *		objectName;		\
	/*@observer@*/ CloneFunction		clone;			\
	/*@observer@*/ CompareFunction		compare;		\
	/*@observer@*/ DestoryFunction		destroy;		\
	/*@observer@*/ EqualsFunction		equals;			\
	/*@observer@*/ HashcodeFunction		hashcode

typedef /*@abstract@*/ struct object {
	OBJECT_OBJECT;
} *Object;

/*@-exportlocal@*/

/**
 * Create a new and initialised Object.
 *
 * @return
 *	An object; otherwise null on error.
 */
extern /*@only@*//*@null@*/ Object ObjectCreate(void);

/**
 * Initialise an object. Typically used for static Objects.
 *
 * @param object
 *	A pointer to an object to initialise.
 */
extern void ObjectInit(/*@out@*/ void *self) /*@modifies *self, internalState@*/;

extern void ObjectDestroyNothing(/*@only@*//*@null@*/ void *self);

/*@=exportlocal@*/

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_type_Object_h__ */
