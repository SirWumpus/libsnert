/*
 * Property.c
 *
 * Copyright 2004, 2006 by Anthony Howe.  All rights reserved.
 */

#include <com/snert/lib/version.h>

#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>

#ifdef __unix__
# include <sys/file.h>
#endif

#include <com/snert/lib/io/file.h>
#include <com/snert/lib/type/Data.h>
#include <com/snert/lib/type/Hash.h>
#include <com/snert/lib/util/Buf.h>
#include <com/snert/lib/util/Base64.h>
#include <com/snert/lib/util/Properties.h>

#define REF_PROPERTIES(v)		((Properties)(v))

#define PAD		'-'

/***********************************************************************
 *** Instance methods
 ***********************************************************************/

long
PropertiesSize(Properties self)
{
	return ((Hash) self->_properties)->size(self->_properties);
}

Data
PropertiesGetData(Properties self, Data key)
{
	return ((Hash) self->_properties)->get(self->_properties, key);
}

char *
PropertiesGetProperty(Properties self, const char *key)
{
	Data v;
	struct data k;

	DataInitWithBytes(&k, (unsigned char *) key, strlen(key) + 1);

	if ((v = PropertiesGetData(self, &k)) == NULL)
		return NULL;

	return (char *) v->_base;
}

int
PropertiesSetData(Properties self, Data key, Data value)
{
	Data k, v;

	if (key == NULL || value == NULL)
		goto error0;

	if ((k = key->clone(key)) == NULL)
		goto error0;

	if ((v = value->clone(value)) == NULL)
		goto error1;

	if (((Hash) self->_properties)->put(self->_properties, k, v))
		goto error2;

	return 0;
error2:
	v->destroy(v);
error1:
	k->destroy(k);
error0:
	return -1;
}

int
PropertiesSetProperty(Properties self, const char *key, const char *value)
{
	Data k, v;

	if ((k = DataCreateCopyString(key)) == NULL)
		goto error0;

	if ((v = DataCreateCopyString(value)) == NULL)
		goto error1;

	if (((Hash) self->_properties)->put(self->_properties, k, v))
		goto error2;

	return 0;
error2:
	v->destroy(v);
error1:
	k->destroy(k);
error0:
	return -1;
}

void
PropertiesRemoveAll(Properties self)
{
	((Hash) self->_properties)->removeAll(self->_properties);
}

int
PropertiesRemoveData(Properties self, Data key)
{
	return ((Hash) self->_properties)->remove(self->_properties, key);
}

int
PropertiesRemoveProperty(Properties self, const char *key)
{
	struct data k;

	DataInitWithBytes(&k, (unsigned char *) key, strlen(key) + 1);

	return PropertiesRemoveData(self, &k);
}

