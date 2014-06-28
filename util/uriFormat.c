/*
 * uriFormat.c
 *
 * RFC 6570 (level 3)
 *
 * Copyright 2014 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <com/snert/lib/type/hash2.h>
#include <com/snert/lib/util/Buf.h>
#include <com/snert/lib/util/uri.h>
#include <com/snert/lib/util/Text.h>

/* RFC 3986
 *
 * Note that vertical bar (|) is not excluded, since it can
 * appear in the file: scheme, eg. "file:///C|/foo/bar/"
 * Assume it is reserved.
 */
//static char uri_excluded[] = "\"<>{}\\^`";
static char uri_reserved[] = "%:/?#[]@!$&'()*+,;=|";
static char uri_unreserved[] = "-_.~";

char *
uriEncode2(const char *string, int flags)
{
	size_t length;
	char *out, *op;
	static const char hex_digit[] = "0123456789ABCDEF";

	length = strlen(string);
	if ((out = malloc(length * 3 + 1)) == NULL)
		return NULL;

	for (op = out ; *string != '\0'; string++) {
		if (isalnum(*string)
		|| strchr(uri_unreserved, *string) != NULL
		|| ((flags & URI_ENC_IGNORE_RESERVED) && strchr(uri_reserved, *string) != NULL)) {
			*op++ = *string;
		} else {
			*op++ = '%';
			*op++ = hex_digit[(*string >> 4) & 0x0F];
			*op++ = hex_digit[*string & 0x0F];
		}
	}
	*op = '\0';

	return out;
}

/**
 * @param s
 *	A pointer to a URI decoded C string.
 *
 * @return
 *	A pointer to an allocated C string containing the encoded URI.
 *	Its the caller's responsibility to free() this pointer.
 */
char *
uriEncode(const char *string)
{
	return uriEncode2(string, 0);
}

typedef struct {
	int op;
	const char first[2];
	const char  next[2];
	int both_key_value;
	int encode_flags;
} Delims;

/* See RFC 6570 Appendix A table */
static Delims delims[] = {
	{ '+',  "",   ",",  0, URI_ENC_IGNORE_RESERVED },
	{ '.',  ".",  ".",  0, 0 },
	{ '/',  "/",  "/",  0, 0 },
	{ ';',  ";",  ";",  1, 0 },
	{ '?',  "?",  "&",  1, 0 },
	{ '&',  "&",  "&",  1, 0 },
	{ '#',  "#",  ",",  0, URI_ENC_IGNORE_RESERVED },
	{ '\0', "",   ",",  0, 0 }
};

char *
uriFormat(const char *fmt, Hash *vars)
{
	int op, span;
	Delims *delim;
	Buf *collector;
	Vector split, keys;
	const char *sep;
	char *el, **item, **key, *value, *result, *encoded;

	result = NULL;

	if (fmt == NULL || vars == NULL || (collector = BufCreate(128)) == NULL)
		goto error0;

	if ((split = TextSplit(fmt, "{}", TOKEN_KEEP_BRACKETS|TOKEN_KEEP_EMPTY)) == NULL)
		goto error1;

	keys = NULL;
	encoded = NULL;

	for (item = (char **)VectorBase(split); *item != NULL; item++) {
		el = *item;
		if (*el != '{') {
			/* Add literal to URI. */
			if (BufAddString(collector, el))
				goto error2;
			continue;
		}

		/* Start of template, get operator. */
		el++;
		op = ispunct(*el) ? *el++ : '\0';
		span = strcspn(el, "}");
		el[span] = '\0';

		/* Find operator's delimeter set. */
		for (delim = delims; delim->op != '\0'; delim++) {
			if (delim->op == op)
				break;
		}
		sep = delim->first;

		/* Process list of variables. */
		if ((keys = TextSplit(el, ",", 0)) == NULL)
			goto error2;
		for (key = (char **)VectorBase(keys); *key != NULL; key++) {
			value = hash_get(vars, *key);
			if (delim->both_key_value) {
				if ((encoded = uriEncode(*key)) == NULL)
					goto error3;
				if (BufAddString(collector, sep))
					goto error4;
				if (BufAddString(collector, encoded))
					goto error4;
				if (op != ';' || (value != NULL && *value != '\0')) {
					if (BufAddByte(collector, '='))
						goto error4;
				}
				free(encoded);
				encoded = NULL;
			}
			if (value != NULL) {
				if ((encoded = uriEncode2(value, delim->encode_flags)) == NULL)
					goto error3;
				if (!delim->both_key_value && BufAddString(collector, sep))
					goto error4;
				if (BufAddString(collector, encoded))
					goto error4;
				sep = delim->next;
				free(encoded);
				encoded = NULL;
			}
		}
		VectorDestroy(keys);
		keys = NULL;
	}

	result = (char *)BufAsBytes(collector);
	collector = NULL;
error4:
	free(encoded);
error3:
	VectorDestroy(keys);
error2:
	VectorDestroy(split);
error1:
	BufFree(collector);
error0:
	return result;
}

