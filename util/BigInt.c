/*
 * BigInt.c
 *
 * Copyright 2001, 2013 by Anthony Howe.  All rights reserved.
 */

#include <stdlib.h>
#include <string.h>

#include <com/snert/lib/version.h>
#include <com/snert/lib/util/BigInt.h>

#ifdef DEBUG_MALLOC
# include <com/snert/lib/util/DebugMalloc.h>
#endif

/*
 * Remove leading zero bytes.
 */
static void
leftJustify(BigInt number)
{
	size_t i;

	for (i = 0; i < number->length; ++i)
		if (number->value[i] != 0)
			break;

	if (i < number->length) {
		memmove(number->value, &number->value[i], number->length - i);
		number->length -= i;
	}
}

/**
 * Create a BigInt with room for length bytes.
 *
 * @param length
 *	Number of bytes of storage
 */
BigInt
BigIntCreate(int length)
{
	BigInt number;

	if ((number = calloc(1, sizeof *number)) == (BigInt) 0)
		goto error0;

	if ((number->value = calloc(length, 1)) == (unsigned char *) 0)
		goto error1;

	number->length = length;
	number->capacity = length;

	return number;
error1:
	free(number);
error0:
	return (BigInt) 0;
}

/**
 * Destory a previously allocated BigInt.
 */
void
BigIntDestroy(BigInt number)
{
	if (number != (BigInt) 0) {
		free(number->value);
		free(number);
	}
}

/*
 * Convert a two's complement number into an unsigned value
 * with negative sign.
 */
static BigInt
changeRepresentation(BigInt number)
{
	BigInt replace;
	int i, j, extra;

	/* Find first non-sign (0xff) byte. The non-sign bytes we keep. */
	for (i = 0; i < number->length; ++i)
		if (number->value[i] != 0xff)
			break;

	/* Are remaining non-sign bytes 0x00? */
	for (j = i; j < number->length; ++j)
		if (number->value[i] != 0)
			break;

	/* Allocate new number. If the non-sign bytes were 0x00
	 * then we'll require an extra byte.
	 */
	extra = (number->length <= j);
	replace = BigIntCreate(number->length - i + extra);

	/* One's complement of non-sign bytes. */
	for (j = i; j < number->length; ++j)
		replace->value[j - i + extra] = (unsigned char) ~number->value[j];

	/* Add one to generate two's complement. */
	for (j = replace->length-1; 0 <= j; --j)
		if (++replace->value[j] == 0)
			break;

	replace->sign = -1;

	free(number);

	return replace;
}

/**
 * Create a BigInt from a two's complement array of bytes stored
 * most signiificant byte first.
 */
BigInt
BigIntFromBytes(char *value, int length)
{
	size_t i;
	BigInt number = BigIntCreate(length);

	if (number != (BigInt) 0) {
		memcpy(number->value, value, length);

		if (value[0] < 0)
			return changeRepresentation(number);

		for (i = 0; i < number->length; ++i)
			if (number->value[i] != 0)
				break;

		number->sign = (i < number->length);

		leftJustify(number);
	}

	return number;
}

/**
 * Create a copy of BigInt.
 */
BigInt
BigIntFromBigInt(BigInt orig)
{
	BigInt copy = BigIntCreate(orig->length);

	if (copy != (BigInt) 0) {
		copy->sign = orig->sign;
		copy->length = orig->length;
		copy->capacity = orig->capacity;
		memcpy(copy->value, orig->value, orig->length);
	}

	return copy;
}

/**
 * Create a BigInt from a C long.
 */
BigInt
BigIntFromUnsignedLong(unsigned long value)
{
	BigInt number = BigIntCreate(sizeof value);

	if (number != (BigInt) 0) {
		size_t i;

		number->sign = (0 < value);
		for (i = sizeof value; 0 < i; value >>= 8)
			number->value[--i] = (unsigned char)(value & 0xff);

		leftJustify(number);
	}

	return number;
}

/**
 * Create a BigInt from a C long.
 */
