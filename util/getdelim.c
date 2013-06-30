/*
 * getdelim.c
 *
 * Copyright 2012, 2013 by Anthony Howe. All rights reserved.
 */

#include <com/snert/lib/version.h>

#ifndef HAVE_GETDELIM

# include <errno.h>
# include <stdio.h>
# include <stdlib.h>

# include <com/snert/lib/util/Text.h>

# ifdef DEBUG_MALLOC
#  include <com/snert/lib/util/DebugMalloc.h>
# endif

# define LINE_SIZE	512

ssize_t
getdelim(char **linep, size_t *np, int delim, FILE *fp)
{
	int ch;
	char *line, *copy;
	ssize_t offset, size;

	if (linep == NULL || np == NULL || fp == NULL) {
		errno = EFAULT;
		return -1;
	}
	if (*linep == NULL)
		*np = 0;

	size = *np;
	offset = 0;
	line = *linep;
	while ((ch = fgetc(fp)) != EOF) {
		if (size <= offset) {
			if ((copy = realloc(line, size+LINE_SIZE)) == NULL)
				return -1;
			size += LINE_SIZE;
			line = copy;
		}
		line[offset++] = ch;
		if (ch == delim)
			break;
	}
	*linep = line;
	*np = offset;

	return offset;
}

# ifndef HAVE_GETLINE
ssize_t
getline(char **linep, size_t *np, FILE *fp)
{
	return getdelim(linep, np, '\n', fp);
}
# endif
#endif /* HAVE_GETDELIM */
