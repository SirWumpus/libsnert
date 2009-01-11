/*
 * counter.c
 *
 * Copyright 2004, 2005 by Anthony Howe.  All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __unix__
#include <sys/file.h>
#endif

#include <com/snert/lib/io/Log.h>
#include <com/snert/lib/io/file.h>
#include <com/snert/lib/io/posix.h>

/***********************************************************************
 *** Global Variables
 ***********************************************************************/

static char formatDigit[] = "%d";

static long fieldWidth = 5;
static int printFileName;
static char *(*_GET)[2];
static char *valueFormat = NULL;
static char *digitFormat = formatDigit;

char *usage =
"\033[1musage: counter [-cfn][-d format|-l format][-w width][file...]\033[0m\n"
"\n"
"-c\t\tis a CGI, write Content-Type header\n"
"-f\t\tprint filename after counter value\n"
"-n\t\tis a non-parsed header CGI, implies -c\n"
"-d format\tdigits format with {} as the digit marker, eg. \"Img/{}.gif\"\n"
"-l format\tvalue format with {} as the value marker, eg. \"counter = {};\"\n"
"-w width\tminimum field width, zero padded\n"
"file\t\ta counter file to update and display\n"
"\n"
"If the environment variable PATH_TRANSLATED is defined, then it refers to a\n"
"counter file to be update. So for example a counter could be updated and its\n"
"value displayed in a web page using server side includes:\n"
"\n"
"\t<!--#include virtual=\"/cgi-bin/counter.cgi/path/to/counter.dat\" -->\n"
"\n"
"If the executable is called nph-coutner.cgi its equivalent to -n. If the\n"
"executable is called counter.cgi its equivalent to -c.\n"
"\n"
"If the environment variable QUERY_STRING is defined, then it contains option\n"
"name=value strings; `digit', `value', and `width' correspond to -d, -l, and -w.\n"
"For example:\n"
"\n"
"\t<!--#include virtual=\"counter.cgi/counter.dat?\n"
"\tdigit=<img src%3D'/Digits/broadway-red/{}.gif'>&width=4\" -->\n"
"\n"
"\033[1mcounter/1.2 Copyright 2004, 2005 by Anthony Howe. All rights reserved.\033[0m\n"
;

/***********************************************************************
 *** Routines
 ***********************************************************************/

/**
 * @param tp
 *	A pointer to pointer of char. Decoded bytes from the source
 *	string are copied to this destination. The destination buffer
 *	must be as large as the source. The copied string is '\0'
 *	terminated and the pointer passed back points to next byte
 *	after the terminating '\0'.
 *
 * @param sp
 * 	A pointer to pointer of char. The URL encoded bytes are copied
 *	from this source buffer to the destination. The copying stops
 *	after an equals-sign, ampersand, or on a terminating '\0' and
 *	this pointer is passed back.
 */
void
cgiUrlDecode(char **tp, char **sp)
{
	int hex;
	char *t, *s;

	for (t = *tp, s = *sp; *s != '\0'; t++, s++) {
		switch (*s) {
		case '=':
		case '&':
			s++;
			break;
		case '+':
			*t = ' ';
			continue;
		case '%':
			if (sscanf(s+1, "%2x", &hex) == 1) {
				*t = (char) hex;
				s += 2;
				continue;
			}
			/*@fallthrough@*/
		default:
			*t = *s;
			continue;
		}
		break;
	}

	/* Terminate decoded string. */
	*t = '\0';

	/* Pass back the next unprocessed location.
	 * For the source '\0' byte, we stop on that.
	 */
	*tp = t+1;
	*sp = s;
}

/**
 * @param urlencoded
 *	A URL encoded string such as the query string portion of an HTTP
 *	request or HTML form data ie. application/x-www-form-urlencoded.
 *
 * @return
 *	A pointer to array 2 of pointer to char. The first column of the
 *	table are the field names and the second column their associated
 *	values. The array is NULL terminated. The array pointer returned
 *	must be released with a single call to free().
 */
char *(*cgiParseForm(char *urlencoded))[2]
{
	int nfields, i;
	char *s, *t, *(*out)[2];

	if (urlencoded == NULL)
		return NULL;

	nfields = 1;
	for (s = urlencoded; *s != '\0'; s++) {
		if (*s == '&')
			nfields++;
	}

	if ((out = malloc((nfields + 1) * sizeof (*out) + strlen(urlencoded) + 1)) == NULL)
		return NULL;

	s = urlencoded;
	t = (char *) &out[nfields+1];

	for (i = 0; i < nfields; i++) {
		out[i][0] = t;
		cgiUrlDecode(&t, &s);

		out[i][1] = t;
		cgiUrlDecode(&t, &s);
	}

	out[i][0] = NULL;
	out[i][1] = NULL;

	return out;
}

