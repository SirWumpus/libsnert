/*
 * option.c
 *
 * Copyright 2006, 2013 by Anthony Howe.  All rights reserved.
 */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <com/snert/lib/version.h>
#include <com/snert/lib/io/file.h>
#include <com/snert/lib/util/Text.h>
#include <com/snert/lib/util/Token.h>
#include <com/snert/lib/util/option.h>

#ifdef DEBUG_MALLOC
# include <com/snert/lib/util/DebugMalloc.h>
#endif

/**
 * @param option
 *	A pointer to C option string to parse.
 *
 * @param assume_plus
 *	If true, then any option string without a leading plus sign
 * 	is assumed to be set true.
 *
 * @param name
 *	A pointer to a C string pointer used to return a copy of the
 *	option's name. Its the caller's responsiblity to free() this.
 *	NULL if no more options were found.
 *
 * @param value
 *	A pointer to a C string pointer used to return a copy of the
 *	option's value. Its the caller's responsiblity to free() this.
 *	NULL if no more options were found.
 *
 * 	The returned string pointer maybe the string "+" or "-" if the
 *	option was of the form "+name" or "-name" respectively. If the
 *	option form used was "name+=value", then string returned will
 *	be ";value". Otherwise the option form was "name=value" and a
 *	pointer to value is returned.
 *
 * @return
 *	True if an option was found and returned via name and value.
 *	Otherwise false on error with errno set and/or end of options.
 *
 *	Options are of three forms:
 *
 *		+name		(equivalent to name="+")
 *		-name		(equivalent to name="-")
 *		name=value
 *		name+=value	(equivalent to name=";value")
 *
 *	The first argv string that does not conform to these forms, ends
 *	the options and is the beginning of the arguments. As a special
 *	case, the option "--" will also signal an end to the list of
 *	options.
 */
int
optionParse(char *option, int assume_plus, char **name, char **value)
{
	char *eq;

	if (option == NULL || name == NULL || value == NULL) {
		errno = EFAULT;
		return 0;
	}

	errno = 0;
	*name = NULL;
	*value = NULL;

	switch (*option) {
	case '+':
		/* An option name must start with an alpha character. */
		++option;
		if (!isalpha(*option) && *option != '_')
			return 0;

		*value = strdup("+");
		*name = strdup(option);
	 	break;
	case '-':
		/* Check for explicit end to option list. */
	 	if (option[1] == '-' && option[2] == '\0')
			return 0;

		/* An option name must start with an alpha character. */
		++option;
		if (!isalpha(*option) && *option != '_')
			return 0;

		*value = strdup("-");
		*name = strdup(option);
	 	break;
	default:
		if (!isalpha(*option) && *option != '_')
			return 0;

		/* Find the equal sign in "name=value" string. */
		for (eq = option; *eq != '='; eq++) {
			if (*eq == '\0') {
				if (assume_plus) {
					*value = strdup("+");
					*name = strdup(option);
					return 1;
				}

				/* An end of string indicates that this
				 * option is actually the first argument.
				 */
				return 0;
			}
		}

		/* If += is specified, then set value to be a new list item
		 * ie. ";value" that can be appended to an existing string.
		 * See optionSet().
		 */
		if (option < eq && eq[-1] == '+')
			*eq-- = ';';

		*value = strdup(eq + 1);
		*eq = '\0';
		*name = strdup(option);
		*eq = '=';
		break;
	}

	return 1;
}

static void
optionPrintAssignment(const char *name, const char *value, int comment)
{
	int has_ws;
	Vector list;
	char *hash, **element, *quoted;

	if (name == NULL)
		return;

	if ((quoted = TokenQuote(value, "'\"")) == NULL)
		return;

 	if ((list = TextSplit(quoted, ";", 0)) == NULL)
 		goto error0;

	hash = comment ? "#" : "";
	has_ws = quoted[strcspn(quoted, " \t")] != '\0';

	element = (char **) VectorBase(list);
	printf(has_ws ? "%s%s=\"%s\"\n" : "%s%s=%s\n", hash, name, TextEmpty(*element));

	if (*element != NULL) {
		for (element++; *element != NULL; element++) {
			printf(has_ws ? "%s%s+=\"%s\"\n" : "%s%s+=%s\n", hash, name, *element);
		}
	}

	VectorDestroy(list);
error0:
	free(quoted);
}

/**
 * @param table
 *	A table of options to be written to standard output.
 *
 * @param mode
 *	0	options as assignments only, no comments
 *	1	options as booleans or assignments, no comments
 *	2	options as booleans or assignments with leading comments
 *	3	options as booleans or assignments with tailing comments
 */
