/*
 * BigInt.h
 *
 * Copyright 2001, 2004 by Anthony Howe.  All rights reserved.
 */

#ifndef __com_snert_lib_util_BigInt_h__
#define __com_snert_lib_util_BigInt_h__		1

/**
 * A big integer.
 *
 * The value[] always represents an unsigned number, where the
 * most significant byte (value[0]) must be non-zero. The sign
 * field indicates whether the number is negative (-1), has
 * value zero (0), or is positive (1).
 *
 * @see java.math.BigInteger
 */
typedef struct bigint {
	int sign;
	int length;
	int capacity;
	unsigned char *value;
} *BigInt;

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Creation functions.
 */
extern BigInt BigIntCreate(int length);
extern BigInt BigIntFromLong(long value);
extern BigInt BigIntFromUnsignedLong(unsigned long value);
extern BigInt BigIntFromBytes(char *value, int length);
extern BigInt BigIntFromBigInt(BigInt number);

extern void BigIntDestroy(BigInt number);
extern void BigIntDestroyDivide(BigInt *result);

/*
 * Conversion functions.
 */
extern char *BigIntToString(BigInt number, int radix);
extern long BigIntToLong(BigInt number);

/*
 * Comparision functions.
 */
extern int BigIntCompareAbs(void *, void *);
extern int BigIntCompare(void *, void *);
extern int BigIntIsZero(BigInt number);

/*
 * Basic operators.
 *
 * Return a new BigInt on succes containing the result; otherwise NULL.
 *
 * The division functions return an array of consisting of the quotient
 * and remainder at index 0 and 1 respectively.
 */
extern BigInt* BigIntDivideByLong(BigInt dividend, long divisor);
extern BigInt* BigIntDivide(BigInt x, BigInt y);
extern BigInt BigIntMultiply(BigInt x, BigInt y);
extern BigInt BigIntSubtract(BigInt x, BigInt y);
extern BigInt BigIntAdd(BigInt x, BigInt y);
extern BigInt BigIntNegate(BigInt number);
extern BigInt BigIntAbs(BigInt number);

/*
 * Accumulator operations.
 *
 * Return 0 on success; otherwise -1.
 */
extern int BigIntAccDivideByLong(BigInt acc, long divisor);
extern int BigIntAccDivide(BigInt acc, BigInt divisor);
extern int BigIntAccMultiply(BigInt acc, BigInt y);
extern int BigIntAccSubtract(BigInt acc, BigInt y);
extern int BigIntAccAdd(BigInt acc, BigInt y);
extern int BigIntAccNegate(BigInt acc);
extern int BigIntAccAbs(BigInt acc);

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_util_BigInt_h__ */


