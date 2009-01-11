/*
 * Text.c
 *
 * Copyright 2001, 2006 by Anthony Howe.  All rights reserved.
 */

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <com/snert/lib/version.h>
#include <com/snert/lib/crc/Crc.h>
#include <com/snert/lib/io/posix.h>
#include <com/snert/lib/util/Buf.h>
#include <com/snert/lib/type/Text.h>
#include <com/snert/lib/util/Text.h>

#define REF_TEXT(v)		((Text)(v))

/***********************************************************************
 *** Instance methods
 ***********************************************************************/

/*@+accessslovak@*/
/*@access Object@*/

static char empty[] = "";

static void
bound_offset_length(long slength, long *offset, long *length)
{
	/* Correct offset if out of bounds. */
	if (*offset < 0)
		*offset = 0;
	else if (slength < *offset)
		*offset = slength;

	/* Correct length if out of bounds. */
	if (*length < 0)
		*length = slength;
	if (slength < *offset + *length)
		*length = slength - *offset;
}

#define TEXT_INPLACE_CONVERSION(_function_, _action_) 		\
void 								\
_function_(Text self, long offset, long length)			\
{								\
	char *stop;						\
	char *s = self->_string;				\
	bound_offset_length(self->_length, &offset, &length);	\
	for (stop = s + offset + length; s < stop; s++) {	\
		_action_;					\
	}							\
}

TEXT_INPLACE_CONVERSION(TextLowerCase, *s = (char) tolower(*s))
TEXT_INPLACE_CONVERSION(TextUpperCase, *s = (char) toupper(*s))
TEXT_INPLACE_CONVERSION(TextInvertCase, *s = (char) (isupper(*s) ? tolower(*s) : toupper(*s)))

void
TextReverseRegion(Text self, long offset, long length)
{
	char ch, *x, *y;

	bound_offset_length(self->_length, &offset, &length);

	/* Reverse segment of string. */
	for (x = y = self->_string + offset, y += length; x < --y; ++x) {
		ch = *y;
		*y = *x;
		*x = ch;
	}
}

Text
TextGetSubstring(Text self, long offset, long length)
{
	bound_offset_length(self->_length, &offset, &length);

	return TextCreateN(self->_string + offset, length);
}

int
TextIsBlank(Text self)
{
	return self->_string[strcspn(self->_string, " \t\r\n\f")] == '\0';
}

int
TextIsInt(Text self, int radix)
{
#ifdef HAVE_STRTOL
	char *stop;

	(void) strtol(self->_string, &stop, radix);

	return *stop == '\0';
#else
	char *digit, *s;
	static char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";

	s = self->_string;

	if (radix == 0) {
		radix = 10;

		if (*s == '0') {
			radix = 8;
			s++;
		}

		if (*s == 'x' || *s == 'X') {
			radix = 16;
			s++;
		}
	} if (*s == '\0' || radix < 2 || 36 < radix)
		return 0;

	for ( ; *s != '\0'; ++s) {
		digit = strchr(digits, *s);
		if (digit == (char *) 0 || radix <= digit - digits)
			return 0;
	}

	return 1;
#endif
}

int
TextAppend(Text self, Text other)
/*@modifies self->_string, self->_length@*/
{
	char *replacement;

	if (other == NULL || self->destroy == ObjectDestroyNothing)
		return -1;

	/*@-usereleased -compdef@*/
	if ((replacement = realloc(self->_string, (size_t)(self->_length + other->_length + 1))) == NULL)
		return -1;
	/*@=usereleased =compdef@*/

	strncpy(replacement + self->_length, other->_string, (size_t) other->_length);
	self->_length += other->_length;
	replacement[self->_length] = '\0';
	/*@-compmempass@*/
	self->_string = replacement;

	return 0;
	/*@=compmempass@*/
}

int
TextGetOctet(Text self, long index)
{
	if (index < 0 || self->_length < index)
		return -1;

	return (int) self->_string[index];
}

int
TextSetOctet(Text self, long index, int ch)
{
	if (index < 0 || self->_length < index)
		return -1;

	/* Overwrite the octet, which might be the null terminator */
	self->_string[index] = (char) ch;

	/* Assert that the string remains null terminated. */
	self->_string[self->_length] = '\0';

	return 0;
}