void
optionListAll(Option *table[], int mode)
{
	Option **opt, *o;

	for (opt = table; *opt != NULL; opt++) {
		o = *opt;

		if (mode == 2 && o->usage != NULL)
			printf("# %s\n", o->usage);

		if (isprint(*o->name)) {
			if (o->initial == NULL) {
				/* Action like -help/+help */
				if (mode == 2)
					printf("#-%s or +%s\n", o->name, o->name);
			} else if ((*o->initial == '+' || *o->initial == '-') && o->initial[1] == '\0') {
				/* Boolean +option or -option */
				if (mode == 0) {
					printf("%s=%ld\n", o->name, o->value);
				} else {
					if (o->string != NULL && strcmp(o->initial, o->string) != 0)
						printf("#%s%s\n", o->initial, o->name);
					printf("%s%s\n", o->value ? "+" : "-", o->name);
				}
			} else {
				/* Assignment option=value or option+=value */
				if (mode == 2 && (o->string == NULL || strcmp(o->initial, o->string) != 0))
					optionPrintAssignment(o->name, o->initial, o->string != NULL);

				optionPrintAssignment(o->name, o->string, 0);
			}
		}

		if (mode == 2)
			printf("\n");
		else if (mode == 3 && o->usage != NULL)
			printf("# %s\n\n", o->usage);
	}
}

/**
 * @param table
 *	A table of options to be written to standard output.
 *
 * @note
 *	Equivalent to optionListAll(table, 2);
 */
void
optionUsage(Option *table[])
{
	optionListAll(table, 2);
}

/**
 * @param table
 *	A table of options to be written to standard output.
 *
 * @param ...
 *	A NULL terminated list of Option *table[] like arguments
 *	to be written to standard output.
 */
void
optionUsageL(Option *table[], ...)
{
	va_list list;

	va_start(list, table);

	for ( ; table != NULL; table = va_arg(list, Option **))
		optionUsage(table);

	va_end(list);
}

/**
 * @param option
 *	A pointer to an Option structure to set.
 *
 * @param value
 *	An option's runtime value to set. If the value starts with a
 *	semi-colin (;), then value is a list item to be appending to
 *	the current value. Any previous value will be free().
 *
 * @return
 *	True if option value was successfully set.
 */
int
optionSet(Option *opt, char *value)
{
	char *list;
	size_t length;

	if (opt == NULL || value == NULL)
		return 0;

	opt->value = strtol(value, NULL, 0);

	switch (*value) {
	case ';':
		if (opt->string == NULL)
			opt->string = (char *) opt->initial;

		length = strlen(opt->string) + strlen(value);
		if ((list = malloc(length + 1)) == NULL)
			return 0;

		(void) snprintf(list, length+1, "%s%s", opt->string, value + (*opt->string == '\0'));

		free(value);
		value = list;
		break;
	case '+':
		if (value[1] == '\0')
			opt->value = 1;
		break;
	}

	if (opt->initial != opt->string)
		free(opt->string);
	opt->length = strlen(value);
	opt->string = value;

	return 1;
}

int
optionSetInteger(Option *opt, long value)
{
	char buffer[20];

	if (opt->initial != opt->string)
		free(opt->string);

	if ((value == 0 && *opt->initial == '-') || (value == 1 && *opt->initial == '+')) {
		opt->string = (char *) opt->initial;
		opt->length = 1;
	} else {
		opt->length = snprintf(buffer, sizeof (buffer), "%ld", value);
		opt->string = strdup(buffer);
	}

	opt->value = value;

	return opt->string != NULL;
}

/**
 * @param table
 *	A table of options.
 *
 * @param name
 *	An option name to lookup.
 *
 * @return
 *	A pointer to an Option or NULL if option was not found.
 */
Option *
optionFind(Option *table[], const char *name)
{
	Option **opt;

	if (table != NULL && name != NULL) {
		for (opt = table; *opt != NULL; opt++) {
			if (TextInsensitiveCompare(name, (*opt)->name) == 0)
				return *opt;
		}
	}

	return NULL;
}

/*
 * @param table
 *	A table of options.
 *
 * @param name
 *	An option name to lookup.
 *
 * @param value
 *	An option's runtime value to set in the option table. If the
 *	value starts with a semi-colin (;), then value is a list item
 *	to be appending to the current value found in the table. Any
 *	previous value will be free().
 *
 * @return
 *	True if option name was found and set, otherwise false.
 */
static int
optionFindAndSet(Option *table[], const char *name, char *value)
{
	Option *option;

	if ((option = optionFind(table, name)) != NULL)
		return optionSet(option, value);

	return 0;
}