BigInt
BigIntFromLong(long value)
{
	int sign;
	BigInt number;

	if (value < 0) {
		sign = -1;
		value = -value;
	} else if (0 < value) {
		sign = 1;
	} else {
		sign = 0;
	}

	number = BigIntFromUnsignedLong((unsigned long) value);

	if (number != (BigInt) 0)
		number->sign = sign;

	return number;
}

/**
 * Return true is the number is zero.
 */
int
BigIntIsZero(BigInt number)
{
	return number->sign == 0;
}

/**
 * Return a new BigInt that is the absolute value of the original.
 */
BigInt
BigIntAbs(BigInt number)
{
	BigInt abs = BigIntFromBigInt(number);
	if (abs != (BigInt) 0)
		abs->sign = (abs->sign != 0);
	return abs;
}

/**
 * Return a new BigInt that is the negative value of the original.
 */
BigInt
BigIntNegate(BigInt number)
{
	BigInt neg = BigIntFromBigInt(number);
	if (neg != (BigInt) 0)
		neg->sign = -neg->sign;
	return neg;
}

/**
 * Return a C long consisting of the least significant bytes of the
 * number.
 */
long
BigIntToLong(BigInt number)
{
	unsigned i;
	long value = 0;

	for (i = 0; i < sizeof value && i < number->length; ++i) {
		value |= (long) number->value[number->length - 1 - i] << (8 * i);
	}

	return value;
}

/**
 * Return an allocated string representation of the number in
 * the given radix (2..36).
 */
char *
BigIntToString(BigInt number, int radix)
{
	int digitsPerByte;
	BigInt tmp, *result;
	char *string, *s, *t, ch;
	static char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";

	if (number->sign == 0)
		return strdup("0");

	if (radix < 2 || 36 < radix)
		goto error0;

	if (16 <= radix)
		digitsPerByte = 2;
	else if (7 <= radix)
		digitsPerByte = 3;
	else if (4 <= radix)
		digitsPerByte = 4;
	else if (3 <= radix)
		digitsPerByte = 6;
	else
		digitsPerByte = 8;

	t = string= malloc(digitsPerByte * number->length + 2);
	if (string == (char *) 0)
		goto error0;

	tmp = BigIntAbs(number);
	if (tmp == (BigInt) 0)
		goto error0;

	while (tmp->sign != 0) {
		result = BigIntDivideByLong(tmp, radix);
		*t++ = digits[result[1]->value[0]];

		free(tmp);
		tmp = result[0];
		free(result[1]);
		free(result);
	}
	free(tmp);

	if (number->sign < 0)
		*t++ = '-';

	*t = '\0';

	/* Reverse the string. */
	for (s = string, t--; s < t; s++, t--) {
		ch = *s;
		*s = *t;
		*t = ch;
	}

	return string;
error0:
	return (char *) 0;
}

/**
 * Compare the magnitude of two numbers ignoring their sign.
 *
 * @return
 *	-1 for abs(a) < abs(b),
 *	 0 for abs(a) == abs(b),
 *	 1 for abs(a) > abs(b).
 */
int
BigIntCompareAbs(void *aa, void *bb)
{
	int i;
	BigInt a = (BigInt) aa;
	BigInt b = (BigInt) bb;

	if (a->length < b->length)
		return -1;
	if (a->length > b->length)
		return 1;

	for (i = 0; i < a->length; ++i) {
		if (a->value[i] < b->value[i])
			return -1;
		if (a->value[i] > b->value[i])
			return 1;
	}

	return 0;
}

/**
 * Compare the magnitude of two numbers ignoring their sign.
 *
 * @return
 *	-1 for a < b, 0 for a == b, and 1 for a > b.
 */
int
BigIntCompare(void *aa, void *bb)
{
	BigInt a = (BigInt) aa;
	BigInt b = (BigInt) bb;

	if (a->sign != b->sign)
		return a->sign - b->sign;

	return BigIntCompareAbs(a, b);
}

/**
 * Divide a BigInt by a C long.
 *
 * @return
 *	An array of BigInt consisting of quotient and remainder
 *	at index 0 and 1 respectively.
 */