void *
TextClone(void *self)
{
	return TextCreateN(REF_TEXT(self)->_string, REF_TEXT(self)->_length);
}

int
TextCompareCaseN(void *selfless, void *otherless, long length)
{
	Text self = selfless;
	Text other = otherless;

	/* NULL pointers sort towards the end of a list. This may seem
	 * odd, but consider a NULL terminated array of pointers to char,
	 * like argv. You can iterate over the array stopping at the
	 * NULL. Sorting NULL to the end allows us to continue using
	 * that iteration technique.
	 */
	if (self == NULL && other != NULL)
		return 1;
	if (self != NULL && other == NULL)
		return -1;
	if (self == other)
		return 0;

	/*@-nullderef@*/
	if (length < 0)
		return strcmp(self->_string, other->_string);

	return strncmp(self->_string, other->_string, (size_t) length);
	/*@=nullderef@*/
}

int
TextCompareIgnoreCaseN(void *selfless, void *otherless, long length)
{
	Text self = selfless;
	Text other = otherless;

	/* NULL pointers sort towards the end of a list. This may seem
	 * odd, but consider a NULL terminated array of pointers to char,
	 * like argv. You can iterate over the array stopping at the
	 * NULL. Sorting NULL to the end allows us to continue using
	 * that iteration technique.
	 */
	if (self == NULL && other != NULL)
		return 1;
	if (self != NULL && other == NULL)
		return -1;
	if (self == other)
		return 0;

#if defined(HAVE_STRCASECMP) && defined(HAVE_STRNCASECMP)
	/*@-nullderef@*/
	if (length < 0)
		return strcasecmp(self->_string, other->_string);

	/* Assume strncasecmp() is more efficient. */
	return strncasecmp(self->_string, other->_string, (size_t) length);
	/*@=nullderef@*/
#else
{
	int diff;
	/*@notnull@*/ char *x, *y;

	/*@-nullderef@*/
	x = self->_string;
	y = other->_string;
	/*@=nullderef@*/

	for ( ; *x != '\0' && *y != '\0'; x++, y++) {
		if (0 <= length && length-- == 0)
			return 0;

		if (*x != *y) {
			diff = tolower(*x) - tolower(*y);
			if (diff != 0)
				return diff;
		}
	}

	if (length == 0)
		return 0;

	return (int)(*x - *y);
}
#endif
}

int
TextCompareCase(void *self, void *other)
{
	return TextCompareCaseN(self, other, -1);
}

int
TextCompareIgnoreCase(void *self, void *other)
{
	return TextCompareIgnoreCaseN(self, other, -1);
}

int
TextEqualsCase(void *self, void *other)
{
	return TextCompareCase(self, other) == 0;
}

int
TextEqualsIgnoreCase(void *self, void *other)
{
	return TextCompareIgnoreCase(self, other) == 0;
}

long
TextHashcodeCase(void *self)
{
	return (long) hash32((unsigned char *) REF_TEXT(self)->_string, (int) REF_TEXT(self)->_length);
}

long
TextHashcodeIgnoreCase(void *self)
{
	char *s;
	unsigned long hash = 0;

	for (s = REF_TEXT(self)->_string; *s != '\0'; s++)
		hash = crc32(hash, (unsigned) tolower(*s));

	return (long) hash;
}

int
TextGetIgnoreCase(Text self)
{
	return self->compare == TextCompareIgnoreCase;
}

void
TextSetIgnoreCase(Text self, int ignoreCase)
{
	if (ignoreCase) {
		self->compare = TextCompareIgnoreCase;
		self->equals = TextEqualsIgnoreCase;
		self->hashcode = TextHashcodeIgnoreCase;
	} else {
		self->compare = TextCompareCase;
		self->equals = TextEqualsCase;
		self->hashcode = TextHashcodeCase;
	}
}

long
TextStartsWithCaseC(Text self, const char *prefix)
{
	const char *text = self->_string;

	for ( ; *prefix != '\0'; ++text, ++prefix) {
		if (*text != *prefix)
			return -1;
	}

	return (long) (text - self->_string);
}

long
TextStartsWithIgnoreCaseC(Text self, const char *prefix)
{
	const char *text = self->_string;

	for ( ; *prefix != '\0'; ++text, ++prefix) {
		if (tolower(*text) != tolower(*prefix))
			return -1;
	}

	return (long) (text - self->_string);
}

