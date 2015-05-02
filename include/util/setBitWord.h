/*
 * setBitWord.h
 *
 * Copyright 2004 by Anthony Howe.  All rights reserved.
 */

#ifndef __com_snert_lib_util_setBitWord_h__
#define __com_snert_lib_util_setBitWord_h__	1

#ifdef __cplusplus
extern "C" {
#endif

struct bitword {
	unsigned long bit;
	const char *name;
};

/**
 * @param table
 *	An array of struct bitword, terminated by a null entry { 0, NULL }.
 *
 * @param word_list
 *	Can either be numeric string representing a decimal, octal (leading
 *	zero), or hexadecimal (leading 0x) number defining a collection of
 *	bits; otherwise a comma seperated list of words representing bit
 *	names.
 *
 * @return
 *	A unsigned 32-bit value representing a collection of bits set.
 */
extern unsigned long setBitWord(struct bitword *table, const char *word_list);

/**
 * @param table
 *	An array of struct bitword, terminated by a null entry { 0, NULL }.
 *
 * @param word_list
 *	Can either be numeric string representing a decimal, octal (leading
 *	zero), or hexadecimal (leading 0x) number defining a collection of
 *	bits; otherwise a delimiter seperated list of words representing bit
 *	names.
 *
 * @param delims
 *	A C string of delimiter characters separating words. If NULL, then
 *	default to comma ",".
 *
 * @param flags
 *	The current set of flags to alter.
 *
 * @return
 *	A unsigned 32-bit value representing a collection of bits set.
 */
extern unsigned long setBitWord2(struct bitword *table, const char *word_list, const char *delims, unsigned long flags);

extern long selectWord(const char **table, const char *word);

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_util_setBitWord_h__ */
