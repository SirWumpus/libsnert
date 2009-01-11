/*
 * Integer.c
 *
 * Copyright 2002, 2006 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>
#include <com/snert/lib/object/Object.h>

/***********************************************************************
 *** Integer definition
 ***********************************************************************/

static void *
integer_create(void *_self, va_list *args)
{
	((integer *) _self)->value = va_arg(*args, long);

	return _self;
}

static void *
integer_clone(void *_self)
{
	return objectCreate(Integer, ((integer *) _self)->value);
}

static int
integer_compare(void *_self, void *_other)
{
	if (_other == NULL || ((object *) _other)->create != integer_create)
		return -1;

	if (((integer *) _self)->value == ((integer *) _other)->value)
		return 0;

	return ((integer *) _self)->value < ((integer *) _other)->value ? -1 : 1;
}

static unsigned long
integer_hashcode(void *_self)
{
	return (unsigned long) ((integer *) _self)->value;
}

static const struct integer _Integer = {
	NULL,
	integer_create,
	sizeof (const struct integer),
	integer_clone,
	integer_compare,
	integer_hashcode
};

const void *Integer = &_Integer;

/***********************************************************************
 *** END
 ***********************************************************************/