long
TextStartsWithCase(Text self, Text prefix)
{
	return TextStartsWithCaseC(self, TextString(prefix));
}

long
TextStartsWithIgnoreCase(Text self, Text prefix)
{
	return TextStartsWithIgnoreCaseC(self, TextString(prefix));
}

long
TextEndsWithCase(Text self, Text suffix)
{
	const char *etext = self->_string + self->_length;
	const char *esuffix = suffix->_string + suffix->_length;

	while (suffix->_string < esuffix) {
		if (etext <= self->_string)
			return -1;
		if (*--etext != *--esuffix)
			return -1;
	}

	return suffix->_length;
}

long
TextEndsWithIgnoreCase(Text self, Text suffix)
{
	const char *etext = self->_string + self->_length;
	const char *esuffix = suffix->_string + suffix->_length;

	while (suffix->_string < esuffix) {
		if (etext <= self->_string)
			return -1;

		--etext;
		--esuffix;

		if (tolower(*etext) != tolower(*esuffix))
			return -1;
	}

	return suffix->_length;
}

static long
TextFindSubstring(Text self, Text sub, int (*cmpn)(void  *self, void *other, long length))
{
	/*@dependent@*/ struct text string;

	/* Empty string matches end of string. */
	if (sub->_length == 0)
		return self->_length;

	TextInitEmptyString(&string);
	string._length = self->_length;
	string._string = self->_string;

	for ( ; *string._string != '\0'; string._string++, string._length--) {
		if ((*cmpn)(&string, sub, sub->_length) == 0)
			return string._string - self->_string;
	}

	/* No match found. */
	return -1;
}

long
TextFindCase(Text self, Text sub)
{
	return TextFindSubstring(self, sub, TextCompareCaseN);
}

long
TextFindIgnoreCase(Text self, Text sub)
{
	return TextFindSubstring(self, sub, TextCompareIgnoreCaseN);
}

char *
TextString(Text self)
{
	return self->_string;
}

long
TextLength(Text self)
{
	return self->_length;
}

void
TextDestroy(void *selfless)
{
	Text self = selfless;

	if (self != NULL) {
		if (self->_string != empty)
			free(self->_string);
		free(self);
	}
}

/***********************************************************************
 *** Text*Token* instance methods
 ***********************************************************************/

void
TextResetTokens(Text self)
/*@ensures dependent self->_nextToken@*/
/*@modifies self->_nextToken@*/
{
	self->_nextToken = self->_string;
}

int
TextHasMoreTokens(Text self)
{
	return self->_nextToken != NULL;
}

Text
TextNextTokenC(Text self, const char *delims, int returnEmptyToken)
{
	Text token;
	char *stop, *string, *t;
	int quote = 0, escape = 0;

	string = self->_nextToken;

	if (string == NULL || delims == NULL)
		return NULL;

	/* Skip leading delimiters? */
	if (!returnEmptyToken) {
		/* Find start of next token. */
		string += strspn(string, delims);

		if (*string == '\0') {
			self->_nextToken = NULL;
			return NULL;
		}
	}

	/* Find end of token. */
	for (stop = string; *stop != '\0'; ++stop) {
		if (escape) {
			escape = 0;
			continue;
		}

		switch (*stop) {
		case '"': case '\'':
			quote = (int) *stop == quote ? 0 : (int) *stop;
			continue;
		case '\\':
			escape = 1;
			if (quote == 0) continue;
			continue;
		}

		if (quote == 0 && strchr(delims, *stop) != (char *) 0)
			break;
	}

	/*@-observertrans@*/
	if ((token = TextCreateN("", (stop - string) + 1)) == NULL)
		return NULL;
	/*@=observertrans@*/

	TextResetTokens(token);

	/* Copy token, removing quotes and backslashes. */
	for (t = token->_string; string < stop; ++string) {
		if (escape) {
			if (quote == 0)
				*t++ = (char) TextBackslash(*string);
			else
				*t++ = *string;
			escape = 0;
			continue;
		}

		switch (*string) {
		case '"': case '\'':
			quote = (int) *string == quote ? 0 : (int) *string;
			continue;
		case '\\':
			escape = 1;
			if (quote == 0) continue;
			break;
		}

		if (quote == 0 && strchr(delims, *string) != (char *) 0)
			break;

		*t++ = *string;
	}
	*t = '\0';

	token->_length = t - token->_string;

	if (*stop == '\0') {
		/* Token found and end of string reached.
		 * Next iteration should return no token.
		 */
		stop = NULL;
	} else {
		size_t length = strspn(stop, delims);
		if (returnEmptyToken) {
			/* Consume only a single delimter. */
			stop += (length != 0);
		} else {
			/* Consume one or more delimeters. */
			stop += length;
		}
	}

	self->_nextToken = stop;

	return token;
}