BigInt *
BigIntDivideByLong(BigInt dividend, long divisor)
{
	int i, divisorSign;
	BigInt quotient, *result;
	unsigned long remainder = 0;

	/* Divide by zero error. */
	if (divisor == 0)
		goto error0;

	result = calloc(2, sizeof (BigInt *));
	if (result == (BigInt *) 0)
		goto error0;

	if (BigIntIsZero(dividend)) {
		result[0] = BigIntFromBigInt(dividend);
		if (result[0] == (BigInt) 0)
			goto error1;

		result[1] = BigIntFromLong(0);
		if (result[1] == (BigInt) 0)
			goto error2;

		return result;
	}

	result[0] = BigIntFromLong(divisor);
	i = BigIntCompare(dividend, result[0]);
	free(result[0]);

	if (i < 0) {
		/* dividend < divisor */
		result[0] = BigIntFromLong(0);
		if (result[0] == (BigInt) 0)
			goto error1;

		result[1] = BigIntFromBigInt(dividend);
		if (result[1] == (BigInt) 0)
			goto error2;

		return result;
	} else if (i == 0) {
		/* dividend == divisor */
		result[0] = BigIntFromLong(1);
		if (result[0] == (BigInt) 0)
			goto error1;

		result[1] = BigIntFromLong(0);
		if (result[1] == (BigInt) 0)
			goto error2;

		return result;
	}

	result[0] = quotient = BigIntCreate(dividend->length);
	if (quotient == (BigInt) 0)
		goto error1;

	if (divisor < 0) {
		divisorSign = -1;
		divisor = -divisor;
	} else {
		divisorSign = 1;
	}

	for (i = 0; i < dividend->length; ++i) {
		remainder = (remainder << 8) | dividend->value[i];
		if ((unsigned long) divisor <= remainder) {
			quotient->value[i] = (unsigned char) (remainder / divisor);
			remainder %= divisor;
		} else {
			quotient->value[i] = 0;
		}
	}

	quotient->sign = dividend->sign / divisorSign;

	leftJustify(quotient);

	result[1] = BigIntFromLong(remainder);
	if (result[1] == (BigInt) 0)
		goto error2;

	return result;
error2:
	free(result[0]);
error1:
	free(result);
error0:
	return (BigInt *) 0;
}

/**
 *
 */
BigInt *
BigIntDivide(BigInt dividend, BigInt divisor)
{
	return (BigInt *) 0;
}

/**
 * Destroy the result of a BigIntDivde.
 */
void
BigIntDestroyDivide(BigInt *result)
{
	if (result != (BigInt *) 0) {
		BigIntDestroy(result[1]);
		BigIntDestroy(result[0]);
		free(result);
	}
}

/**
 * Return the result of multiplying x by y.
 */
BigInt
BigIntMultiply(BigInt xx, BigInt yy)
{
	BigInt product;
	int ix, iy, ip;
	unsigned char *x, *y, *p;

	if (xx->sign == 0 || yy->sign == 0)
		return BigIntCreate(0);

	product = BigIntCreate(xx->length + yy->length);
	if (product == (BigInt) 0)
		return (BigInt) 0;

	p = product->value;
	x = xx->value;
	y = yy->value;

	for (ix = xx->length - 1; 0 <= ix; --ix) {
		long carry = 0;
		for (iy = yy->length - 1, ip = iy+ix+1; 0 <= iy; --iy, --ip) {
			long acc = x[ix] * y[iy] + p[ip] + carry;
			p[ip] = (unsigned char) acc;
			carry = acc >> 8;
		}
		p[ix] = (unsigned char) carry;
	}

	leftJustify(product);

	product->sign = xx->sign * yy->sign;

	return product;
}

/*
 * Return the result of abs(x) + abs(y).
 */
