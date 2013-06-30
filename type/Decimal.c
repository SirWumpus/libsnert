/*
 * Decimal.c
 *
 * Copyright 2004, 2013 by Anthony Howe.  All rights reserved.
 */

#include <float.h>
#include <stdlib.h>
#include <string.h>

#include <com/snert/lib/version.h>
#include <com/snert/lib/crc/Crc.h>
#include <com/snert/lib/type/Decimal.h>

#ifdef DEBUG_MALLOC
# include <com/snert/lib/util/DebugMalloc.h>
#endif

#define REF_DECIMAL(v)		((Decimal)(v))

#define DYNAMIC_OBJECT		1
#define DYNAMIC_CONTENT		2

/***********************************************************************
 *** Instance methods
 ***********************************************************************/

/*@only@*//*@null@*/
static void *
DecimalClone(void *self)
{
	return DecimalCreate(REF_DECIMAL(self)->value);
}

static int
DecimalCompare(void *self, /*@null@*/ void *other)
{
	double diff;

	/* NULL pointers and non-Data objects sort towards the end. */
	if (other == NULL || REF_DECIMAL(other)->compare != DecimalCompare)
		return -1;

	if (self == other)
		return 0;

	diff = REF_DECIMAL(self)->value - REF_DECIMAL(other)->value;

	if (diff <= DBL_EPSILON)
		return 0;

	return DBL_EPSILON < diff? 1 : -1;
}

static int
DecimalEquals(void *self, /*@null@*/ void *other)
{
	return (*REF_DECIMAL(self)->compare)(self, other) == 0;
}

static long
DecimalHashcode(void *self)
{
	/*** Not very good. ***/
	return (long) REF_DECIMAL(self)->value;
}

#ifdef REALLY_WANT_ACCESSORS_FOR_PUBLIC_INSTANCE_VARIABLE
static double
DecimalGet(Decimal self)
{
	return self->value;
}

static void
DecimalSet(Decimal self, double value)
{
	self->value = value;
}
#endif

/***********************************************************************
 *** Class methods
 ***********************************************************************/

void
DecimalInit(Decimal self)
{
	static struct decimal model;

	if (model.objectName == NULL) {
		ObjectInit(&model);

		/* Overrides. */
		model.objectSize = sizeof (struct decimal);
		model.objectName = "Decimal";
		model.clone = DecimalClone;
		model.equals = DecimalEquals;
		model.compare = DecimalCompare;
		model.hashcode = DecimalHashcode;

		/* Methods */
#ifdef REALLY_WANT_ACCESSORS_FOR_PUBLIC_INSTANCE_VARIABLE
		model.objectMethodCount += 2;
		model.set = DecimalSet;
		model.get = DecimalGet;
#endif
		/* Instance variables. */
		model.value = 0;
	}

	*self = model;
}

Decimal
DecimalCreate(double value)
{
	Decimal self;

	if ((self = malloc(sizeof (*self))) == NULL)
		return NULL;

	DecimalInit(self);
	/*@-usedef -type@*/
	self->destroy = free;
	/*@=usedef =type@*/
	self->value = value;

	return self;
}

Decimal
DecimalCreateFromString(const char *str)
{
	if (str == NULL)
		return NULL;

	return DecimalCreate(strtod(str, NULL));
}

/***********************************************************************
 *** Test
 ***********************************************************************/

#ifdef TEST

#define WITHOUT_SYSLOG			1

#include <com/snert/lib/version.h>

#ifdef USE_DEBUG_MALLOC
# define WITHOUT_SYSLOG			1
# include <com/snert/lib/io/Log.h>
# include <com/snert/lib/util/DebugMalloc.h>
#endif

int
main(int argc, char **argv)
{
	return 0;
}

#endif /* TEST */