Text
TextNextToken(Text self, Text delims, int returnEmptyToken)
{
	return TextNextTokenC(self, (const char *) TextString(delims), returnEmptyToken);
}

Vector
TextSplitOnC(Text self, const char *delims, int returnEmptyTokens)
{
	Text token;
	Vector list;

	if ((list = VectorCreate(5)) == NULL)
		return NULL;

	TextResetTokens(self);

	while ((token = TextNextTokenC(self, delims, returnEmptyTokens)) != NULL) {
		if (VectorAdd(list, token)) {
			VectorDestroy(list);
			/*@-kepttrans@*/
			TextDestroy(token);
			/*@=kepttrans@*/
			return NULL;
		}
	}

	return list;
}

Vector
TextSplitOn(Text self, Text delims, int returnEmptyToken)
{
	return TextSplitOnC(self, (const char *) TextString(delims), returnEmptyToken);
}

/***********************************************************************
 *** Text*C instance methods
 ***********************************************************************/

#define TEXT_FUNC_C2(_return_type_, _base_function_)	\
_return_type_ 						\
_base_function_ ## C(Text self, const char *other)	\
{ 							\
	/*@dependent@*/ struct text temp; 		\
	TextInitFromString(&temp, other); 		\
	return _base_function_(self, &temp); 		\
}

#define TEXT_FUNC_C3(_return_type_, _base_function_, _type3_)		\
_return_type_ 								\
_base_function_ ## C(Text self, const char *other, _type3_ value)	\
{ 									\
	/*@dependent@*/ struct text temp; 				\
	TextInitFromString(&temp, other); 				\
	return _base_function_(self, &temp, value); 			\
}

TEXT_FUNC_C2(int, TextAppend)
TEXT_FUNC_C2(int, TextCompareCase)
TEXT_FUNC_C3(int, TextCompareCaseN, long)
TEXT_FUNC_C2(int, TextCompareIgnoreCase)
TEXT_FUNC_C3(int, TextCompareIgnoreCaseN, long)
TEXT_FUNC_C2(long, TextEndsWithCase)
TEXT_FUNC_C2(long, TextEndsWithIgnoreCase)
TEXT_FUNC_C2(long, TextFindCase)
TEXT_FUNC_C2(long, TextFindIgnoreCase)

/***********************************************************************
 *** Class methods
 ***********************************************************************/

