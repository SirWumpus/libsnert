/*
 * tlds.h
 *
 * Copyright 2005, 2006 by Anthony Howe. All rights reserved.
 */

#ifndef __com_snert_lib_mail_tlds_h__
#define __com_snert_lib_mail_tlds_h__	1

#ifdef __cplusplus
extern "C" {
#endif

#include <com/snert/lib/util/option.h>

#define MAX_TLD_LEVELS	3

extern Option tldOptLevel1;
extern Option tldOptLevel2;
extern Option tldOptLevel3;

#define tldOptLevelOne	tldOptLevel1		/* deprecated */
#define tldOptLevelTwo	tldOptLevel2		/* deprecated */

/**
 * List of global top level domains.
 */
extern const char *tlds_level_1[];
#define tlds_level_one tlds_level_1		/* deprecated */

/**
 * List of 2-level domains, commonly found under some country TLDs.
 */
extern const char *tlds_level_2[];
#define tlds_level_two tlds_level_2		/* deprecated */

/**
 * List of 3-level domains, commonly found under some country TLDs.
 */
extern const char *tlds_level_3[];

/**
 * @return
 *	Zero on success, otherwise -1 on error.
 */
extern int tldInit(void);

/**
 * @param filepath
 *	The absolute file path of a text file, containing white space
 *	separated list of top level domains (without any leading dot).
 */
extern int tldLoadTable(const char *filepath, const char ***table);

/**
 * @param domain
 *	A domain name with or without the root domain dot specified,
 *	ie. example.com or example.com.
 *
 * @return
 *	True of the domain end with a valid top level domain.
 */
extern int hasValidTLD(const char *domain);

/**
 * @param domain
 *	A domain name with or without the root domain dot specified,
 *	ie. example.com or example.com.
 *
 * @param level
 *	Check Nth top level domain, ie. .co.uk or .com.au. Valid
 *	values are currently 1 and 2.
 *
 * @return
 *	True of the domain end with a valid top level domain.
 */
extern int hasValidNthTLD(const char *domain, int level);

/**
 * @param domain
 *	A domain name with or without the root domain dot specified,
 *	ie. example.com or example.com.
 *
 * @return
 *	The offset in the string of a valid two or one level TLD; otherwise
 *	-1 if not found.
 */
extern int indexValidTLD(const char *domain);

/**
 * @param domain
 *	A domain name with or without the root domain dot specified,
 *	ie. example.com or example.com.
 *
 * @param level
 *	Find the Nth level from the right end of the domain string.
 *
 * @return
 *	The offset in the string; otherwise -1 if not found.
 */
extern int indexValidNthTLD(const char *domain, int level);

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_mail_tlds_h__ */
