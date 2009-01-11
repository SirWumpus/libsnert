/*
 * nph-download.c
 *
 * Copyright 2004 by Anthony Howe.  All rights reserved.
 *
 * To build:
 *
 *	gcc -O2 -o nph-download.cgi nph-download.c
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#if defined(__BORLANDC__)
# include <io.h>
#endif

char usage[] =
"usage: nph-download.cgi\n"
"\n"
"DOCUMENT_ROOT\troot path of the document tree\n"
"QUERY_STRING\tfile within the document tree to download\n"
"\n"
"nph-download.cgi/1.0 Copyright 2004 by Anthony Howe.  All rights reserved.\n"
;

char buffer[BUFSIZ];

int
urldecode(char *t, long tsize, char *s)
{
	char *stop, *tstop;

	for (tstop = t + tsize - 1; t < tstop && *s != '\0'; t++, s++) {
		switch (*s) {
		case '+':
			*t = ' ';
			break;
		case '%':
			*t = (unsigned char) strtol(s+1, &stop, 16);
			if (s+3 != stop)
				return -1;
			s += 2;
			break;
		default:
			*t = *s;
		}
	}

	*t = '\0';

	return 0;
}

int
main(int argc, char **argv)
{
	FILE *fp;
	size_t n;
	struct stat sb;
	char path[512], *basename, *document_root, *query_string, *remote_user;

#if defined(__BORLANDC__)
	_fmode = O_BINARY;
	setmode(1, O_BINARY);
#endif

	query_string = getenv("QUERY_STRING");
	if (query_string == NULL || *query_string == '\0') {
		printf("HTTP/1.0 400 Bad Request\r\n\r\n400 Bad Request: missing QUERY_STRING\r\n");
		return 0;
	}

	document_root = getenv("DOCUMENT_ROOT");
	if (document_root == NULL || *document_root == '\0') {
		printf("HTTP/1.0 400 Bad Request\r\n\r\n400 Bad Request: missing DOCUMENT_ROOT\r\n");
		return 0;
	}

	/* Copy the request into a working buffer. */
	if (urldecode(path, sizeof (path), query_string)) {
		printf("HTTP/1.0 400 Bad Request\r\n\r\n400 Bad Request: URL encoding error\r\n");
		return 0;
	}

	if (strstr(path, "../") != NULL) {
		printf("HTTP/1.0 403 Forbidden\r\n\r\n403 Forbidden: relative paths disallowed\r\n");
		return 0;
	}

	/* Is it an absolute reference to a /home directory? */
	n = sizeof("/home")-1;
	remote_user = getenv("REMOTE_USER");
	if (remote_user != NULL && strncmp(path, "/home/", n) == 0) {
		/* Only accept a reference to /home/${REMOTE_USER}. */
		if (strncmp(path+n, remote_user, strlen(remote_user)) != 0) {
			printf("HTTP/1.0 403 Forbidden\r\n\r\n403 Forbidden: %s\r\n", path);
			return 0;
		}
	}

#ifdef ALLOW_ABSOLUTE_PATH
	else if (*path == '/') {
		/* Deny access to special directories. */
		if (strstr(path, "//") != NULL
		|| strncmp(path, "/etc/", sizeof("/etc/")-1) == 0
		|| strncmp(path, "/var/", sizeof("/var/")-1) == 0
		|| strncmp(path, "/tmp/", sizeof("/tmp/")-1) == 0
		|| strncmp(path, "/root/", sizeof ("/root/")-1) == 0
		|| strncmp(path, "/usr/local/etc/", sizeof ("/usr/local/etc/")-1) == 0
		) {
			printf("HTTP/1.0 403 Forbidden\r\n\r\n403 Forbidden: %s\r\n", path);
			return 0;
		}
	}
#else
	else if (*path == '/') {
		printf("HTTP/1.0 403 Forbidden\r\n\r\n403 Forbidden: %s\r\n", path);
		return 0;
	}
#endif
	else {
		/* Create path relative to DOCUMENT_ROOT. */
		snprintf(path, sizeof (path), "%s/%s", document_root, query_string);
		urldecode(path, sizeof (path), path);
	}

	if (stat(path, &sb) || S_ISDIR(sb.st_mode)) {
		printf("HTTP/1.0 404 Not Found\r\n\r\n404 Not Found: %s\r\n", path);
		return 0;
	}

	if ((basename = strrchr(path, '/')) == NULL) {
		printf("HTTP/1.0 404 Not Found\r\n\r\n404 Not Found\r\n");
		return 0;
	}
	basename++;

	if ((fp = fopen(path, "r")) == NULL) {
		printf("HTTP/1.0 404 Not Found\r\n\r\n404 Not Found: %s\r\n", path);
		return 0;
	}

	printf("HTTP/1.0 200 OK\r\n");
	printf("Cache: no-store, no-transform\r\n");
	printf("Content-Length: %lu\r\n", (long unsigned) sb.st_size);
	printf("Content-Transfer-Encoding: binary\r\n");
/*	printf("Content-Type: application/octet-stream; name=\"%s\"\r\n", basename); */
	printf("Content-Disposition: inline; filename=\"%s\"; size=%lu\r\n", basename, (long unsigned) sb.st_size);
	printf("\r\n");

	while (0 < (n = fread(buffer, 1, sizeof (buffer), fp)))
		fwrite(buffer, 1, n, stdout);

	fclose(fp);

	return 0;
}