void
TextInitEmptyString(Text self)
{
	static struct text model;

	if (model.objectName == NULL) {
		ObjectInit(&model);

		/* Overrides. */
		model.objectSize = sizeof (struct text);
		model.objectName = "Text";
		model.clone = TextClone;
		model.compare = TextCompareCase;
		model.equals = TextEqualsCase;
		model.hashcode = TextHashcodeCase;

		/* Methods */
		model.string = TextString;
		model.length = TextLength;

		model.isBlank = TextIsBlank;
		model.isInteger = TextIsInt;
		model.getOctet = TextGetOctet;
		model.setOctet = TextSetOctet;
		model.append = TextAppend;
		model.appendC = TextAppendC;

		model.getIgnoreCase = TextGetIgnoreCase;
		model.setIgnoreCase = TextSetIgnoreCase;

		model.compareCase = TextCompareCase;
		model.compareCaseC = TextCompareCaseC;
		model.compareIgnoreCase = TextCompareIgnoreCase;
		model.compareIgnoreCaseC = TextCompareIgnoreCaseC;

		model.equalsCase = TextEqualsCase;
		model.equalsIgnoreCase = TextEqualsIgnoreCase;

		model.hashcodeCase = TextHashcodeCase;
		model.hashcodeIgnoreCase = TextHashcodeIgnoreCase;

		model.compareCaseN = TextCompareCaseN;
		model.compareCaseNC = TextCompareCaseNC;
		model.compareIgnoreCaseN = TextCompareIgnoreCaseN;
		model.compareIgnoreCaseNC = TextCompareIgnoreCaseNC;

		model.findCase = TextFindCase;
		model.findCaseC = TextFindCaseC;
		model.findIgnoreCase = TextFindIgnoreCase;
		model.findIgnoreCaseC = TextFindIgnoreCaseC;

		model.endsWithCase = TextEndsWithCase;
		model.endsWithCaseC = TextEndsWithCaseC;
		model.endsWithIgnoreCase = TextEndsWithIgnoreCase;
		model.endsWithIgnoreCaseC = TextEndsWithIgnoreCaseC;

		model.startsWithCase = TextStartsWithCase;
		model.startsWithCaseC = TextStartsWithCaseC;
		model.startsWithIgnoreCase = TextStartsWithIgnoreCase;
		model.startsWithIgnoreCaseC = TextStartsWithIgnoreCaseC;

		model.invertCase = TextInvertCase;
		model.lowerCase = TextLowerCase;
		model.upperCase = TextUpperCase;
		model.reverse = TextReverseRegion;
		model.substring = TextGetSubstring;

		model.resetTokens = TextResetTokens;
		model.hasMoreTokens = TextHasMoreTokens;
		model.nextToken = TextNextToken;
		model.nextTokenC = TextNextTokenC;
		model.split = TextSplitOn;
		model.splitC = TextSplitOnC;

		model.objectMethodCount += 45;

		/* Instance variables. */
#ifdef ASSERT_STATIC_MEMBERS_ARE_NULL
		model._nextToken = NULL;
		model._string = NULL;
		model._length = 0;
#endif
	}

	*self = model;
}

void
TextInitFromString(Text self, const char *string)
{
	if (string != NULL) {
		TextInitEmptyString(self);
		self->_length = (long) strlen(string);
		/*@-mustfreeonly -temptrans@*/
		self->_string = (char *) string;
		/*@=mustfreeonly =temptrans@*/
		TextResetTokens(self);
	}
}

Text
TextCreateN(const char *string, long length)
{
	Text self;

	if (string == NULL || length < 0)
		return NULL;

	if ((self = malloc(sizeof (*self))) == NULL)
		return NULL;

	TextInitEmptyString(self);

	/*@-mustfreeonly@*/
	if ((self->_string = malloc((size_t)(length+1))) == NULL) {
		free(self);
		return NULL;
	}
	/*@=mustfreeonly@*/

	/* Overrides */
	self->destroy = TextDestroy;

	strncpy(self->_string, string, (size_t) length);
	self->_string[length] = '\0';
	self->_length = length;
	TextResetTokens(self);

	return self;
}

Text
TextCreate(const char *string)
{
	if (string == NULL)
		return NULL;

	return TextCreateN(string, (long) strlen(string));
}

Text
TextCreateFromInputLine(FILE *fp, long max)
{
	Buf *b;
	Text self;

	if (feof(fp) || ferror(fp))
		goto error0;

	if ((b = BufCreate(100)) == NULL)
		goto error0;

	/*@-observertrans@*/
	if ((self = TextCreateN("", 0)) == NULL)
		goto error1;
	/*@=observertrans@*/

	if (BufAddInputLine(b, fp, max) < 0)
		goto error2;

	free(self->_string);
	self->destroy = TextDestroy;
	self->_length = BufLength(b);
	/*@-mustfreefresh@*/
	self->_string = (char *) BufAsBytes(b);
	TextResetTokens(self);

	return self;
	/*@-usereleased@*/
error2:
	self->destroy(self);
error1:
	BufDestroy(b);
error0:
	return NULL;
	/*@=mustfreefresh =usereleased@*/
}

Text
TextCreateFromReadLine(int fd, long max)
{
	Buf *b;
	Text self;

	if ((b = BufCreate(100)) == NULL)
		goto error0;

	/*@-observertrans@*/
	if ((self = TextCreateN("", 0)) == NULL)
		goto error1;
	/*@=observertrans@*/

	if (BufAddReadLine(b, fd, max) < 0)
		goto error2;

	free(self->_string);
	self->destroy = TextDestroy;
	self->_length = BufLength(b);
	/*@-mustfreefresh@*/
	self->_string = (char *) BufAsBytes(b);
	TextResetTokens(self);

	return self;
	/*@-usereleased@*/
error2:
	self->destroy(self);
error1:
	BufDestroy(b);
error0:
	return NULL;
	/*@=mustfreefresh =usereleased@*/
}

