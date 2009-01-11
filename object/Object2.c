/*
 * generic.c
 *
 * Copyright 2004, 2006 by Anthony Howe.  All rights reserved.
 */

#include <com/snert/lib/version.h>

#include <stdlib.h>
#include <string.h>

#include <com/snert/lib/object/Object.h>

/***********************************************************************
 *** Object definition
 ***********************************************************************/

static const struct object _object = {
	NULL,
	NULL,
	sizeof (struct object)
};

const void *Object = &_object;

/***********************************************************************
 *** Object Management
 ***********************************************************************/

/**
 * @param type
 *	A pointer to an ``object'' descriptor to be created.
 *
 * @param ...
 *	Arguments to be passed to the object's initialisation method.
 *
 * @return
 *	A pointer to a new allocated object of type, else NULL on
 *	error. Its the caller's responsiblity to pass this object
 *	to objectDestroy() when the object is no longer required.
 */
void *
objectCreate(const void *type, ...)
{
	va_list args;
	struct object *self;

	if (type == NULL)
		return NULL;

	if ((self = malloc(((struct object *) type)->size_of)) != NULL) {
		va_start(args, type);
		memcpy(self, type, ((struct object *) type)->size_of);
		if (((struct object *) type)->create != NULL) {
			if (((struct object *) type)->create(self, &args) != self) {
				free(self);
				self = NULL;
			}
		}
		va_end(args);
	}

	return self;
}

/**
 * @param self
 *	A pointer to an ``object'', previously created by
 *	objectCreate(), to be deallocated.
 */
void
objectDestroy(void *self)
{
	if (self != NULL) {
		if (((struct object *) self)->destroy != NULL)
			((struct object *) self)->destroy(self);
		free(self);
	}
}

/**
 * Initialise a static or stack allocated object of a given type. Its
 * the caller's responsiblity to pass this object to objectFini() when
 * the object is no longer required.
 *
 * @param type
 *	A pointer to an ``object'' descriptor to be initialised.
 *
 * @param self
 *	A pointer to a static or stack allocated ``object''.
 *
 * @param ...
 *	Arguments to be passed to the object's initialisation method.
 *
 * @return
 *	Zero on success, otherwise -1 on error.
 */
int
objectInit(const void *type, void *self, ...)
{
	int rc = -1;
	va_list args;

	if (type != NULL) {
		rc = 0;
		va_start(args, self);
		memcpy(self, type, ((struct object *) type)->size_of);
		if (((struct object *) type)->create != NULL)
			rc = -(((struct object *) type)->create(self, &args) != self);
		va_end(args);
	}

	return rc;
}

/**
 * Discard a static or stack allocated object's resources.
 *
 * @param self
 *	A pointer to an ``object'', previously initialised by
 *	objectInit(), to be destroyed.
 */
void
objectFini(void *self)
{
	if (self != NULL && ((struct object *) self)->destroy != NULL)
		((struct object *) self)->destroy(self);
}

/***********************************************************************
 *** END
 ***********************************************************************/