#ifdef TEST

#include <assert.h>
#include <com/snert/lib/util/getopt.h>
#include <com/snert/lib/sys/sysexits.h>

static const char usage[] =
"usage: uriFormat [-T][-v key=value] format\n"
;

typedef struct {
	const char *format;
	const char *expect;
} Test;

static char *test_vars[] = {
	"empty=",
	"K1=VAL1",
	"K2=VAL2",
	"K3=VAL3",
	"label=example.com",
	"path=/foo/bar/bat",
	"phrase=Hello World!",
	"specials= %7F/?#&@+",
	NULL
};

static Test tests[] = {
	{ "literal", "literal" },
	{ "empty=[{empty}]", "empty=[]" },
	{ "var K1=[{K1}]", "var K1=[VAL1]" },
	{ "encode [{phrase}]", "encode [Hello%20World%21]" },
	{ "list {K1,K2,K3}", "list VAL1,VAL2,VAL3" },
	{ "{K1} leading", "VAL1 leading" },
	{ "sequence {K1}{K2}{K3}", "sequence VAL1VAL2VAL3" },
	{ "reserved {+K1,specials,K3}", "reserved VAL1,%20%7F/?#&@+,VAL3" },
	{ "www{.label}", "www.example.com" },
	{ "{/path}{?K1,K2}{&K3}{#K3}", "/%2Ffoo%2Fbar%2Fbat?K1=VAL1&K2=VAL2&K3=VAL3#VAL3" },
	{ "{+path}{;empty,K1,K2}", "/foo/bar/bat;empty;K1=VAL1;K2=VAL2" },
	{ NULL, NULL }
};

static void
addvar(Hash *vars, const char *var)
{
	int span;

	span = strcspn(var, "=");
	if (hash_putk(vars, var, span, strdup(var+span+1)))
		abort();
}

int
main(int argc, char **argv)
{
	Test *t;
	Hash *vars;
	char *uri, **v;
	int ch, run_tests;

	run_tests = 0;
	vars = hash_create();
	assert(vars != NULL);

	while ((ch = getopt(argc, argv, "Tv:")) != -1) {
		switch (ch) {
		case 'T':
			run_tests = 1;
			break;
		case 'v':
			addvar(vars, optarg);
			break;
		default:
			optind = argc;
		}
	}
	if (!run_tests && argc <= optind) {
		fprintf(stderr, usage);
		return EX_USAGE;
	}

	if (run_tests) {
		for (v = test_vars; *v != NULL; v++)
			addvar(vars, *v);
		for (t = tests; t->format != NULL; t++) {
			uri = uriFormat(t->format, vars);
			printf("     [%s] [%s]", t->format, TextNull(uri));
			printf("\r%s\n", strcmp(t->expect, uri) == 0 ? "OK" : "FAIL");
			free(uri);
		}
	} else if ((uri = uriFormat(argv[optind], vars)) != NULL) {
		printf("%s\n", TextNull(uri));
		free(uri);
	}

	hash_destroy(vars);

	return EX_OK;
}

#endif
