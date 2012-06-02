/*
 * tlds.c
 *
 * Copyright 2005, 2006 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#ifndef __MINGW32__
# if defined(HAVE_SYSLOG_H)
#  include <syslog.h>
# endif
#endif
#include <com/snert/lib/io/Log.h>

#include <com/snert/lib/util/Text.h>
#include <com/snert/lib/mail/tlds.h>

/***********************************************************************
 *** Global Variables & Constants
 ***********************************************************************/

static const char usage_tld_level_one[] =
  "The absolute file path of a text file, containing white space\n"
"# separated list of global and country top level domains (without\n"
"# any leading dot), eg. biz com info net org eu fr uk. This list\n"
"# will override the built-in list.\n"
"#"
;
static const char usage_tld_level_two[] =
  "The absolute file path of a text file, containing white space\n"
"# separated list of two-level domains (without any leading dot),\n"
"# eg. co.uk ac.uk com.au gouv.fr tm.fr. This list will override\n"
"# the built-in list.\n"
"#"
;
static const char usage_tld_level_three[] =
  "The absolute file path of a text file, containing white space\n"
"# separated list of three-level domains (without any leading dot),\n"
"# eg. act.edu.au bo.nordland.no co.at.lv. This list will override\n"
"# the built-in list.\n"
"#"
;

Option tldOptLevel1	= { "tld-level-one-file", "", usage_tld_level_one };
Option tldOptLevel2	= { "tld-level-two-file", "", usage_tld_level_two };
Option tldOptLevel3	= { "tld-level-three-file", "", usage_tld_level_three };

static int tld_init_done;

static const char *tld_0[] = {
	NULL
};

static const char *tld_1[] = {
#include "tlds-alpha-by-domain.c"
	NULL
};

static const char *tld_2[] = {
#include "two-level-tlds.c"
	NULL
};

static const char *tld_3[] = {
#include "three-level-tlds.c"
	NULL
};

const char **tld_level_1 = tld_1;
const char **tld_level_2 = tld_2;
const char **tld_level_3 = tld_3;

static const char **tld_level_0 = tld_0;
static const char ***nth_tld[] = { &tld_level_0, &tld_level_1, &tld_level_2, &tld_level_3, NULL };

/***********************************************************************
 *** Routines
 ***********************************************************************/

/**
 * @param filepath
 *	The absolute file path of a text file, containing white space
 *	separated list of top level domains (without any leading dot).
 */
int
tldLoadTable(const char *filepath, const char ***table)
{
	FILE *fp;
	int last_ch, ch;
	char **words, *word;
	size_t word_count, byte_count, n;

	if (filepath == NULL || *filepath == '\0' || table == NULL)
		return -1;

	if (*table != tld_1 && *table != tld_2 && *table != tld_3)
		free(*table);
	*table = NULL;

	if ((fp = fopen(filepath, "r")) == NULL)
		return -1;

	/* Count bytes and white space separated words. */
	word_count = byte_count = 0;
	for (last_ch = ' '; (ch = fgetc(fp)) != EOF; last_ch = ch) {
		if (isspace(last_ch) && !isspace(ch))
			word_count++;
 		byte_count++;
	}

	rewind(fp);

	if ((words = malloc((word_count+1) * sizeof (const char *) + (byte_count+1))) == NULL) {
		fclose(fp);
		return -1;
	}

	/* Read the whole file into a buffer and null terminate it. */
	word = (char *) &words[word_count+1];
	n = fread(word, 1, byte_count, fp);
	word[byte_count] = '\0';
	fclose(fp);

	if (n != byte_count) {
		free(words);
		return -1;
	}

	/* Skip leading white space. */
	word += strspn(word, " \t\f\r\n");

	for (n = 0; n < word_count; n++) {
		/* Remember start of word. */
		words[n] = word;

		/* Find end of word and terminate it. */
		word += strcspn(word, " \t\f\r\n");
		*word++ = '\0';

		/* Find start of next word. */
		word += strspn(word, " \t\f\r\n");
	}

	words[n] = NULL;
	*table = (const char **) words;

	return 0;
}

#ifdef OLD
static int
tldCopyTable(const char **source, const char ***target)
{
	size_t length, i;
	const char **words;

	free(*target);
	*target = NULL;

	for (length = 0; source[length] != NULL; length++)
		;

	if ((words = malloc((length+1) * sizeof (const char *))) == NULL)
		return -1;

	for (i = 0; i < length; i++)
		words[i] = source[i];

	words[i] = NULL;
	*target = words;

	return 0;
}
#endif