static BigInt
add(BigInt xx, BigInt yy)
{
	BigInt zz;
	int carry, sum;
	unsigned char *x, *y, *z;

	/* Swap if x is shorter. */
	if (xx->length < yy->length) {
		zz = xx;
		xx = yy;
		yy = zz;
	}

	/* Allocate sum with space for carry. */
	zz = BigIntCreate(xx->length+1);
	if (zz == (BigInt) 0)
		return (BigInt) 0;

	x = &xx->value[xx->length];
	y = &yy->value[yy->length];
	z = &zz->value[zz->length];

	sum = 0;

	/* Add common part of both numbers. */
	while (yy->value < y) {
		sum = *--x + *--y + (sum >> 8);
		*--z = (unsigned char) sum;
	}

	/* Add carry to remainder of longer number. */
	carry = ((sum >> 8) != 0);
	while (xx->value < x && carry) {
		carry = (*--z = (unsigned char) (*--x + 1)) == 0;
	}

	/* Copy remainder of longer number. */
	while (xx->value < x)
		*--z = *--x;

	/* Carry into extra space? */
	if (carry)
		*--z = 1;

	leftJustify(zz);

	return zz;
}

/*
 * Return the result of abs(x) - abs(y). The value of x
 * must be greater than that of y.
 */
static BigInt
subtract(BigInt xx, BigInt yy)
{
	BigInt zz;
	int borrow, diff;
	unsigned char *x, *y, *z;

	/* Allocate sum with space for carry. */
	zz = BigIntCreate(xx->length+1);
	if (zz == (BigInt) 0)
		return (BigInt) 0;

	x = &xx->value[xx->length];
	y = &yy->value[yy->length];
	z = &zz->value[zz->length];

	diff = 0;

	/* Subtract common parts of both numbers. */
	while (yy->value < y) {
		diff = *--x - *--y + (diff >> 8);
		*--z = (unsigned char) diff;
	}

	borrow = ((diff >> 8) != 0);
	while (xx->value < x && borrow)
		borrow = ((*--z = (unsigned char) (*--x - 1)) == (unsigned char) -1);

	/* Copy remainder of longer number. */
	while (xx->value < x)
		*--z = *--x;

	leftJustify(zz);

	return zz;
}

/**
 * Return the result of x + y.
 */
BigInt
BigIntAdd(BigInt xx, BigInt yy)
{
	int cmp;
	BigInt sum;

	if (xx->sign == 0)
		return BigIntFromBigInt(yy);

	if (yy->sign == 0)
		return BigIntFromBigInt(xx);

	if (xx->sign == yy->sign) {
		sum = add(xx, yy);
		sum->sign = xx->sign;
		return sum;
	}

	if (0 <= (cmp = BigIntCompareAbs(xx, yy)))
		sum = subtract(xx, yy);
	else
		sum = subtract(yy, xx);

	if (sum != (BigInt) 0)
		sum->sign = cmp * xx->sign;

	return sum;
}

/**
 * Return the result of x - y.
 */
BigInt
BigIntSubtract(BigInt xx, BigInt yy)
{
	int cmp;
	BigInt diff;

	if (xx->sign == 0)
		return BigIntNegate(yy);

	if (yy->sign == 0)
		return BigIntFromBigInt(xx);

	if (xx->sign != yy->sign) {
		diff = add(xx, yy);
		diff->sign = xx->sign;
		return diff;
	}

	if (0 <= (cmp = BigIntCompareAbs(xx, yy)))
		diff = subtract(xx, yy);
	else
		diff = subtract(yy, xx);

	if (diff != (BigInt) 0)
		diff->sign = cmp * xx->sign;

	return diff;
}

static int
UpdateAccumulator(BigInt acc, BigInt result)
{
	unsigned char *value = acc->value;

	if (result == (BigInt) 0)
		return -1;

	*acc = *result;
	result->value = value;
	free(result);

	return 0;
}

int
BigIntAccDivideByLong(BigInt acc, long divisor)
{
	BigInt *result = BigIntDivideByLong(acc, divisor);

	if (result == (BigInt *) 0)
		return -1;

	UpdateAccumulator(acc, result[0]);
	free(result[1]);
	free(result);

	return 0;
}

int
BigIntAccMultiply(BigInt acc, BigInt y)
{
	return UpdateAccumulator(acc, BigIntMultiply(acc, y));
}

