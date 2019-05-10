/*
 * jspr (Jasper) - JSON string path recovery
 *
 * A simple JSON object string reader that can find JSON objects
 * by their path for substring extraction and processing.
 *
 * To build CLI tool:
 *
 *	cc -DTEST -ojspr jspr.c
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jspr.h"

#define WS		" \t\n\r\f"
#define COMMA_WS	"," WS
#define OBJECT_WS	"}]" COMMA_WS

int jspr_debug;

/*
 * @param str
 *	A C string to scan.
 *
 * @param stop
 *	A pointer to a C string pointer to passback where the
 *	token scanner stopped.
 *
 * @param delims
 *	A C string containing a set of delimiters that termainate
 *	a JSON token.  Can be NULL to default to C whitespace.
 *
 * @return
 *	A pointer within str to the start of a token.  A token can
 *	be a double quoted string (including quotes), a JSON object
 *	{ ... } string, or a JSON array [ ... ] string, or an
 *	unquoted string.  Backslash can be used to escape itself, a
 *	double-quote, or any other character.
 */
static const char *
jspr_token(const char *str, const char **stop, const char *delims)
{
	const char *s;
	int bcount = 0, escape = 0, quote = 0, open, close;

	if (delims == NULL)
		delims = WS;
	str += strspn(str, WS);

	switch (*str) {
	case '{':
		open = '{'; close = '}'; break;
	case '[':
		open = '['; close = ']'; break;
	default:
		open = close = -1;
	}

        for (s = str; *s != '\0'; s++) {
                if (escape) {
                        escape = 0;
                        continue;
                }

                switch (*s) {
                case '"': case '\'':
                        if (quote == 0)
                                quote = *s;
                        else if (*s == quote)
                                quote = 0;
                        continue;
                case '\\':
                        escape = 1;
                        continue;
                }

               	if (quote == 0) {
			if (*s == open)
				bcount++;
			else if (*s == close)
				bcount--;
			if (bcount == 0 && strchr(delims, *s) != NULL)
               			break;
		}
        }

	if (stop != NULL)
		*stop = s;

#ifndef NDEBUG
        if (jspr_debug) {
		int span = (int)(s - str);
		(void) fprintf(stderr, "%d:%.*s\n", span, span, str);
	}
#endif
	return str;
}

/*
 * @param js
 *	A JSON object or array string.
 *
 * @param end
 *	A pointer to a C string pointer to passback where the
 *	scanner stopped.
 *
 * @param key
 *	A key label or a numeric index string.  If key is an index
 *	and the current value being walked is an object, then index
 *	will refer to the Nth key found.
 *
 * @return
 *	A pointer within js to the start of the key string or the Nth
 *	element of an array.
 */
static const char *
jspr_find(const char *js, const char **end, const char *key)
{
	int index, ws;
	const char *stop;

	// Array index?
	index = isdigit(*key) ? strtol(key, NULL, 10) : -1;

	js += strspn(js, WS);
	if (*js == '{') {
		for (js++; *js != '\0'; js = stop) {
			// Length of key.
			js = jspr_token(js, &stop, ":}" WS);
			ws = strspn(stop, WS);
			*end = stop;

			// Colon delimiter following key?
			if (stop[ws] != ':')
				break;

			// Found key?
			if (index < 0) {
				if (strncmp(js, key, stop-js) == 0
				|| (*js == '"' && strncmp(js+1, key, stop-js-2) == 0)) {
					return js;
				}
			} else if (--index < 0) {
				return js;
			}

			// Skip value.
			js = jspr_token(stop + ws + 1, &stop, COMMA_WS);
			stop += strspn(stop, WS);
			if (*stop == ',')
				stop++;
			stop += strspn(stop, WS);

			// End of object?
			if (*stop == '}')
				break;
		}
	} else if (*js == '[' && 0 <= index) {
		for (js++; *js != '\0'; js = stop) {
			js = jspr_token(js, &stop, COMMA_WS);
			stop += strspn(stop, WS);
			*end = stop;

			if (--index < 0) {
				// Return start of value.
				return js;
			}

			// Next item.
			if (*stop == ',')
				stop++;
			stop += strspn(stop, WS);

			// End of (empty) array?
			if (*stop == ']')
				break;
		}
	}

	return NULL;
}

/**
 * @param js
 *	A JSON object or array string.
 *
 * @param labels
 *	A NULL terminated array of JSON labels to search.  A label
 *	can be an array index.  If a label is an index and the
 *	current value being walked is an object, then index will
 *	refer to the Nth key found.
 *
 * @param span
 *	Passback the span of the value string found.
 *
 * @param flags
 *	If JSPR_KEY_NAME is passed, return the key name found
 *	at object index instead, the value.
 *
 * @return
 *	A pointer within js to the start of the JSON value string.
 *	If value is a string, the opening double quote is skipped,
 *	and span reduced by one to skip the closing double quote.
 */