int
PropertiesLoad(Properties self, const char *file)
{
	Buf *b;
	FILE *input;
	long length;
	Base64 decoder;
	Data key, value;
	char *base, *buffer;
	int rc, n, voffset;
	size_t offset;

	rc = -1;

	if ((input = fopen(file, "r")) == NULL)
		goto error0;

	/* Wait until we get the file lock. */
	do
		errno = 0;
	while (flock(fileno(input), LOCK_SH) && errno == EINTR);

	if (errno != 0 || (b = BufCreate(BUFSIZ)) == NULL)
		goto error1;

	if ((decoder = Base64Create()) == NULL)
		goto error2;

	decoder->setPadding(decoder, PAD);

	base = (char *) BufBytes(b);

	for ( ; 0 <= BufAddInputLine(b, input, -1); BufSetLength(b, 0)) {
		if (BufLength(b) <= 0 || *base == '#')
			continue;

		/* Look for the key/value delimiter. */
		if ((offset = strcspn(base, " \t:=")) == BufLength(b))
			goto error3;

		/* Skip over the delimiter. */
		n = strspn(base + offset, " \t:=");

		if (1 < offset && base[0] == '*' && base[1] == '?') {
			/* A binary key base 64 encoded. */
			decoder->reset(decoder);
			(void) decoder->decodeBuffer(decoder, base+2, offset-2, &buffer, &length);
			if ((key = DataCreateWithBytes((unsigned char *) buffer, length)) == NULL)
				goto error3;
		} else {
			/* A typical text key. */
			if ((key = DataCreateCopyBytes((unsigned char *) base, offset + 1)) == NULL)
				goto error3;

			key->base(key)[offset] = '\0';
		}

		voffset = offset + n;
		length = BufLength(b) - voffset;

		if (1 < length && base[voffset] == '*' && base[voffset+1] == '?') {
			/* A binary value base 64 encoded. */
			decoder->reset(decoder);
			(void) decoder->decodeBuffer(decoder, base+voffset+2, length-2, &buffer, &length);
			if ((value = DataCreateWithBytes((unsigned char *) buffer, length)) == NULL) {
				key->destroy(key);
				goto error3;
			}
		} else {
			/* A typical text value. */
			if ((value = DataCreateCopyString(base + voffset)) == NULL) {
				key->destroy(key);
				goto error3;
			}

			value->base(value)[length] = '\0';
		}

		if (((Hash) self->_properties)->put(self->_properties, key, value)) {
			value->destroy(value);
			key->destroy(key);
			goto error3;
		}
	}

	rc = 0;
error3:
	Base64Destroy(decoder);
error2:
	BufDestroy(b);
error1:
	(void) fclose(input);
error0:
	return rc;
}

static int
isPrintable(Data obj)
{
	long i, length;
	unsigned char *p = obj->_base;

	length = obj->length(obj);

	if (1 < length && p[0] == '*' && p[1] == '?')
		return 0;

	if (0 < length && p[length-1] == '\t')
		return 0;

	/* Does it look like the null byte of a C string? */
	if (p[length-1] == '\0')
		length--;

	for (p = obj->_base, i = 0; i < length; i++, p++) {
		if (!isprint(*p))
			return 0;
	}

	return 1;
}

typedef struct {
	FILE *output;
	Base64 encoder;
} Context;

static int
saveKeyValue(void *k, void *v, void *data)
{
	long length;
	char *buffer;
	Data key = (Data) k;
	Data value = (Data) v;
	Context *ctx = (Context *) data;

	if (isPrintable(key)) {
		fputs((char *) key->_base, ctx->output);
	} else if (ctx->encoder->encodeBuffer(ctx->encoder, (char *) key->_base, key->_length, &buffer, &length, 1) == BASE64_ERROR) {
		return 0;
	} else {
		/* RFC 2045
		 *
		 * These are variant on EBCDIC systems:
		 *
		 *	!"#$@[\]^`{|}~
		 *
		 * While these are invariant:
		 *
		 *	A-Z  a-z  0-9  SPACE  "%&'()*+,-./:;<=>?_
		 *
		 * We have to pick an uncommon combination of symbols
		 * that can be used to signify that what follows is
		 * a Base64 string. Cannot use equals, colon, or space
		 * since they are used delimit the key and value.
		 */
		fputs("*?", ctx->output);
		fputs(buffer, ctx->output);
		free(buffer);
	}

	fputc('\t', ctx->output);

	if (isPrintable(value)) {
		fputs((char *) value->_base, ctx->output);
	} else if (ctx->encoder->encodeBuffer(ctx->encoder, (char *) value->_base, value->_length, &buffer, &length, 1) == BASE64_ERROR) {
		return 0;
	} else {
		fputs("*?", ctx->output);
		fputs(buffer, ctx->output);
		free(buffer);
	}

	fputs("\r\n", ctx->output);

	return 1;
}

int
PropertiesSave(Properties self, const char *file)
{
	int rc;
	Context ctx;

	rc = -1;

	if ((ctx.output = fopen(file, "w")) == NULL)
		goto error0;

	/* Wait until we get the file lock. */
	do
		errno = 0;
	while (flock(fileno(ctx.output), LOCK_EX) && errno == EINTR);

	if (errno != 0 || (ctx.encoder = Base64Create()) == NULL)
		goto error1;

	/* Switch the default Base64 padding character, since equals-sign (=)
	 * is a common delimiter for key=value pairs. Note too that colon (:)
	 * is another favourite that shouldn't be used.
	 */
	ctx.encoder->setPadding(ctx.encoder, PAD);

	(void) ((Hash) self->_properties)->walk(self->_properties, saveKeyValue, &ctx);

	rc = 0;

	ctx.encoder->destroy(ctx.encoder);
error1:
	(void) fclose(ctx.output);
error0:
	return rc;
}

