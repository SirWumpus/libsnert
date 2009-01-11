/*
 * Data.h
 *
 * Copyright 2004 by Anthony Howe.  All rights reserved.
 */

#ifndef __com_snert_lib_type_Data_h__
#define __com_snert_lib_type_Data_h__	1

#ifdef __cplusplus
extern "C" {
#endif

#include <com/snert/lib/type/Object.h>

#define OBJECT_DATA					\
	OBJECT_OBJECT;					\
	/*@exposed@*/ unsigned char *(*base)(struct data *self);	\
	long (*length)(struct data *self);		\
	/*@null@*/ unsigned char *_base;		\
	long _length

typedef /*@abstract@*/ struct data {
	OBJECT_DATA;
} *Data;

/*@-exportlocal@*/

/**
 * Create a new and initialised Data object.
 *
 * @return
 *	A Data object; otherwise null on error.
 */
extern /*@only@*//*@null@*/ Data DataCreate(void);

/**
 * Create a new and initialised Data object.
 *
 * @param bytes
 *	A previously allocated buffer of bytes that the Data object
 *	will take responsibility of.
 *
 * @param length
 *	The number of bytes in the buffer.
 *
 * @return
 *	A Data object; otherwise null on error.
 */
extern /*@only@*//*@null@*/ Data DataCreateWithBytes(unsigned char *bytes, long length);

/**
 * Create a new and initialised Data object.
 *
 * @param bytes
 *	A buffer of bytes from which to initialise the Data object.
 *	The bytes are copied from the source buffer.
 *
 * @param length
 *	The number of bytes in the buffer.
 *
 * @return
 *	A Data object; otherwise null on error.
 */
extern /*@only@*//*@null@*/ Data DataCreateCopyBytes(unsigned char *bytes, long length);

/**
 * Create a new and initialised Data object.
 *
 * @param string
 *	A C string from which to initialise the Data object. The
 *	Data object will take responsibility for the string.
 *
 * @return
 *	A Data object; otherwise null on error.
 */
extern/*@only@*//*@null@*/ Data DataCreateWithString(const char *string);

/**
 * Create a new and initialised Data object.
 *
 * @param string
 *	A C string from which to initialise the Data object. The
 *	bytes are copied from the source string.
 *
 * @return
 *	A Data object; otherwise null on error.
 */
extern/*@only@*//*@null@*/ Data DataCreateCopyString(const char *string);

/**
 * Initialise an object. Typically used for static Objects.
 *
 * @param data
 *	A pointer to a Data object to initialise.
 */
extern void DataInit(/*@out@*/ Data self) /*@modifies self, internalState@*/;

/**
 * Initialise an object. Typically used for static Objects.
 *
 * @param data
 *	A pointer to a Data object to initialise.
 *
 * @param bytes
 *	A buffer of bytes from which to initialise the Data object.
 *
 * @param length
 *	The number of bytes in the buffer.
 */
extern void DataInitWithBytes(/*@out@*/ Data self, /*@keep@*/ unsigned char *bytes, long length);

/*@=exportlocal@*/

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_type_Data_h__ */
