/*
 * ProcTitle.c
 *
 * Copyright 2005, 2008 by Anthony Howe. All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <com/snert/lib/version.h>
#include <com/snert/lib/util/ProcTitle.h>

#if !defined(HAVE_SETPROCTITLE)

extern char **environ;

static int arg_c;
static char **arg_v;
static char *arg_v_0;

#if defined(__linux__)
static size_t arg_size;
static char **env_array;
#elif defined(__unix__)
static char *arg_v_1;
#endif

#if defined(__linux__)
static char **
copy_array(char **array, int *plength)
{
	size_t size;
	int i, length;
	char **copy, *dst;

	size = 0;
	length = 0;

	/* Count number of strings and space used by strings. */
	for (i = 0; array[i] != NULL; i++) {
		size += strlen(array[i]) + 1;
		length++;
	}

	/* Allocate array and space for copied strings. */
	if ((copy = malloc((length+1) * sizeof (*copy) + size)) != NULL) {
		/* Strings come immediately after the array of pointers. */
		dst = (char *) &copy[length+1];

		/* Copy the array. */
		for (i = 0; i < length; i++) {
			copy[i] = strcpy(dst, array[i]);
			dst += strlen(dst) + 1;
		}

		/* Make sure its always NULL terminated. */
		copy[i] = NULL;

		if (plength != NULL)
			*plength = length;
	}

	return copy;
}
#endif

void
ProcTitleInit(int argc, char **argv)
{
#if defined(__linux__)
	int envc;

	if ((arg_v_0 = strdup(argv[0])) == NULL)
		return;

	/* The process title under linux is changed by writting into
	 * string pointed to by argv[0] which will destroy the contents
	 * of the argv strings and the environ strings that immediately
	 * follow after in memory. So we make a copy of the environ
	 * array and record how much space we have to play with.
	 */
	if ((env_array = copy_array(environ, &envc)) == NULL)
		return;

	if (0 < envc)
		arg_size = environ[envc-1] - argv[0];
	else
		arg_size = argv[argc-1] - argv[0];

# if defined(FREE_ENIVRON)
	free(environ);
# endif
	environ = env_array;
#elif defined(__unix__)
	/* For OpenBSD, we can just replace values of argv[0] and argv[1].
	 */
	arg_v_0 = argv[0];
	arg_v_1 = argv[1];
#endif
	arg_c = argc;
	arg_v = argv;
}

void
ProcTitleFini(void)
{
#if defined(__linux__)
	free(arg_v_0);
# if !defined(FREE_ENIVRON)
	free(env_array);
# endif
#endif
}

void
ProcTitleSet(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
# if defined(__linux__)
{
	int n;

	if (fmt == NULL)
		n = snprintf(arg_v[0], arg_size, "%s", arg_v_0);
	else
		n = vsnprintf(arg_v[0], arg_size, fmt, args);
	if (n < arg_size)
		memset(arg_v[0] + n, 0, arg_size - n);
	arg_v[1] = NULL;
}
# elif defined(__unix__)
	if (fmt == NULL) {
		arg_v[0] = arg_v_0;
		arg_v[1] = arg_v_1;
	} else {
		static char title[512];
		vsnprintf(title, sizeof (title), fmt, args);
		arg_v[0] = title;
		arg_v[1] = NULL;
	}
# endif
	va_end(args);
}

#endif /* !defined(HAVE_SETPROCTITLE) */

#if defined(TEST) && defined(__unix__)

int
main(int argc, char **argv)
{
        ProcTitleInit(argc, argv);

	ProcTitleSet("ProcTitle Test: Once more unto the breach, dear friends, once more, Or close the wall up with our English dead!");
	printf("sleeping for 30 seconds, please check process title\n");
	sleep(30);

	ProcTitleFini();

	return 0;
}

#endif
