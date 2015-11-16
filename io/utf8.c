/*
 * utf8.c
 *
 * Copyright 2015 by Anthony Howe.  All rights reserved.
 */

#include <errno.h>

#include <com/snert/lib/version.h>
#include <com/snert/lib/io/file.h>

#ifndef WERROR
#define WERROR		((wint_t) -2)
#endif

wint_t
fgetwc_utf8(FILE *fp)
{
	int x;
	wint_t wc;
	size_t length;
	unsigned char mb[6];

	if (fp == NULL) {
		errno = EFAULT;
		return WEOF;
	}

	if ((x = fgetc(fp)) == EOF)
		return WEOF;
#ifdef V1
	if (x < 0x80)
		/* 0xxxxxxx : ASCII */	
		return (wint_t) x;
	
	if (x < 0xC0)
		/* 10xxxxxx : in middle of multibyte character sequence. */
		return (wint_t) -2;
	else if (x < 0xE0)
		/* 110xxxxx 10xxxxxx */
		length = 2;
	else if (x < 0xF0)
		/* 1110xxxx 10xxxxxx 10xxxxxx */
		length = 3;
	else if (x < 0xF8)
		/* 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx */
		length = 4;

 	/* RFC 3629 restricts UTF-8 to fit into Unicode-16. */
	else if (x < 0xFC)
		/* 111110xx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx */
		length = 5;
	else if (x < 0xFE)
		/* 1111110x 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx */
		length = 6;
	else 
		return (wint_t) -3;
#else
	signed char mask;
	for (length = 0, mask = 0x80; (mask & x) == (unsigned char)mask; mask >>= 1)
		length++;

	switch (length) {
	case 0:
		/* 0xxxxxxx : ASCII */	
		return (wint_t) x;
	case 1: 
		/* 10xxxxxx : in middle of multibyte character sequence. */
	case 7: case 8:
		/* 11111110 (0xFE) or 11111111 (0xFF) invalid */
		return WERROR;
	}
 	/* case 5: case 6: RFC 3629 restricts UTF-8 to fit into Unicode-16. */
#endif

	mb[0] = x;
	if (fread(mb+1, 1, length-1, fp) != length-1)
		return (wint_t) -4;

	/* Convert the UTF-8 sequence to a Unicode-32 value. */
	wc = mb[0] & (0x00FF >> length);
	for (x = 1; x < length; x++) {
		if ((mb[x] & 0xC0) != 0x80)
			/* Not a 10xxxxxx continuation byte. */
			return (wint_t) -5;
		wc <<= 6;
		wc |= mb[x] & 0x3F;
	}
	
	return wc;
}		

size_t
fgetws_utf8(wchar_t *ws, size_t size, FILE *fp)
{
	wchar_t wc;
	size_t length;

	if (ws == NULL || size == 0 || fp == NULL) {
		errno = EINVAL;
		return 0;
	}

	size--;
	errno = 0;

	for (length = 0; length < size && (wc = fgetwc_utf8(fp)) != WEOF; ) {
		ws[length++] = wc;
		if (wc == '\n')
			break;
	}

	ws[length] = '\0';

	return length;
}

int
fputwc_utf8(wchar_t wc, FILE *fp)
{
	int i;
	size_t length;
	unsigned char mb[6], lead;

	if (wc <= 0x7F)
		return fputc((int) wc, fp);
	
	if (wc <= 0x7FF)
		length = 2;
	else if (wc <= 0xFFFF)
		length = 3;
	else if (wc <= 0x1FFFFF)
		length = 4;

 	/* RFC 3629 restricts UTF-8 to fit into Unicode-16. */
	else if (wc <= 0x3FFFFFF)
		length = 5;
	else if (wc <= 0x7FFFFFFF)
		length = 6;
	else
		return -1;

	/* Convert UTF-32 to UTF-8. */		
	for (i = length-1; 0 < i; i--) {
		mb[i] = 0x80 | (wc & 0x3F);
		wc >>= 6;
	}
	lead = ((signed char) 0x80 >> (length-1));
	mb[i] = lead | (~lead & wc);

	return fwrite(mb, 1, length, fp) == length ? 0 : -1;
}

int
fputws_utf8(wchar_t *ws, FILE *fp)
{
	while (*ws != '\0') {
		if (fputwc_utf8(*ws++, fp) < 0)
			return -1;
	}

	return 0;
}

#ifdef TEST

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct {
	char *mb;
	wchar_t *wc;
} mb_wc_mapping;

static mb_wc_mapping eg[] = {
	/* Wikipedia examples */
	{ "$\n", L"$\n" },
	{ "\xC2\xA2\n", L"\u00A2\n" },
	{ "\xE2\x82\xAC\n", L"\u20AC\n" },
	{ "\xF0\x90\x8D\x88\n", L"\U00010348\n" },
	
	/* RFC 3629 section 7 examples. */
	{ "A\xE2\x89\xA2\xCE\x91.\n", L"A\u2262\u0391.\n" },
	{ "\xED\x95\x9C\xEA\xB5\xAD\xEC\x96\xB4\n", L"\uD55C\uAD6D\uC5B4\n" },
	{ "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E\n", L"\u65E5\u672C\u8A9E\n" },
	{ "\xEF\xBB\xBF\xF0\xA3\x8E\xB4\n", L"\uFeFF\U000233B4\n" },
	{ NULL, NULL }
};


int
main(int argc, char **argv)
{
	int i, j;
	FILE *tmp;
	char buf[128];
	wchar_t wbuf[128];
	mb_wc_mapping *m;
	size_t length;
	
	if ((tmp = tmpfile()) == NULL)
		return EXIT_FAILURE;

	(void) printf("write mb, read UTF8...\n");
	for (i = 0, m = eg; m->mb != NULL; m++, i++) {
		rewind(tmp);
		(void) fputs(m->mb, tmp);
		rewind(tmp);
		length = fgetws_utf8(wbuf, 128, tmp);
		
		for (j = 0; j < length && m->wc[j] != '\0'; j++) {
			if (wbuf[j] != m->wc[j])
				break;
		}
				
		(void) printf("%d %s\n", i, j == length ? "-OK-" : "FAIL");			
	}

	(void) printf("write wc, read mb...\n");
	for (i = 0, m = eg; m->mb != NULL; m++, i++) {
		rewind(tmp);
		(void) fputws_utf8(m->wc, tmp);
		rewind(tmp);
		(void) fgets(buf, 128, tmp);

		length = strlen(buf);
		(void) printf("%d %s\n", i, memcmp(buf, m->mb, length) == 0 ? "-OK-" : "FAIL");			
	}
				
	(void) fclose(tmp);
	
	return EXIT_SUCCESS;
}

#endif /* TEST */
