/*
 * JavaTime.c
 *
 * Copyright 2001, 2013 by Anthony Howe. All rights reserved.
 */

#include <stdlib.h>
#include <string.h>

#include <com/snert/lib/version.h>
#include <com/snert/lib/util/BigInt.h>
#include <com/snert/lib/util/JavaTime.h>

#ifdef DEBUG_MALLOC
# include <com/snert/lib/util/DebugMalloc.h>
#endif

JavaTime
JavaTimeCreate(unsigned long seconds)
{
	JavaTime t = malloc(sizeof *t);
	if (t == (JavaTime) 0)
		return (JavaTime) 0;

	JavaTimeSet(t, seconds);

	return t;
}

void
JavaTimeSet(JavaTime jt, unsigned long seconds)
{
	jt->seconds = seconds;
	jt->milliseconds = 0;
}

int
JavaTimeFillJavaLong(JavaTime jt, JavaLong bytes8)
{
	BigInt thousand, seconds, milliseconds;

	if (jt == (JavaTime) 0 || bytes8 == (JavaLong) 0)
		goto error0;

	if ((thousand = BigIntFromLong(1000)) == (BigInt) 0)
		goto error0;

	if ((seconds = BigIntFromUnsignedLong(jt->seconds)) == (BigInt) 0)
		goto error1;

	if ((milliseconds = BigIntFromUnsignedLong(jt->milliseconds)) == (BigInt) 0)
		goto error2;

	if (BigIntAccMultiply(seconds, thousand))
		goto error3;

	if (BigIntAccAdd(milliseconds, seconds))
		goto error3;

	if (8 < milliseconds->length)
		goto error3;

	memset(bytes8, 0, 8);
	memcpy(
		bytes8 + 8 - milliseconds->length,
		milliseconds->value, milliseconds->length
	);

	return 0;
error3:
	free(milliseconds);
error2:
	free(seconds);
error1:
	free(thousand);
error0:
	return -1;
}

JavaLong
JavaTimeToJavaLong(JavaTime jt)
{
	JavaLong bytes8;

	if ((bytes8 = calloc(1, 8)) == (JavaLong) 0)
		goto error0;

	if (JavaTimeFillJavaLong(jt, bytes8))
		goto error1;

	return bytes8;
error1:
	free(bytes8);
error0:
	return (JavaLong) 0;
}

JavaTime
JavaTimeFromJavaLong(JavaLong bytes8)
{
	JavaTime jt;
	BigInt *result, jlong;

	if ((jt = JavaTimeCreate(0)) == (JavaTime) 0)
		goto error0;

	if ((jlong = BigIntFromBytes((char *) bytes8, 8)) == (BigInt) 0)
		goto error1;

	if ((result = BigIntDivideByLong(jlong, 1000)) == (BigInt *) 0)
		goto error2;

	jt->seconds = (unsigned long) BigIntToLong(result[0]);
	jt->milliseconds = (unsigned) BigIntToLong(result[1]);

	free(result);
	free(jlong);

	return jt;
error2:
	free(jlong);
error1:
	free(jt);
error0:
	return (JavaTime) 0;
}

int
JavaTimeCompare(void *aa, void *bb)
{
	JavaTime a = (JavaTime) aa;
	JavaTime b = (JavaTime) bb;

	if (a->seconds < b->seconds)
		return -1;
	if (a->seconds > b->seconds)
		return 1;
	if (a->milliseconds < b->milliseconds)
		return -1;
	if (a->milliseconds > b->milliseconds)
		return 1;
	return 0;
}
