/*
 * Integer.h
 *
 * Copyright 2004 by Anthony Howe.  All rights reserved.
 */

#ifndef __com_snert_lib_type_Integer_h__
#define __com_snert_lib_type_Integer_h__	1

#ifdef __cplusplus
extern "C" {
#endif

#include <com/snert/lib/type/Object.h>

#define OBJECT_INTEGER					\
	OBJECT_OBJECT;					\
	long value

typedef struct integer {
	OBJECT_INTEGER;
} *Integer;

/*@-exportlocal@*/

/**
 * Create a new and initialised Integer object.
 *
 * @param value
 *	A long value.
 *
 * @return
 *	A Integer object; otherwise null on error.
 */
extern /*@only@*//*@null@*/ Integer IntegerCreate(long value);

/**
 * Create a new and initialised Integer object.
 *
 * @param string
 *	A C string representing a integer number in decimal, octal, or hex.
 *
 * @return
 *	A Integer object; otherwise null on error.
 */
extern /*@only@*//*@null@*/ Integer IntegerCreateFromString(const char *string);

/**
 * Initialise an object. Typically used for static Objects.
 *
 * @param data
 *	A pointer to a Integer object to initialise.
 */
extern void IntegerInit(/*@out@*/ Integer self);

/*@=exportlocal@*/

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_type_Integer_h__ */