/***********************************************************************
 *** Test
 ***********************************************************************/

#ifdef TEST

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

void
TestTextSplit(Text string, Text delims, int empty, long expect)
{
	int i;
	Text s;
	Vector v;

	printf("string=[%s] delims=[%s] return-empty-tokens=%d\n",TextString(string),TextString(delims), empty);
	v = TextSplitOn(string, delims, empty);

	if (v == NULL) {
		printf("vector is null\n");
		return;
	}

	printf("  length=%ld expected=%ld", VectorLength(v), expect);
	for (i = 0; i < VectorLength(v); i++) {
		s = VectorGet(v, i);
		printf("[%s]", s == NULL ? "" : s->string(s));
	}

	printf(" ...%s\n", VectorLength(v) == expect ? "OK" : "FAIL");
	VectorDestroy(v);

	string->destroy(string);
	delims->destroy(delims);
}

void
TestFunction(long (*cmp)(Text, Text), Text a, Text b, long expect)
{
	long result = (*cmp)(a, b);

	printf("a=[%s] b=[%s] expect=%ld, result=%ld %s\n", TextString(a), TextString(b), expect, result, expect == result ? "OK" : "FAIL");

	a->destroy(a);
	b->destroy(b);
}

void
TestCompareFunction(int (*cmp)(void *, void *), Text a, Text b, int expect)
{
	int result;

	result = (*cmp)(a, b);
	if (result < 0)
		result = -1;
	else if (0 < result)
		result = 1;
	else
		result = 0;

	printf("a=[%s] b=[%s] expect=%d, result=%d %s\n", TextString(a), TextString(b), expect, result, expect == result ? "OK" : "FAIL");

	a->destroy(a);
	b->destroy(b);
}

