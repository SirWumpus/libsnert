/*
 * printVar.c
 *
 * Copyright 2013, 2014 by Anthony Howe. All rights reserved.
 */

#include <com/snert/lib/version.h>
#include <stdio.h>
#include <com/snert/lib/util/Text.h>

void
printVar(int columns, const char *name, const char *value)
{
	int length;
	Vector list;
	const char **args;

	if (columns <= 0)
		printf("%s=\"%s\"\n",  name, value);
	else if ((list = TextSplit(value, " \t", 0)) != NULL && 0 < VectorLength(list)) {
		args = (const char **) VectorBase(list);

		length = printf("%s=\"'%s'", name, *args);
		for (args++; *args != NULL; args++) {
			/* Line wrap. */
			if (columns <= length + strlen(*args) + 4) {
				(void) printf("\n\t");
				length = 8;
			}
			length += printf(" '%s'", *args);
		}
		if (columns <= length + 1) {
			(void) printf("\n");
		}
		(void) printf("\"\n");

		VectorDestroy(list);
	}
}

void
snertPrintInfo(void)
{
#ifdef LIBSNERT_VERSION
	printVar(0, "LIBSNERT_VERSION", LIBSNERT_VERSION);
#endif
#ifdef LIBSNERT_CONFIGURE
	printVar(LINE_WRAP, "LIBSNERT_CONFIGURE", LIBSNERT_CONFIGURE);
#endif
#ifdef LIBSNERT_BUILT
	printVar(LINE_WRAP, "LIBSNERT_BUILT", LIBSNERT_BUILT);
#endif
#ifdef LIBSNERT_CFLAGS
	printVar(LINE_WRAP, "CFLAGS", CFLAGS_PTHREAD " " LIBSNERT_CFLAGS);
#endif
#ifdef LIBSNERT_CPPFLAGS
	printVar(LINE_WRAP, "CPPFLAGS", CPPFLAGS_PTHREAD " " LIBSNERT_CPPFLAGS);
#endif
#ifdef LIBSNERT_LDFLAGS
	printVar(LINE_WRAP, "LDFLAGS", LDFLAGS_PTHREAD " " LIBSNERT_LDFLAGS);
#endif
#ifdef LIBSNERT_LIBS
	printVar(LINE_WRAP, "LIBS", LIBSNERT_LIBS " " LIBS_PTHREAD);
#endif
}

void
snertPrintVersion(void)
{
	printf(LIBSNERT_STRING " " LIBSNERT_COPYRIGHT "\n");
#ifdef LIBSNERT_BUILT
	printf("LibSnert built on " LIBSNERT_BUILT "\n");
#endif
}
