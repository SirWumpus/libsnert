/*
 * Decimal.h
 *
 * Copyright 2004 by Anthony Howe.  All rights reserved.
 */

#ifndef __com_snert_lib_type_Decimal_h__
#define __com_snert_lib_type_Decimal_h__	1

#ifdef __cplusplus
extern "C" {
#endif

#include <com/snert/lib/type/Object.h>

#define OBJECT_DECIMAL						\
	OBJECT_OBJECT;						\
	double value

typedef struct decimal {
	OBJECT_DECIMAL;
} *Decimal;

/*@-exportlocal@*/

/**
 * Create a new and initialised Decimal object.
 *
 * @param value
 *	A double value.
 *
 * @return
 *	A Decimal object; otherwise null on error.
 */
extern /*@only@*//*@null@*/ Decimal DecimalCreate(double value);

/**
 * Create a new and initialised Decimal object.
 *
 * @param string
 *	A C string representing a fractional decimal number.
 *
 * @return
 *	A Decimal object; otherwise null on error.
 */
extern /*@only@*//*@null@*/ Decimal DecimalCreateFromString(const char *string);

/**
 * Initialise an object. Typically used for static Objects.
 *
 * @param data
 *	A pointer to a Decimal object to initialise.
 */
extern void DecimalInit(/*@out@*/ Decimal self);

/*@=exportlocal@*/

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_type_Decimal_h__ */
