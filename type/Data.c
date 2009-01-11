/*
 * Data.c
 *
 * Copyright 2004 by Anthony Howe.  All rights reserved.
 */

#include <stdlib.h>
#include <string.h>

#include <com/snert/lib/crc/Crc.h>
#include <com/snert/lib/type/Data.h>

#define REF_DATA(v)		((Data)(v))

/***********************************************************************
 *** Instance methods
 ***********************************************************************/

static void
DataDestroy(/*@only@*//*@null@*/ void *selfless)
{
	Data self = selfless;

	if (self != NULL) {
		free(self->_base);
		free(self);
	}
}

/*@only@*//*@null@*/
static void *
DataClone(void *self)
{
	return DataCreateCopyBytes((*REF_DATA(self)->base)(self), (*REF_DATA(self)->length)(self));
}

static int
DataCompare(void *self, /*@null@*/ void *other)
{
	long a, b;

	/* NULL pointers and non-Data objects sort towards the end. */
	if (other == NULL || REF_DATA(other)->compare != DataCompare)
		return -1;

	if (self == other)
		return 0;

	a = (*REF_DATA(self)->length)(self);
	b = (*REF_DATA(other)->length)(other);

	if (a > b)
		return 1;

	if (a < b)
		return -1;

	return memcmp((*REF_DATA(self)->base)(self), (*REF_DATA(other)->base)(other), (size_t) a);
}

static int
DataEquals(void *self, /*@null@*/ void *other)
{
	return (*REF_DATA(self)->compare)(self, other) == 0;
}

static long
DataHashcode(void *self)
{
	return (long) hash32((*REF_DATA(self)->base)(self), (int) (*REF_DATA(self)->length)(self));
}

/*@null@*//*@exposed@*/
static unsigned char *
DataBase(Data self)
{
	return self->_base;
}

static long
DataLength(Data self)
{
	return self->_length;
}

/***********************************************************************
 *** Class methods
 ***********************************************************************/

static char DataName[] = "Data";

void
DataInit(Data self)
{
	static struct data model;

	if (model.objectName != DataName) {
		ObjectInit(&model);

		/* Overrides */
		model.objectSize = sizeof (struct data);
		model.objectName = DataName;
		model.clone = DataClone;
		model.equals = DataEquals;
		model.compare = DataCompare;
		model.hashcode = DataHashcode;

		/* Methods */
		model.base = DataBase;
		model.length = DataLength;
		model.objectMethodCount += 2;

#ifdef ASSERT_STATIC_MEMBERS_ARE_NULL
		/* Instance variables. */
		model._base = NULL;
		model._length = 0;
#endif
	}

	*self = model;
}

void
DataInitWithBytes(Data self, unsigned char *bytes, long length)
{
	DataInit(self);
	/*@-mustfreeonly @*/
	self->_base = bytes;
	/*@=mustfreeonly @*/
	self->_length = length;
}

/*
 *
 */
Data
DataCreate(void)
{
	Data self;

	if ((self = calloc(1, sizeof (*self))) == NULL)
		return NULL;

	DataInit(self);
	/*@-type@*/
	self->destroy = free;
	/*@=type@*/

	return self;
}

Data
DataCreateWithBytes(unsigned char *bytes, long length)
{
	Data self;

	if (bytes == NULL || (self = DataCreate()) == NULL)
		return NULL;

	self->destroy = DataDestroy;
	self->_length = length;
	self->_base = bytes;

	return self;
}

Data
DataCreateCopyBytes(unsigned char *bytes, long length)
{
	Data self;
	unsigned char *buffer;

	if (bytes == NULL)
		return NULL;

	if ((buffer = malloc((size_t) length)) == NULL)
		return NULL;

	(void) memcpy(buffer, bytes, (size_t) length);

	if ((self = DataCreateWithBytes(buffer, length)) == NULL)
		free(buffer);

	return self;
}

Data
DataCreateWithString(const char *str)
{
	if (str == NULL)
		return NULL;

	return DataCreateWithBytes((unsigned char *) str, (long) strlen(str)+1);
}

Data
DataCreateCopyString(const char *str)
{
	if (str == NULL)
		return NULL;

	return DataCreateCopyBytes((unsigned char *) str, (long) strlen(str)+1);
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
	Data a;
	struct data data;
	static unsigned char sample[] = { 1, 2, 3, 4 };

	printf("\n--Data--\n");

	printf("init local stack object\n");
	DataInit(&data);

	printf("destroy local stack object\n");
	data.destroy(&data);

	printf("create dynamic object");
	isNotNull((a = DataCreate()));

	printf("destroy dynamic object\n");
	a->destroy(a);

	printf("\nDataCreateCopyBytes()");
	isNotNull(a = DataCreateCopyBytes(sample, sizeof (sample)));

	printf("length equals original...%s\n", (size_t) a->length(a) == sizeof (sample) ? "OK" : "FAIL");
	printf("content same as original...%s\n", memcmp(a->base(a), sample, sizeof (sample)) == 0 ? "OK" : "FAIL");

	printf("destroy a\n");
	a->destroy(a);

	printf("\nDataCreateCopyString()");
	isNotNull(a = DataCreateCopyString("sample"));

	printf("length equals original...%s\n", (size_t) a->length(a) == sizeof ("sample") ? "OK" : "FAIL");
	printf("length equals strlen()+1...%s\n", (size_t) a->length(a) == strlen("sample")+1 ? "OK" : "FAIL");

	printf("memcmp() with original...%s\n", memcmp(a->base(a), "sample", (long) sizeof ("sample")) == 0 ? "OK" : "FAIL");
	printf("strcmp() with original...%s\n", strcmp(a->base(a), "sample") == 0 ? "OK" : "FAIL");

	printf("destroy a\n");
	a->destroy(a);

	printf("\n--DONE--\n");

	return 0;
}

#endif /* TEST */
