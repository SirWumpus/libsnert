/*
 * Object.c
 *
 * Copyright 2004, 2013 by Anthony Howe.  All rights reserved.
 */

#include <stdlib.h>
#include <string.h>

#include <com/snert/lib/version.h>
#include <com/snert/lib/type/Object.h>

#ifdef DEBUG_MALLOC
# include <com/snert/lib/util/DebugMalloc.h>
#endif

#define REF_OBJECT(v)		((Object)(v))

/***********************************************************************
 *** Instance methods
 ***********************************************************************/

/*@out@*//*@null@*/
static void *
ObjectClone(void *self)
{
	Object copy;

	if ((copy = ObjectCreate()) != NULL)
		*copy = *REF_OBJECT(self);

	return (void *) copy;
}

static int
ObjectCompare(void *self, /*@null@*/ void *other)
{
	return (int)((char *) self - (char *) other);
}

static int
ObjectEquals(void *self, /*@null@*/ void *other)
{
	return self == other;
}

static long
ObjectHashcode(void *self)
{
	return (long) self;
}

/*@-mustfreeonly -mustdefine@*/
void
ObjectDestroyNothing(/*@unused@*/ void *self)
{
	/* Do nothing. */
}
/*@=mustfreeonly =mustdefine @*/

/***********************************************************************
 *** Class methods
 ***********************************************************************/

static char ObjectName[] = "Object";

/**
 * Initialise an object. Typically used for static Objects.
 *
 * @param object
 *	A pointer to an object to initialise.
 */
/*@-mustdefine@*/
void
ObjectInit(void *self)
{
	static struct object model = {
		sizeof (struct object),
		5,
		ObjectName,
		ObjectClone,
		ObjectCompare,
		ObjectDestroyNothing,
		ObjectEquals,
		ObjectHashcode
	};

	*(Object) self = model;
}
/*@=mustdefine@*/

/**
 * Create and initialise a new Object.
 *
 * @return
 *	An object; otherwise null on error.
 */
Object
ObjectCreate(void)
{
	Object self;

	if ((self = malloc(sizeof (*self))) == NULL)
		return NULL;

	ObjectInit(self);
 	/*@-type@*/
	self->destroy = free;
 	/*@=type@*/

	return self;
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
	int x, y;
	Object a, b, c;
	struct object obj;

	printf("\n--Object--\n");

	printf("init local stack object\n");
	ObjectInit(&obj);

	printf("destroy local stack object\n");
	obj.destroy(&obj);

	printf("create dynamic object");
	isNotNull((a = ObjectCreate()));

	printf("destroy dynamic object\n");
	a->destroy(a);

	printf("create b");
	isNotNull((b = ObjectCreate()));

	printf("clone c from b");
	isNotNull((c = b->clone(b)));

	printf("reflexive: b equals self...%s\n", b->equals(b, b) ? "OK" : "FAIL");
	printf("reflexive: c equals self...%s\n", c->equals(c, c) ? "OK" : "FAIL");

	printf("b not equal NULL...%s\n", !b->equals(b, NULL) ? "OK" : "FAIL");
	printf("c not equal NULL...%s\n", !c->equals(c, NULL) ? "OK" : "FAIL");

	printf("b not equal c...%s\n", !b->equals(b, c) ? "OK" : "FAIL");
	printf("c not equal b...%s\n", !c->equals(c, b) ? "OK" : "FAIL");

	printf("b compared to b is %d...%s\n", b->compare(b, b), b->compare(b, b) == 0 ? "OK" : "FAIL");
	printf("c compared to c is %d...%s\n", c->compare(c, c), c->compare(c, c) == 0 ? "OK" : "FAIL");
	printf("b compared to c is %d\n", x = b->compare(b, c));
	printf("c compared to b is %d\n", y = c->compare(c, b));
	printf("b compared c equals -(c compared to b)...%s\n", x == -y ? "OK" : "FAIL");

	printf("hashcodes for b and c are different...%s\n", b->hashcode(b) != c->hashcode(c) ? "OK" : "FAIL");

	printf("destroy b & c\n");
	b->destroy(b);
	c->destroy(c);

	printf("\n--DONE--\n");

	return 0;
}

#endif /* TEST */
