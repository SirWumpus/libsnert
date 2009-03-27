/*
 * option.h
 *
 * Copyright 2006 by Anthony Howe.  All rights reserved.
 */

#ifndef __com_snert_lib_util_option_h__
#define __com_snert_lib_util_option_h__ 1

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>

typedef struct {
	const char *name;	/* "" or string */
	const char *initial;	/* optionOn, optionOff, or a string */
	const char *usage;	/* usage decription */
	char *string;		/* runtime setting */
	size_t length;		/* length of string */
	long value;		/* runtime setting */
} Option;

/**
 * @param table
 *	A NULL terminated table of options to be initialise.
 *
 * @param ...
 *	A NULL terminated list of Option *table[] arguments.
 *
 * @note
 *	Repeated calls to this function are safe.
 */
extern void optionInit(Option *table[], ...);

/**
 * @param o
 *	A pointer to an Option structure to initialise.
 */
extern void optionInitOption(Option *o);

/**
 * @param table
 *	A NULL terminated table of options to be initialise.
 *
 * @param ...
 *	A NULL terminated list of Option *table[] arguments.
 */
extern void optionFree(Option *table[], ...);

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
extern int optionParse(char *option, int assume_plus, char **name, char **value);

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
extern int optionSet(Option *option, char *value);

/**
 * @param option
 *	A pointer to an Option structure to set.
 *
 * @param value
 *	An option's numerical runtime value to set.
 *
 * @return
 *	True if option value was successfully set.
 */
extern int optionSetInteger(Option *opt, long value);

/**
 * @param table
 *	A table of options to be written to standard output.
 *
 * @note
 *	Equivalent to optionListAll(table, 2);
 */
extern void optionUsage(Option *table[]);

/**
 * @param table
 *	A table of options to be written to standard output.
 *
 * @param mode
 *	0	options as assignments, no comments
 *	1	options as booleans or assignments, no comments
 *	2	options as booleans or assignments with leading comments
 *	3	options as booleans or assignments with tailing comments
 */
extern void optionListAll(Option *table[], int mode);

/**
 * @param table
 *	A table of options to be written to standard output.
 *
 * @param ...
 *	A NULL terminated list of Option *table[] like arguments
 *	to be written to standard output.
 */
extern void optionUsageL(Option *table[], ...);

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
 *	The index of the first argument following the options.
 */
extern int optionArray(int argc, char *argv[], Option *table[]);

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
 *	The index of the first argument following the options.
 */
extern int optionArrayL(int argc, char *argv[], Option *table[], ...);

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
 *	The index of the first argument following the options
 */
extern int optionArrayV(int argc, char *argv[], Option *table[], va_list list);

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
extern int optionStringV(const char *string, Option *table[], va_list list);

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
extern int optionString(const char *string, Option *table[], ...);

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
extern int optionFile(const char *filename, Option *table[], ...);

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_util_option_h__ */