int
BigIntAccSubtract(BigInt acc, BigInt y)
{
	return UpdateAccumulator(acc, BigIntSubtract(acc, y));
}

int
BigIntAccAdd(BigInt acc, BigInt y)
{
	return UpdateAccumulator(acc, BigIntAdd(acc, y));
}

int
BigIntAccNegate(BigInt acc)
{
	return UpdateAccumulator(acc, BigIntNegate(acc));
}

int
BigIntAccAbs(BigInt acc)
{
	return UpdateAccumulator(acc, BigIntAbs(acc));
}


#ifdef TEST

#include <stdio.h>


static void
answer(BigInt a, BigInt b, BigInt c, char *op, int radix)
{
	char *number;

	number = BigIntToString(a, radix);
	printf(number);
	free(number);

	printf(" %s ", op);

	number = BigIntToString(b, radix);
	printf(number);
	free(number);

	printf(" = ");

	number = BigIntToString(c, radix);
	printf(number);
	free(number);

	printf("\n");
}

int
main(int argc, char **argv)
{
	char number[] = { 0x00, 0xe9, 0xa0, 0x2b, 0xf5, 0xc2 };
	BigInt a, b, c, *result;

	a = BigIntFromLong(0x7fff);
	b = BigIntFromLong(1000);
	c = BigIntMultiply(a, b);
	answer(a, b, c, "*", 10);
	BigIntDestroy(c);
	BigIntDestroy(b);
	BigIntDestroy(a);

	a = BigIntFromLong(-123);
	b = BigIntFromLong(37);
	c = BigIntMultiply(a, b);
	answer(a, b, c, "*", 10);
	BigIntDestroy(c);
	BigIntDestroy(b);
	BigIntDestroy(a);

	a = BigIntFromBytes(number, sizeof number);
	b = BigIntFromLong(1000);
	result = BigIntDivideByLong(a, 1000);
	answer(a, b, result[0], "/", 10);
	printf("quotient = %lx\n", BigIntToLong(result[0]));
	printf("remainder = %ld\n", BigIntToLong(result[1]));
	BigIntDestroyDivide(result);
	BigIntDestroy(b);
	BigIntDestroy(a);

	a = BigIntFromLong(0xffffffff);
	b = BigIntFromLong(1);

	c = BigIntAdd(a, b);
	answer(a, b, c, "+", 10);
	BigIntDestroy(c);

	c = BigIntAdd(b, a);
	answer(b, a, c, "+", 10);
	BigIntDestroy(c);

	c = BigIntSubtract(a, b);
	answer(a, b, c, "-", 10);
	BigIntDestroy(c);

	c = BigIntSubtract(b, a);
	answer(b, a, c, "-", 10);
	BigIntDestroy(c);

	BigIntDestroy(b);
	BigIntDestroy(a);

	a = BigIntFromLong(-0x7fffL);
	b = BigIntFromLong( 0x7fff00L);

	c = BigIntAdd(a, b);
	answer(a, b, c, "+", 10);
	BigIntDestroy(c);

	c = BigIntAdd(b, a);
	answer(b, a, c, "+", 10);
	BigIntDestroy(c);

	c = BigIntSubtract(a, b);
	answer(a, b, c, "-", 10);
	BigIntDestroy(c);

	c = BigIntSubtract(b, a);
	answer(b, a, c, "-", 10);
	BigIntDestroy(c);

	BigIntDestroy(b);
	BigIntDestroy(a);

	a = BigIntFromLong(-0x7fffL);
	b = BigIntFromLong(-0x7fff00L);

	c = BigIntAdd(a, b);
	answer(a, b, c, "+", 10);
	BigIntDestroy(c);

	c = BigIntAdd(b, a);
	answer(b, a, c, "+", 10);
	BigIntDestroy(c);

	c = BigIntSubtract(a, b);
	answer(a, b, c, "-", 10);
	BigIntDestroy(c);

	c = BigIntSubtract(b, a);
	answer(b, a, c, "-", 10);
	BigIntDestroy(c);

	BigIntDestroy(b);
	BigIntDestroy(a);

	return 0;
}

#endif