/**
 * @param argc
 *	Number of arguments in the argv array.
 *
 * @param argv
 *	An array of C string pointers, NULL terminated.
 *
 * @param table
 *	A NULL terminated table of options to be set.
 *
 * @return
 *	The index of the first argument following the options
 */
int
optionArray(int ac, char *av[], Option *table[])
{
	int argi;
	char *name, *value;

	if (av == NULL || table == NULL) {
		errno = EFAULT;
		return 0;
	}

	for (argi = 1; argi < ac && optionParse(av[argi], 0, &name, &value); argi++) {
		if (!optionFindAndSet(table, name, value))
			free(value);
		free(name);
	}

	return argi;
}

/**
 * @param argc
 *	Number of arguments in the argv array.
 *
 * @param argv
 *	An array of C string pointers, NULL terminated.
 *
 * @param table
 *	A NULL terminated table of options to be set.
 *
 * @param list
 *	A NULL terminated list of Option *table[] arguments.
 *
 * @return
 *	The index of the first argument following the options.
 */
int
optionArrayV(int ac, char *av[], Option *table[], va_list list)
{
	Option **opt;
	int argi, max_argi;

	for (max_argi = 0, opt = table; opt != NULL; opt = va_arg(list, Option **)) {
		argi = optionArray(ac, av, opt);
		if (max_argi < argi)
			max_argi = argi;
	}

	return max_argi;
}

/**
 * @param argc
 *	Number of arguments in the argv array.
 *
 * @param argv
 *	An array of C string pointers, NULL terminated.
 *
 * @param table
 *	A NULL terminated table of options to be set.
 *
 * @param ...
 *	A NULL terminated list of Option *table[] arguments.
 *
 * @return
 *	The index of the first argument following the options;
 *	or zero (0) if an unknown option was found.
 */
int
optionArrayL(int argc, char *argv[], Option *table[], ...)
{
	int argi;
	va_list list;

	va_start(list, table);
	argi = optionArrayV(argc, argv, table, list);
	va_end(list);

	return argi;
}

/**
 * @param string
 *	A C string of one or more options.
 *
 * @param table
 *	A NULL terminated table of options to be set.
 *
 * @param list
 *	A NULL terminated list of Option *table[] arguments.
 *
 * @return
 *	The index of the first argument following the options;
 *	or zero (0) if an unknown option was found.
 */
int
optionStringV(const char *string, Option *table[], va_list list)
{
	char **av;
	int ac, argi = 0;

	if (!TokenSplit(string, NULL, &av, &ac, 1)) {
		argi = optionArrayV(ac, av, table, list);
		free(av);
	}

	return argi;
}

/**
 * @param string
 *	A C string of one or more options.
 *
 * @param table
 *	A NULL terminated table of options to be set.
 *
 * @param ...
 *	A NULL terminated list of Option *table[] arguments.
 *
 * @return
 *	The index of the first argument following the options;
 *	or zero (0) if an unknown option was found.
 */
int
optionString(const char *string, Option *table[], ...)
{
	int argi;
	va_list list;

	va_start(list, table);
	argi = optionStringV(string, table, list);
	va_end(list);

	return argi;
}

/**
 * @param filename
 *	The file path of a text file containing comments and/or
 *	options to be read.
 *
 * @param table
 *	A NULL terminated table of options to be set.
 *
 * @param ...
 *	A NULL terminated list of Option *table[] arguments.
 *
 * @return
 *	Zero (0) on success, otherwise -1 on error and errno set.
 */
int
optionFile(const char *filename, Option *table[], ...)
{
	int rc;
	FILE *fp;
	char *buf;
	va_list list;

	rc = -1;

	if ((buf = malloc(BUFSIZ)) == NULL)
		goto error0;

	if ((fp = fopen(filename, "r")) == NULL)
		goto error1;
#ifdef __unix__
	(void) fileSetCloseOnExec(fileno(fp), 1);
#endif
	while (fgets(buf, BUFSIZ, fp) != NULL) {
		if (*buf == '#')
			continue;

		va_start(list, table);
		optionStringV(buf, table, list);
		va_end(list);
	}

	rc = fclose(fp);
error1:
	free(buf);
error0:
	return rc;
}

/**
 * @param o
 *	A pointer to an Option structure to initialise.
 */
void
optionResetOption(Option *o)
{
	if (o->initial != o->string)
		free(o->string);

	o->value = 0;
	o->string = (char *) o->initial;

	if (o->string != NULL) {
		o->value = strtol(o->string, NULL, 0);
		if (o->string[0] == '+' && o->string[1] == '\0')
			o->value = 1;
	}
}

