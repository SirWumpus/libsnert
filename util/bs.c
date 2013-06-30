/*
 * bs.c
 *
 * Binary String
 *
 * Copyright 2005, 2013 by Anthony Howe.  All rights reserved.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <com/snert/lib/version.h>
#include <com/snert/lib/util/bs.h>

#ifdef DEBUG_MALLOC
# include <com/snert/lib/util/DebugMalloc.h>
#endif

long
bsLength(unsigned char *bs)
{
	long length = 0;

	if (bs == NULL)
		return 0;

	while (*bs != 0) {
		length += *bs;
		bs += 1 + *bs;
	}

	return length;
}

unsigned char *
bsGetBytes(unsigned char *bs)
{
	int i;
	unsigned char *buffer, *buf;

	if (bs == NULL)
		return NULL;

	/* Allocate the continuous buffer plus one byte for an extra
	 * terminating NULL byte. The terminating NULL byte is NOT
	 * part of the data and is added for convience when using C
	 * string functions, assuming the source data contains no
	 * NULL bytes.
	 */
	if ((buffer = malloc(bsLength(bs) + 1)) == NULL)
		return NULL;

	for (buf = buffer; *bs != 0; ) {
		for (i = *bs++; 0 < i; i--, bs++, buf++) {
			*buf = *bs;
		}
	}
	*buf = '\0';

	return buffer;
}

long
bsPrint(FILE *fp, unsigned char *bs)
{
	int i, n;
	long length = 0;

	while (*bs != 0) {
		for (i = *bs++; 0 < i; i--, bs++) {
			if (isprint(*bs)) {
				if (*bs == '\\' || *bs == '"' || *bs == '\'') {
					if (fputc('\\', fp) == EOF)
						return -1;
					length++;
				}
				if (fputc(*bs, fp) == EOF)
					return -1;
				length++;
			} else {
				if ((n = fprintf(fp, "\\%.3o", *bs)) < 0)
					return -1;
				length += n;
			}
		}
	}

	return length;
}