static void
printDigit(unsigned long value, int width)
{
	if (0 < value || 0 < width) {
		printDigit(value / 10, width - 1);
		(void) printf(digitFormat, (int)(value % 10));
	}
}

static int
counter(char *filename)
{
	FILE *fp;
	unsigned long value;

	if (filename == NULL)
		return 0;

	if ((fp = fopen(filename, "r+")) == NULL) {
		if (errno == ENOENT && (fp = fopen(filename, "w+")) == NULL)
			return -1;
	}

	flock(fileno(fp), 2);

	value = 0;
	(void) fscanf(fp, "%lu", &value);
	value++;

	if (valueFormat == NULL)
		printDigit(value, fieldWidth);
	else
		printf(valueFormat, value);

	if (printFileName)
		printf(" %s\n", filename);
	else
		printf("\n");

	rewind(fp);
	(void) fprintf(fp, "%lu\r\n", value);

	return fclose(fp);
}

static int
repalceMarker(char *format)
{
	/* Only replace {} if there are no percent signs
	 * within the marker string that could cause an
	 * error for printf().
	 */
	if (format[strcspn(format, "%")] == '%')
		return -1;

	format += strcspn(format, "{");

	if (format[0] == '{' && format[1] == '}') {
		format[0] = '%';
		format[1] = 'd';
	}

	return 0;
}

int
main(int argc, char **argv)
{
	char *pt;
	int i, argi, isCGI, isNPH;

#if defined(__BORLANDC__)
	_fmode = O_BINARY;
#endif
#if defined(__BORLANDC__)
	setmode(1, O_BINARY);
#endif

	if ((pt = strrchr(argv[0], '/')) == NULL
	&&  (pt = strrchr(argv[0], '\\')) == NULL)
		pt = argv[0];
	else
		pt++;

	isNPH = strncmp(pt, "nph-", 4) == 0;
	isCGI = strstr(pt, ".cgi") != NULL;

	if ((_GET = cgiParseForm(getenv("QUERY_STRING"))) != NULL) {
		for (i = 0; _GET[i][0] != NULL; i++) {
			if (strcmp(_GET[i][0], "digit") == 0) {
				digitFormat = _GET[i][1];
				if (repalceMarker(digitFormat))
					digitFormat = formatDigit;
				continue;
			}
			if (strcmp(_GET[i][0], "value") == 0) {
				valueFormat = _GET[i][1];
				if (repalceMarker(valueFormat))
					valueFormat = NULL;
				continue;
			}
			if (strcmp(_GET[i][0], "width") == 0) {
				fieldWidth = strtol(_GET[i][1], NULL, 10);
				continue;
			}
			if (strcmp(_GET[i][0], "debug") == 0) {
				LogSetLevel(LOG_DEBUG);
				continue;
			}
		}
	}

	for (argi = 1; argi < argc; argi++) {
		if (argv[argi][0] != '-')
			break;

		switch (argv[argi][1]) {
		case 'c':
			isCGI = 1;
			break;
		case 'f':
			printFileName = 1;
			break;
		case 'n':
			isNPH = 1;
			break;
		case 'd':
			digitFormat = argv[argi][2] == '\0' ? argv[++argi] : &argv[argi][2];
			if (repalceMarker(digitFormat))
				fprintf(stderr, "string \"%s\" contains percent sign", digitFormat);
			break;
		case 'l':
			valueFormat = argv[argi][2] == '\0' ? argv[++argi] : &argv[argi][2];
			if (repalceMarker(valueFormat))
				fprintf(stderr, "string \"%s\" contains percent sign", valueFormat);
			break;
		case 'w':
			fieldWidth = strtol(argv[argi][2] == '\0' ? argv[++argi] : &argv[argi][2], NULL, 10);
			break;
		case 'v':
			LogSetLevel(LOG_DEBUG);
			break;
		default:
			fprintf(stderr, "invalid option -%c\n%s", argv[argi][1], usage);
			return 2;
		}
	}

	if ((pt = getenv("PATH_TRANSLATED")) == NULL && argc <= argi) {
		fprintf(stderr, "%s", usage);
		return 2;
	}

	if (isNPH) {
		pt = getenv("SERVER_PROTOCOL");
		printf("%s 200 OK\r\n", pt == NULL ? "HTTP/1.0" : pt);
		isCGI = 1;
	}

	if (isCGI)
		printf("Content-Type: text/plain; charset=US-ASCII\r\n\r\n");

	if ((pt = getenv("PATH_TRANSLATED")) != NULL && counter(pt))
		fprintf(stderr, "counter %s: %s (%d)\n", pt, strerror(errno), errno);

	for ( ; argi < argc; argi++) {
		if (counter(argv[argi]))
			fprintf(stderr, "counter %s: %s (%d)\n", argv[argi], strerror(errno), errno);
	}

	return 0;
}