/**
 * @param table
 *	A NULL terminated table of options to be initialise.
 *
 * @param ...
 *	A NULL terminated list of Option *table[] arguments.
 *
 * @note
 *	Repeated calls to this function discard the previous
 *	settings.
 */
void
optionInit(Option *table[], ...)
{
	va_list list;
	Option **opt;

	va_start(list, table);

	for (opt = table; opt != NULL; opt = va_arg(list, Option **)) {
		for ( ; *opt != NULL; opt++) {
			optionResetOption(*opt);
		}
	}

	va_end(list);
}

void
optionFreeV(Option *table[], va_list list)
{
	Option **opt, *o;

	for (opt = table; opt != NULL; opt = va_arg(list, Option **)) {
		for ( ; *opt != NULL; opt++) {
			o = *opt;
			if (o->initial != o->string)
				free(o->string);
			o->string = NULL;
		}
	}
}

void
optionFreeL(Option *table[], ...)
{
	va_list list;
	va_start(list, table);
	optionFreeV(table, list);
	va_end(list);
}

size_t
optionCountV(Option *table[], va_list list)
{
	Option **opt;
	size_t count = 0;

	for (opt = table; opt != NULL; opt = va_arg(list, Option **)) {
		for ( ; *opt != NULL; opt++)
			count++;
	}

	return count;
}

size_t
optionCountL(Option *table[], ...)
{
	size_t count;
	va_list list;

	va_start(list, table);
	count = optionCountV(table, list);
	va_end(list);

	return count;
}

void
optionDupFree(Option *table[])
{
	optionFreeL(table, NULL);
	free(table);
}

Option **
optionDupV(Option *table[], va_list list)
{
	size_t count;
	va_list list2;
	Option **opt, **copy, **cpy, *c;

	va_copy(list2, list);
	count = optionCountV(table, list2);

	if ((copy = malloc((count+1) * sizeof (*copy) + count * sizeof (**copy))) == NULL)
		return NULL;

	cpy = copy;
	c = (Option *) &copy[count+1];

	for (opt = table; opt != NULL; opt = va_arg(list, Option **)) {
		for ( ; *opt != NULL; opt++, cpy++, c++) {
			*cpy = c;
			*c = **opt;
			if (c->initial != c->string)
				c->string = strdup(c->string);
		}
	}

	*cpy = NULL;

	return copy;
}

Option **
optionDupL(Option *table[], ...)
{
	va_list list;
	Option **copy;

	va_start(list, table);
	copy = optionDupV(table, list);
	va_end(list);

	return copy;
}

#ifdef TEST
#include <stdio.h>

static Option optIntro  = { "", 	NULL,	"Here we have some test options.\n" };
static Option optDaemon	= { "daemon",	"+",	"If true, then use daemon mode." };
static Option optDebug	= { "debug", 	"-",	"Display more debugging output." };
static Option optFile	= { "file", 	"",	"Read option file." };
static Option optHelp	= { "help", 	NULL,	"Show the option summary and exit." };
static Option optNumber	= { "number",	"123",	"Specify a number." };
static Option optNum2	= { "num2",	"1",	"Specify a number." };
static Option optString	= { "string",	"boo!",	"Specify a string." };
static Option optString2= { "string2",	"space and\ttab",	"Specify a quoted string." };
static Option optString3= { "_name",	"whatever",	"Specify whatever." };
static Option optList1  = { "list",	"element1; element2",	"A list" };
static Option optCopyright = { "", NULL, LIBSNERT_COPYRIGHT };

static Option *table0[] = {
	&optIntro,
	&optDaemon,
	&optDebug,
	&optFile,
	&optHelp,
	NULL
};

static Option *table1[] = {
	&optNumber,
	&optNum2,
	&optString,
	&optString2,
	&optString3,
	&optList1,
	&optCopyright,
	NULL
};

int
main(int argc, char **argv)
{
	int argi;

	optionInit(table0, table1, NULL);
	argi = optionArrayL(argc, argv, table0, table1, NULL);

	if (*optFile.string != '\0') {
		/* Do NOT reset this option. */
		optFile.initial = optFile.string;
		optFile.string = NULL;

		optionInit(table0, table1, NULL);
		(void) optionFile(optFile.string, table0, table1, NULL);
		(void) optionArrayL(argc, argv, table0, table1, NULL);
	}

	for ( ; argi < argc; argi++)
		printf("argv[%d]=%s\n", argi, argv[argi]);

	if (optHelp.string != NULL) {
		optionUsageL(table0, table1, NULL);
		exit(2);
	}

	if (optNumber.value == 999)
		printf("bingo!\n");

	return 0;
}

#endif