const char *
jspr_find_labels(const char *js, const char **labels, int *span, int flags)
{
	const char **label, *stop;

	if (js == NULL)
		return NULL;
	if (labels == NULL || *labels == NULL) {
		*span = (int) strlen(js);
		return js;
	}

	for (label = labels; *label != NULL; label++) {
		if ((js = jspr_find(js, &stop, *label)) == NULL)
			return NULL;
		if (*stop == ':' && (label[1] != NULL || !(flags & JSPR_KEY_NAME))) {
			// Find key's value.
			js = stop + strspn(stop+1, WS) + 1;
		}
	}

	if (flags & JSPR_KEY_NAME) {
		if (labels < label && *stop != ':') {
			js = label[-1];
			stop = js + strlen(js);
		}
	} else {
		js = jspr_token(js, &stop, strchr("{[", *js) == NULL ? OBJECT_WS : COMMA_WS);
	}

	if (*js == '"') {
		while (*--stop != '"')
			;
		js++;
	}
	*span = stop - js;

	return 0 < *span ? js : NULL;
}

/**
 * @param js
 *	A JSON object or array string.
 *
 * @param path
 *	A dot separated string of labels.  A label can be a array
 *	index.  If a label is an index and the current value being
 *	walked is an object, then index will refer to the Nth key
 *	found.
 *
 * @param span
 *	Passback the span of the value string found.
 *
 * @param flags
 *	If JSPR_KEY_NAME is passed, return the key name found
 *	at object index instead, the value.
 *
 * @return
 *	A pointer within js to the start of the JSON value string.
 *	If value is a string, the opening double quote is skipped,
 *	and span reduced by one to skip the closing double quote.
 */
const char *
jspr_find_path(const char *js, const char *path, int *span, int flags)
{
	char *p;
	int i, nlabels;
	const char **labels;

	if (js == NULL || *js == '\0')
		return NULL;
	if (path == NULL || *path == '\0') {
		*span = (int) strlen(js);
		return js;
	}

	// Count labels.
	for (nlabels = 1, p = (char *)path; *p != '\0'; p++) {
		if (*p == '.')
			nlabels++;
	}

	// Array of label pointers followed by a copy of path.
	if ((labels = malloc((nlabels+1) * sizeof (*labels) + strlen(path)+1)) == NULL)
		return NULL;

	p = (char *) &labels[nlabels+1];
	(void) strcpy(p, path);

	// Build array of label pointers.
	for (i = 0; i < nlabels; i++) {
		labels[i] = p;
		p += strcspn(p, ".");
		*p++ = '\0';
	}
	labels[i] = NULL;

	js = jspr_find_labels(js, labels, span, flags);
	free(labels);

	return js;
}

#ifdef TEST
#include <err.h>
#include <getopt.h>

char filemsg[] = "File \"%s\" ";

/*
 * @param fn
 *	A C string of a file path.  NULL or "-" refers to stdin.
 *
 * @return
 *	An allocated C string pointer of the read file.  The caller
 *	is responsibling for freeing this pointer.
 */
char *
read_file(const char *fn)
{
	FILE *fp;
	char *buf;
	size_t size, length;

	if (fn == NULL || *fn == '-') {
		fp = stdin;
	} else if ((fp = fopen(fn, "rb")) == NULL) {
		err(1, filemsg, fn);
	}

	buf = NULL;
	length = 0;
	for (size = 512; !feof(fp); ) {
		size += size;
		if ((buf = realloc(buf, size)) == NULL)
			err(1, filemsg, fn);
		length += fread(buf+length, sizeof (char), size-length-1, fp);
	}

	buf[length] = '\0';
	(void) fclose(fp);

	return buf;
}

static const char usage[] =
"usage: jspr [-klv] file [label ...]\n"
"\n"
"-k\t\twrite the key name found at index\n"
"-l\t\twrite the length of the value\n"
"-v\t\tverbose debug\n"
"\n"
"Given a JSON file and a list of key labels, scan the object to find\n"
"the given key and write to standard output its value.  If no labels\n"
"are given, write the whole object.  A label can be an array index.\n"
"If a label is an index and the current value being walked is an\n"
"object, then index will refer to the Nth key found.\n"
"\n"
;

int
main(int argc, char **argv)
{
	char *buf;
	int ch, span;
	const char *val;
	int flags = 0;
	int write_length = 0;

	while ((ch = getopt(argc, argv, "klv")) != -1) {
		switch (ch) {
		case 'k':
			flags |= JSPR_KEY_NAME;
			break;
		case 'l':
			write_length = 1;
			break;
		case 'v':
			jspr_debug++;
			break;
		default:
			optind = argc;
		}
	}
	if (argc <= optind) {
		(void) fprintf(stderr, usage);
		exit(2);
	}

	buf = read_file(argv[optind++]);

	if (argv[optind] != NULL && strchr(argv[optind], '.') == NULL)
		val = jspr_find_labels(buf, (const char **)(argv+optind), &span, flags);
	else
		val = jspr_find_path(buf, argv[optind], &span, flags);

	if (val != NULL) {
		if (write_length)
			(void) printf("%d\n", span);
		(void) printf("%.*s\n", span, val);
	}

	free(buf);

	return val == NULL;
}
#endif /* TEST */
