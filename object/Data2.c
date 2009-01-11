/*
 * Data.c
 *
 * Copyright 2002, 2006 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <stdlib.h>
#include <string.h>

#include <com/snert/lib/crc/Crc.h>
#include <com/snert/lib/object/Object.h>

/***********************************************************************
 *** Data definition
 ***********************************************************************/

static void
data_destroy(void *_self)
{
	if (_self != NULL) {
		free(((data *) _self)->data);
		((data *) _self)->data = NULL;
		((data *) _self)->size = 0;
	}
}

static int
data_set(void *_self, unsigned char *buffer, unsigned long size)
{
	unsigned char *copy;

	if (buffer != NULL) {
		if ((copy = realloc(((data *) _self)->data, size + 1)) == NULL)
			return -1;

		((data *) _self)->size = size;
		((data *) _self)->data = copy;
		memcpy(copy, buffer, size);
		copy[size] = '\0';
	} else {
		objectFini(_self);
	}

	return 0;
}

static void *
data_create(void *_self, va_list *args)
{
	unsigned long size;
	unsigned char *buffer;

	buffer = va_arg(*args, unsigned char *);
	size = va_arg(*args, unsigned long);
	((data *) _self)->data = NULL;

	return data_set(_self, buffer, size) == 0 ? _self : NULL;
}

static void *
data_clone(void *_self)
{
	return objectCreate(Data, ((data *) _self)->data, ((data *) _self)->size);
}

static int
data_compare(void *_self, void *_other)
{
	data *self, *other;

	if (_other == NULL || ((object *) _other)->destroy != data_destroy)
		return -1;

	self = _self;
	other = _other;

	if (self->size > other->size)
		return 1;

	if (self->size < other->size)
		return -1;

	return memcmp(self->data, other->data, (size_t) self->size);
}

static unsigned long
data_hashcode(void *_self)
{
	return (unsigned long) hash32(((data *) _self)->data, ((data *) _self)->size);
}

static const struct data _Data = {
	data_destroy,
	data_create,
	sizeof (const struct data),
	data_clone,
	data_compare,
	data_hashcode,
	data_set
};

const void *Data = &_Data;

/***********************************************************************
 *** END
 ***********************************************************************/

