/*
 * Integer.c
 *
 * Copyright 2004, 2013 by Anthony Howe.  All rights reserved.
 */

#include <stdlib.h>
#include <string.h>

#include <com/snert/lib/version.h>
#include <com/snert/lib/crc/Crc.h>
#include <com/snert/lib/type/Integer.h>

#ifdef DEBUG_MALLOC
# include <com/snert/lib/util/DebugMalloc.h>
#endif

#define REF_INTEGER(v)		((Integer)(v))

#define DYNAMIC_OBJECT		1
#define DYNAMIC_CONTENT		2

/***********************************************************************
 *** Instance methods
 ***********************************************************************/

/*@only@*//*@null@*/
static void *
IntegerClone(void *self)
{
	return IntegerCreate(REF_INTEGER(self)->value);
}

static int
IntegerCompare(void *self, /*@null@*/ void *other)
{
	/* NULL pointers and non-Data objects sort towards the end. */
	if (other == NULL || REF_INTEGER(other)->compare != IntegerCompare)
		return -1;

	if (self == other || REF_INTEGER(self)->value == REF_INTEGER(other)->value)
		return 0;

	return REF_INTEGER(self)->value < REF_INTEGER(other)->value ? -1 : 1;
}

static int
IntegerEquals(void *self, /*@null@*/ void *other)
{
	return (*REF_INTEGER(self)->compare)(self, other) == 0;
}

static long
IntegerHashcode(void *self)
{
	return REF_INTEGER(self)->value;
}

#ifdef REALLY_WANT_ACCESSORS_FOR_PUBLIC_INSTANCE_VARIABLE
static long
IntegerGet(Integer self)
{
	return self->value;
}

static void
IntegerSet(Integer self, long value)
{
	self->value = value;
}
#endif

/***********************************************************************
 *** Class methods
 ***********************************************************************/

void
IntegerInit(Integer self)
{
	static struct integer model;

	if (model.objectName == NULL) {
		ObjectInit(&model);

		/* Overrides. */
		model.objectSize = sizeof (struct integer);
		model.objectName = "Integer";
		model.clone = IntegerClone;
		model.equals = IntegerEquals;
		model.compare = IntegerCompare;
		model.hashcode = IntegerHashcode;

		/* Methods */
#ifdef REALLY_WANT_ACCESSORS_FOR_PUBLIC_INSTANCE_VARIABLE
		model.objectMethodCount += 2;
		model.set = IntegerSet;
		model.get = IntegerGet;
#endif
		/* Instance variables. */
		model.value = 0;
	}

	*self = model;
}

Integer
IntegerCreate(long value)
{
	Integer self;

	if ((self = malloc(sizeof (*self))) == NULL)
		return NULL;

	IntegerInit(self);
	/*@-usedef -type@*/
	self->destroy = free;
	/*@=usedef =type@*/
	self->value = value;

	return self;
}

Integer
IntegerCreateFromString(const char *str)
{
	if (str == NULL)
		return NULL;

	return IntegerCreate(strtol(str, NULL, 0));
}

/***********************************************************************
 *** Test
 ***********************************************************************/

#ifdef TEST

#define WITHOUT_SYSLOG			1

#include <stdio.h>
#include <com/snert/lib/version.h>

#ifdef USE_DEBUG_MALLOC
# define WITHOUT_SYSLOG			1
# include <com/snert/lib/io/Log.h>
# include <com/snert/lib/util/DebugMalloc.h>
#endif

void
isNotNull(void *ptr)
{
	if (ptr == NULL) {
		printf("...NULL\n");
		exit(1);
	}

	printf("...OK\n");
}

int
main(int argc, char **argv)
{
	Integer a, b;
	struct integer data;

	printf("\n--Integer--\n");

	printf("init local stack object\n");
	IntegerInit(&data);

	printf("destroy local stack object\n");
	data.destroy(&data);

	printf("create dynamic object");
	isNotNull((a = IntegerCreate(9)));

	printf("destroy dynamic object\n");
	a->destroy(a);

	printf("\nIntegerCreateFromString()");
	isNotNull(a = IntegerCreateFromString("0xbeef"));

	printf("value is 0xBEEF...%s\n", a->value == 0xbeef ? "OK" : "FAIL");

	printf("\nIntegerCreate()");
	isNotNull(b = IntegerCreate(9));

	printf("a > b...%s\n", a->compare(a, b) > 0 ? "OK" : "FAIL");
	printf("b < a...%s\n", b->compare(b, a) < 0 ? "OK" : "FAIL");
	printf("a != b...%s\n", !a->equals(a, b) ? "OK" : "FAIL");


	printf("destroy a & b\n");
	a->destroy(a);
	b->destroy(b);

	printf("\n--DONE--\n");

	return 0;
}

#endif /* TEST */
