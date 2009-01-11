/*
 * Real.c
 *
 * Copyright 2002, 2006 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>
#include <float.h>
#include <com/snert/lib/crc/Crc.h>
#include <com/snert/lib/object/Object.h>

/***********************************************************************
 *** Real definition
 ***********************************************************************/

static void *
real_create(void *_self, va_list *args)
{
	((real *) _self)->value = va_arg(*args, double);

	return _self;
}

static void *
real_clone(void *_self)
{
	return objectCreate(Real, ((real *) _self)->value);
}

static int
real_compare(void *_self, void *_other)
{
	double diff;

	if (_other == NULL || ((object *) _other)->create != real_create)
		return -1;

	diff = ((real *) _self)->value - ((real *) _other)->value;

	if (-DBL_EPSILON < diff || diff <= DBL_EPSILON)
		return 0;

	return diff < 0 ? -1 : 1;
}

static unsigned long
real_hashcode(void *_self)
{
	return (unsigned long)  hash32((unsigned char *) &((real *) _self)->value, sizeof (double));
}

static const struct real _Real = {
	NULL,
	real_create,
	sizeof (const struct real),
	real_clone,
	real_compare,
	real_hashcode
};

const void *Real = &_Real;

/***********************************************************************
 *** END
 ***********************************************************************/