int
main(int argc, char **argv)
{
	Text a;
	struct text data;
	const char *abc = "abc";

	printf("\n--Text--\n");

	printf("init local stack object\n");
	TextInitEmptyString(&data);

	printf("destroy local stack object\n");
	data.destroy(&data);

	printf("\ncreate from string");
	isNotNull(a = TextCreate(abc));

	printf("length=%ld...%s\n", a->length(a), (size_t) a->length(a) == sizeof (abc)-1 ? "OK" : "FAIL");
	printf("content same as original...%s\n", memcmp(a->string(a), abc, sizeof (abc)) == 0 ? "OK" : "FAIL");

	printf("destroy a\n");
	a->destroy(a);

	printf("\nTextCompareIgnoreCase\n");
	TestCompareFunction(TextCompareIgnoreCase, TextCreate("a"), TextCreate("A"), 0);
	TestCompareFunction(TextCompareIgnoreCase, TextCreate("b"), TextCreate("A"), 1);
	TestCompareFunction(TextCompareIgnoreCase, TextCreate("a"), TextCreate("B"), -1);

	TestCompareFunction(TextCompareIgnoreCase, TextCreate("ab"), TextCreate("AB"), 0);
	TestCompareFunction(TextCompareIgnoreCase, TextCreate("ab"), TextCreate("A"), 1);
	TestCompareFunction(TextCompareIgnoreCase, TextCreate("a"), TextCreate("AB"), -1);

	TestCompareFunction(TextCompareIgnoreCase, TextCreate("ORDB.org"), TextCreate("ordborg"), -1);
	TestCompareFunction(TextCompareIgnoreCase, TextCreate("ORDB.org"), TextCreate("ordb.org"), 0);
	TestCompareFunction(TextCompareIgnoreCase, TextCreate("bitbucket@ORDB.org"), TextCreate("bitbucket@ordb.org"), 0);

	printf("\nTextEndsWith\n");
	TestFunction(TextEndsWithIgnoreCase, TextCreate(""), TextCreate("ordb.org"), -1);
	TestFunction(TextEndsWithIgnoreCase, TextCreate(".org"), TextCreate("ordb.org"), -1);
	TestFunction(TextEndsWithIgnoreCase, TextCreate("ORDB.org"), TextCreate("ordb.org"), sizeof("ordb.org")-1);
	TestFunction(TextEndsWithIgnoreCase, TextCreate("bitbucket@ORDB.org"), TextCreate("ordb.org"), sizeof("ordb.org")-1);

	printf("\nTextSplitOn\n");

	/* Empty string and empty delimeters. */
	TestTextSplit(TextCreate(""), TextCreate(""), 1, 1);		/* length=1 [] */
	TestTextSplit(TextCreate(""), TextCreate(""), 0, 0);		/* length=0 */

	/* Empty string. */
	TestTextSplit(TextCreate(""), TextCreate(","), 1, 1);		/* length=1 [] */
	TestTextSplit(TextCreate(""), TextCreate(","), 0, 0);		/* length=0 */

	/* Empty delimiters. */
	TestTextSplit(TextCreate("a,b,c"), TextCreate(""), 1, 1);		/* length=1 [a,b,c] */
	TestTextSplit(TextCreate("a,b,c"), TextCreate(""), 0, 1);		/* length=1 [a,b,c] */

	/* Assorted combinations of empty tokens. */
	TestTextSplit(TextCreate(","), TextCreate(","), 1, 2);		/* length=2 [][] */
	TestTextSplit(TextCreate(","), TextCreate(","), 0, 0);		/* length=0 */
	TestTextSplit(TextCreate("a,,"), TextCreate(","), 1, 3);		/* length=3 [a][][] */
	TestTextSplit(TextCreate("a,,"), TextCreate(","), 0, 1);		/* length=1 [a] */
	TestTextSplit(TextCreate(",b,"), TextCreate(","), 1, 3);		/* length=3 [][b][] */
	TestTextSplit(TextCreate(",b,"), TextCreate(","), 0, 1);		/* length=1 [b] */
	TestTextSplit(TextCreate(",,c"), TextCreate(","), 1, 3);		/* length=3 [][][c] */
	TestTextSplit(TextCreate(",,c"), TextCreate(","), 0, 1);		/* length=1 [c] */
	TestTextSplit(TextCreate("a,,c"), TextCreate(","), 1, 3);		/* length=3 [a][][c] */
	TestTextSplit(TextCreate("a,,c"), TextCreate(","), 0, 2);		/* length=2 [a][c] */
	TestTextSplit(TextCreate("a,b,c"), TextCreate(","), 1, 3);		/* length=3 [a][b][c] */
	TestTextSplit(TextCreate("a,b,c"), TextCreate(","), 0, 3);		/* length=3 [a][b][c] */

	/* Quoting of tokens. */
	TestTextSplit(TextCreate("a,b\\,c"), TextCreate(","), 1, 2);	/* length=2 [a][b,c] */
	TestTextSplit(TextCreate("a,b\\,c"), TextCreate(","), 0, 2);	/* length=2 [a][b,c] */
	TestTextSplit(TextCreate("a,'b,c'"), TextCreate(","), 1, 2);	/* length=2 [a][b,c] */
	TestTextSplit(TextCreate("a,'b,c'"), TextCreate(","), 0, 2);	/* length=2 [a][b,c] */
	TestTextSplit(TextCreate("\"a,b\",c"), TextCreate(","), 1, 2);	/* length=2 [a,b][c] */
	TestTextSplit(TextCreate("\"a,b\",c"), TextCreate(","), 0, 2);	/* length=2 [a,b][c] */
	TestTextSplit(TextCreate("a,'b,c'd,e"), TextCreate(","), 1, 3);	/* length=3 [a][b,cd][e] */
	TestTextSplit(TextCreate("a,'b,c'd,e"), TextCreate(","), 0, 3);	/* length=3 [a][b,cd][e] */
	TestTextSplit(TextCreate("a,'',e"), TextCreate(","), 1, 3);	/* length=3 [a][][e] */
	TestTextSplit(TextCreate("a,'',e"), TextCreate(","), 0, 3);	/* length=3 [a][][e] */
	TestTextSplit(TextCreate("a,b''d,e"), TextCreate(","), 1, 3);	/* length=3 [a][bd][e] */
	TestTextSplit(TextCreate("a,b''d,e"), TextCreate(","), 0, 3);	/* length=3 [a][bd][e] */

	printf("\n--DONE--\n");

	return 0;
}
#endif