int
tldInit(void)
{
	int rc;

	if (!tld_init_done) {
		if (tldOptLevel1.string != NULL && *tldOptLevel1.string != '\0'
		&& (rc = tldLoadTable(tldOptLevel1.string, &tld_level_1))) {
			syslog(LOG_ERR, "%s load error: %s (%d)", tldOptLevel1.string, strerror(errno), errno);
			return -1;
		}

		if (tldOptLevel2.string != NULL && *tldOptLevel2.string != '\0'
		&& (rc = tldLoadTable(tldOptLevel2.string, &tld_level_2))){
			syslog(LOG_ERR, "%s load error: %s (%d)", tldOptLevel2.string, strerror(errno), errno);
			return -1;
		}

		if (tldOptLevel3.string != NULL && *tldOptLevel3.string != '\0'
		&& (rc = tldLoadTable(tldOptLevel3.string, &tld_level_3))){
			syslog(LOG_ERR, "%s load error: %s (%d)", tldOptLevel3.string, strerror(errno), errno);
			return -1;
		}

		tld_init_done = 1;
	}

	return 0;
}

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
int
indexValidNthTLD(const char *domain, int level)
{
	size_t length;
	const char **tld;
	int i, lastdot, rootdot;

	if (domain == NULL) {
		errno = EFAULT;
		return -1;
	}

	if (*domain == '\0' || level < 1 || MAX_TLD_LEVELS < level) {
		errno = EINVAL;
		return -1;
	}

#ifdef OLD
	if (!tld_init_done && tldInit())
		return -1;
#endif

	length = strlen(domain);
	rootdot = (0 < length && domain[length-1] == '.');
	length -= rootdot;
	lastdot = length;

	for (i = 0; i < level; i++)
		lastdot = strlrcspn(domain, lastdot-1, ".");

	length -= lastdot;
	domain += lastdot;

	for (tld = *nth_tld[level]; *tld != NULL; tld++) {
		if (TextInsensitiveCompareN(domain, *tld, length) == 0
		&& ((*tld)[length] == '\0' || (*tld)[length] == '.'))
			return lastdot;
	}

	return -1;
}

/**
 * @param domain
 *	A domain name with or without the root domain dot specified,
 *	ie. example.com or example.com.
 *
 * @return
 *	The offset in the string; otherwise -1 if not found.
 */
int
indexValidTLD(const char *domain)
{
	int offset, level;

	if (domain != NULL && *domain != '\0') {
		for (level = MAX_TLD_LEVELS; 0 < level; level--) {
			if (0 <= (offset = indexValidNthTLD(domain, level)))
				return offset;
		}
	}

	return -1;
}

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
int
hasValidNthTLD(const char *domain, int level)
{
	return indexValidNthTLD(domain, level) != -1;
}

/**
 * @param domain
 *	A domain name with or without the root domain dot specified, ie
 *	example.com or example.com.
 *
 * @return
 *	True of the domain end withs a valid top level domain.
 */
int
hasValidTLD(const char *domain)
{
	return indexValidTLD(domain) != -1;
}

#ifdef TEST
# include <errno.h>
# include <stdio.h>
# include <com/snert/lib/util/getopt.h>

static char usage[] = "usage: tlds [-1 file][-2 file][-3 file][-l 1|2|3] domain ...\n";

static Option *optTable[] = {
	&tldOptLevel1,
	&tldOptLevel2,
	&tldOptLevel3,
	NULL
};

int
main(int argc, char **argv)
{
	int i, ch, level = 1;

	optionInit(optTable, NULL);

	while ((ch = getopt(argc, argv, "l:1:2:3:")) != -1) {
		switch (ch) {
		case '1':
			optionSet(&tldOptLevel1, optarg);
			break;
		case '2':
			optionSet(&tldOptLevel2, optarg);
			break;
		case '3':
			optionSet(&tldOptLevel3, optarg);
			break;
		case 'l':
			level = strtol(optarg, NULL, 10);
			break;
		default:
			(void) fprintf(stderr, usage);
			return 64;
		}
	}

	if (argc <= optind) {
		(void) fprintf(stderr, usage);
		return 64;
	}

	if (tldInit()) {
		fprintf(stderr, "tldInit error: %s (%d)\n", strerror(errno), errno);
		return 1;
	}

	for (i = optind; i < argc; i++) {
		int answer = hasValidNthTLD((char *) argv[i], level);
		int offset = indexValidNthTLD((char *) argv[i], level);
		int off = indexValidTLD((char *) argv[i]);

		printf("%s %s %d %d\n", argv[i], answer ? "yes" : "no", offset, off);
	}

	return 0;
}
#endif
