/*
 * Object2.h
 *
 * Copyright 2004, 2006 by Anthony Howe.  All rights reserved.
 *
 * Simplified & reduced base Object class.
 */

#ifndef __com_snert_lib_object_Object2_h__
#define __com_snert_lib_object_Object2_h__		1

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************************
 *** Object
 ***********************************************************************/

#include <stdarg.h>
#include <stdlib.h>

/*
 * The ``void *'' replaces ``object *'' or ``struct object *'' since it
 * truely accepts all data pointer types, which is the basic idea behind
 * an ``object'' class.
 *
 * A previous version of object contained too much overhead for what
 * should be the smallest and simplest expression of an object. An
 * object must be able to be initialise an instance of itself and
 * destroy an instance of itself. Anything more should be part of
 * an extended subclass.
 *
 * In map and vector objects, the most important thing to aid with
 * memory management is to be able to dispose of any held objects when
 * a map or vector is destroyed. Therefore the destroy() method should
 * be the first and foremost member of any data structure. This simple
 * rule, makes it simple for any complex C structure to be stored within
 * a map or vector without having to be an object returned by
 * objectCreate().
 */
#define CLASS_OBJECT							\
	void (*destroy)(void *self);					\
	void *(*create)(void *self, va_list *args);			\
	size_t size_of

typedef struct object {
	CLASS_OBJECT;
} object;

extern const void *Object;

/***********************************************************************
 *** Object Management
 ***********************************************************************/

/**
 * @param type
 *	A pointer to an ``object'' descriptor to be created.
 *
 * @param ...
 *	Arguments to be passed to the object's initialisation method.
 *
 * @return
 *	A pointer to a new allocated object of type, else NULL on
 *	error. Its the caller's responsiblity to pass this object
 *	to objectDestroy() when the object is no longer required.
 */
extern void *objectCreate(const void *type, ...);

/**
 * @param self
 *	A pointer to an ``object'', previously created by
 *	objectCreate(), to be deallocated.
 */
extern void objectDestroy(void *self);

/**
 * Initialise a static or stack allocated object of a given type. Its
 * the caller's responsiblity to pass this object to objectFini() when
 * the object is no longer required.
 *
 * @param type
 *	A pointer to an ``object'' descriptor to be initialised.
 *
 * @param self
 *	A pointer to a static or stack allocated ``object''.
 *
 * @param ...
 *	Arguments to be passed to the object's initialisation method.
 *
 * @return
 *	Zero on success, otherwise -1 on error.
 */
extern int objectInit(const void *type, void *self, ...);

/**
 * Discard a static or stack allocated object's resources.
 *
 * @param self
 *	A pointer to an ``object'', previously initialised by
 *	objectInit(), to be destroyed.
 */
extern void objectFini(void *self);

/***********************************************************************
 *** Atom Interface
 ***********************************************************************/

/*
 * An ``atom'' is an interface for the more common object orient
 * types found in Java or Ruby that hold data, get passed around,
 * copied, and compared with other objects.
 *
 * Concerning clone() method:
 *
 *  a)	x->clone() != x, ie. different object pointers.
 *
 *  b)  x->clone()->compare(x) is typically true, but not a requirement.
 *
 * Concerning compare() method:
 *
 *  a)	x->compare(y) returns 0 if x and y are the same type and "equal"
 *	in size and value.
 *
 *  b)	For arthimetic types x < y returns -1 and x > y	returns 1.
 *
 *  c)	For data types like a memory buffer, x shorter than y returns -1
 *	and x longer the y returns 1, otherwise if the buffers are the
 *	same size, then return the difference between the first two
 *	differing bytes.
 */
#define CLASS_ATOM							\
	CLASS_OBJECT;							\
	void *(*clone)(void *self);					\
	int (*compare)(void *self, void *other);			\
	unsigned long (*hashcode)(void *self)

/***********************************************************************
 *** Integer
 ***********************************************************************/

#define CLASS_INTEGER							\
	CLASS_ATOM;							\
	long value

typedef struct integer {
	CLASS_INTEGER;
} integer;

extern const void *Integer;

/***********************************************************************
 *** Real
 ***********************************************************************/

#define CLASS_REAL							\
	CLASS_ATOM;							\
	double value

typedef struct real {
	CLASS_REAL;
} real;

extern const void *Real;

/***********************************************************************
 *** Data (binary or string)
 ***********************************************************************/

#define CLASS_DATA							\
	CLASS_ATOM;							\
	int (*set)(void *self, unsigned char *data, unsigned long size);\
	unsigned long size;						\
	unsigned char *data

typedef struct data {
	CLASS_DATA;
} data;

extern const void *Data;

/***********************************************************************
 *** Map Interface
 ***********************************************************************/

#define CLASS_MAP							\
	CLASS_ATOM;							\
	void *(*get)(void *self, void *key);				\
	int (*put)(void *self, void *key, void *value);			\
	int (*remove)(void *self, void *key);				\
	unsigned long size;						\
	void **base

#define MAP_OK			0
#define MAP_ERROR		(-1)
#define MAP_NOT_FOUND		(-2)

/***********************************************************************
 *** Hash Map
 ***********************************************************************/

#define CLASS_HASH							\
	CLASS_MAP;							\

typedef struct hashmap {
	CLASS_MAP;
} hashmap;

extern const void *Hashmap;

/***********************************************************************
 *** Hash Map File
 ***********************************************************************/

#define CLASS_HASHFILE							\
	CLASS_HASH;							\
	char *filepath

typedef struct hashfile {
	CLASS_HASHFILE;
} hashfile;

extern const void *Hashfile;

/***********************************************************************
 *** List Interface
 ***********************************************************************/

#define CLASS_LIST							\
	CLASS_ATOM;							\
	int (*add)(void *self, void *value);				\
	void *(*get)(void *self, unsigned long index);			\
	void *(*set)(void *self, unsigned long index, void *value);	\
	void *(*remove)(void *self, unsigned long index);		\
	void (*removeAll)(void *self);					\
	unsigned long length;						\
	unsigned long size;						\
	void **base

/***********************************************************************
 *** Vector
 ***********************************************************************/

#define CLASS_VECTOR							\
	CLASS_LIST;							\
	int (*insert)(void *self, unsigned long before, void *value);	\
	void (*sort)(void *self, int (*compare)(const void *a, const void *b))

typedef struct vector {
	CLASS_VECTOR;
} vector;

extern const void *Vector;

/***********************************************************************
 ***
 ***********************************************************************/

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_object_Object2_h__ */
