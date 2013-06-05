#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#define STRLEN(s)			(sizeof (s)-1)
#define STARTS_WITH_IC(s,pre)		(strncasecmp(s, pre, STRLEN(pre)) == 0)

int imap4_search_key(const char *s, char **stop);

/**
 * @param s
 *	C string pointer to start of IMAP search term number.
 *
 * @param stop
 *      A pointer to a C string pointer. The point were the scan stops
 *      is passed back through this argument. This pointer can be NULL.
 *
 * @return
 *	Zero on successful parse, otherwise non-zero on error.
 */
static int
imap4_search_number(const char *s, char **stop)
{
	(void) strtol(s, stop, 10);

	return s == *stop;
}

/**
 * @param s
 *	C string pointer to start of IMAP sequence set.
 *
 * @param stop
 *      A pointer to a C string pointer. The point were the scan stops
 *      is passed back through this argument. This pointer can be NULL.
 *
 * @return
 *	Zero on successful parse, otherwise non-zero on error.
 */
static int
imap4_search_sequence(const char *s, char **stop)
{
	unsigned long start, finish;

	while (isdigit(*s)) {
		start = strtoul(s, stop, 10);
		if (s == *stop)
			return 1;
		s = *stop;
		if (*s == ':') {
			finish = strtoul(s+1, stop, 10);
			if (s == *stop)
				return 1;
		}
		s = *stop;
		if (*s == ',')
			s++;
	} 

	return *s != ' ' && *s != ')' && *s != '\0';
}

/**
 * @param s
 *	C string pointer to start of IMAP string.
 *
 * @param stop
 *      A pointer to a C string pointer. The point were the scan stops
 *      is passed back through this argument. This pointer can be NULL.
 *
 * @return
 *	Zero on successful parse, otherwise non-zero on error.
 */
static int
imap4_search_string(const char *s, char **stop)
{
	int escape, quote;

	if (*s == '"' || *s == '\'') {
		/* Quoted string. */
		for (escape = 0, quote = *s++; isprint(*s); s++) {
			if (!escape && *s == quote) {
				s++;
				break;
			}
			escape = *s == '\\';
		}
		*stop = (char *)s;
	} else {
		/* Bare word (atom). */
		*stop = (char *)s + strcspn(s, " )");
	}

	return 0;
}

/**
 * @param s
 *	C string pointer to start of IMAP search term.
 *
 * @param stop
 *      A pointer to a C string pointer. The point were the scan stops
 *      is passed back through this argument. This pointer can be NULL.
 *
 * @return
 *	Zero on successful parse, otherwise non-zero on error.
 */
static int
imap4_search_term(const char *s, char **stop)
{
	int rc = 1;
	char *next;

	next = (char *)s + strcspn(s, " )");
	if (*s == '(') {
		/* Expression */
		(void) imap4_search_key(s+1, &next);
		if (*next == ')') {
			next++;
			rc = 0;
		}
	} else if (isdigit(*s)) {
		rc = imap4_search_sequence(s, &next);
	} else if (STARTS_WITH_IC(s, "ALL")) {
		rc = 0;
	} else if (STARTS_WITH_IC(s, "LARGER")) {
		rc = *next != ' ' || imap4_search_number(next+1, &next);
	} else if (STARTS_WITH_IC(s, "SMALLER")) {
		rc = *next != ' ' || imap4_search_number(next+1, &next);
	} else if (STARTS_WITH_IC(s, "TEXT")) {
		rc = *next != ' ' || imap4_search_string(next+1, &next);
	} else if (STARTS_WITH_IC(s, "NOT")) {
		rc = *next != ' ' || imap4_search_key(next+1, &next);
	} else if (STARTS_WITH_IC(s, "OR")) {
		rc =  *next != ' ' || imap4_search_term(next+1, &next)
		   || *next != ' ' || imap4_search_term(next+1, &next)
		;
	} else {
		/* Unknown search term. */
		next = (char *)s;
	}
	*stop = next;	

	return rc;
}

/**
 * @param s
 *	C string pointer to start of IMAP search key string.
 *
 * @param stop
 *      A pointer to a C string pointer. The point were the scan stops
 *      is passed back through this argument. This pointer can be NULL.
 *
 * @return
 *	Zero on successful parse, otherwise non-zero on error.
 */
int
imap4_search_key(const char *s, char **stop)
{
	int rc;

	do {
		rc = imap4_search_term(s + strspn(s, " "), stop);
		s = *stop;
	} while (rc == 0 && *s == ' ');

	return rc;
}

int
main(int argc, char **argv)
{
	int ch;
	char *next;

	while ((ch = getopt(argc, argv, "")) != -1) {
		switch (ch) {
		default:
			optind = argc;
		}
	}
	if (argc <= optind) {
		(void) fprintf(stderr, "usage: sk string\n");
		return 2;
	}
	if (imap4_search_key(argv[1], &next)) {
		(void) fprintf(stderr, "%s\n", argv[1]);
		(void) fprintf(stderr, "%*s^ parse error\n", next-argv[1], "");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