int
PropertiesWalk(Properties self, int (*function)(void *key, void *value, void *data), void *data)
{
	return ((Hash) self->_properties)->walk(self->_properties, function, data);
}

void
PropertiesDestroy(void *self)
{
	Hash hash = REF_PROPERTIES(self)->_properties;
	hash->destroy(hash);
	free(self);
}

/***********************************************************************
 *** Class methods
 ***********************************************************************/

static char PropertiesName[] = "Properties";

Properties
PropertiesCreate(void)
{
	Properties self;
	static struct properties model;

	if ((self = malloc(sizeof (*self))) == NULL)
		goto error0;

	if (model.objectName != PropertiesName) {
		ObjectInit(&model);

		/* Overrides */
		model.objectSize = sizeof (struct properties);
		model.objectName = PropertiesName;
		model.destroy = PropertiesDestroy;

		/* Methods */
		model.getData = PropertiesGetData;
		model.getProperty = PropertiesGetProperty;

		model.setData = PropertiesSetData;
		model.setProperty = PropertiesSetProperty;

		model.removeAll = PropertiesRemoveAll;
		model.removeData = PropertiesRemoveData;
		model.removeProperty = PropertiesRemoveProperty;

		model.walk = PropertiesWalk;
		model.load = PropertiesLoad;
		model.save = PropertiesSave;
		model.size = PropertiesSize;

		model.objectMethodCount += 11;

		/* Instance variables. */
		model._properties = NULL;
	}

	*self = model;

	if ((self->_properties = HashCreate()) == NULL)
		goto error1;

	return self;
error1:
	free(self);
error0:
	return NULL;
}

/***********************************************************************
 *** Test
 ***********************************************************************/

#ifdef TEST
#include <errno.h>
#include <com/snert/lib/version.h>
#include <com/snert/lib/type/Data.h>

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
countProperties(void *key, void *value, void *data)
{
	(*(long *) data)++;
	printf("count=%ld %s=%s\n", *(long *) data, ((Data) key)->_base, ((Data) value)->_base);

	return 1;
}

int
main(int argc, char **argv)
{
	long count;
	char *value;
	Properties a;

	setvbuf(stdout, NULL, _IOLBF, 0);

	printf("\n--Properties--\n");

	printf("create properties A");
	isNotNull((a = PropertiesCreate()));

	printf("load properties from \"properties.txt\"");
	if (a->load(a, "properties.txt"))
		printf(": %s (%d)...FAIL\n", strerror(errno), errno);
	else
		printf("...OK\n");

	value = a->getProperty(a, "key2");
	printf("key2 exists...%s\n", value == NULL ? "NULL" : "OK");
	if (value != NULL)
		printf("key2 equals value2...%s\n", strcmp(value, "value2") == 0 ? "OK" : "FAIL");

	value = a->getProperty(a, "binary");
	printf("binary exists...%s\n", value == NULL ? "NULL" : "OK");
	if (value != NULL)
		printf("binary equals \\000\\001...%s\n", memcmp(value, "\000\001", 2) == 0 ? "OK" : "FAIL");

	printf("add a property...%s\n", a->setProperty(a, "keyBEL", "value{\07}") ?  "FAIL" : "OK");

	count = 0;
	a->walk(a, countProperties, &count);
	printf("count = %ld...%s\n", count, count == 6 ? "OK" : "FAIL");
	printf("save properties to \"properties.out\"...%s\n", a->save(a, "properties.out") ? "FAIL" : "OK");
	printf("destroy properties A\n");
	a->destroy(a);

	printf("\n--DONE--\n");

	return 0;
}

#endif /* TEST */
