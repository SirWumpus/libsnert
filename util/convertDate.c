/*
 * convertDate.c
 *
 * Internet Date & Time parsing functions based on RFC 2822.
 *
 * Copyright 2003, 2006 by Anthony Howe.  All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# ifdef HAVE_SYS_TIME_H
# include <sys/time.h>
# else
# include <time.h>
# endif
#endif

static const int dayOfYear[] = {
	0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
};

static const int dayOfLeapYear[] = {
	0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335
};

static const int daysPerMonth[] = {
	31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

/* Pack the three letter month name into a 32-bit value. */
static const unsigned long months[] = {
	/* Official RFC 2822 month names. */
	('J' << 16) | ('a' << 8) | 'n', ('F' << 16) | ('e' << 8) | 'b',
	('M' << 16) | ('a' << 8) | 'r', ('A' << 16) | ('p' << 8) | 'r',
	('M' << 16) | ('a' << 8) | 'y', ('J' << 16) | ('u' << 8) | 'n',
	('J' << 16) | ('u' << 8) | 'l', ('A' << 16) | ('u' << 8) | 'g',
	('S' << 16) | ('e' << 8) | 'p', ('O' << 16) | ('c' << 8) | 't',
	('N' << 16) | ('o' << 8) | 'v', ('D' << 16) | ('e' << 8) | 'c',

	/* Lower case version for stupid qmail-scanner. */
	('j' << 16) | ('a' << 8) | 'n', ('f' << 16) | ('e' << 8) | 'b',
	('m' << 16) | ('a' << 8) | 'r', ('a' << 16) | ('p' << 8) | 'r',
	('m' << 16) | ('a' << 8) | 'y', ('j' << 16) | ('u' << 8) | 'n',
	('j' << 16) | ('u' << 8) | 'l', ('a' << 16) | ('u' << 8) | 'g',
	('s' << 16) | ('e' << 8) | 'p', ('o' << 16) | ('c' << 8) | 't',
	('n' << 16) | ('o' << 8) | 'v', ('d' << 16) | ('e' << 8) | 'c',

	/* Upper case version for stupid QuickMail Pro Server for Mac 2.0. */
	('J' << 16) | ('A' << 8) | 'N', ('F' << 16) | ('E' << 8) | 'B',
	('M' << 16) | ('A' << 8) | 'R', ('A' << 16) | ('P' << 8) | 'R',
	('M' << 16) | ('A' << 8) | 'Y', ('J' << 16) | ('U' << 8) | 'N',
	('J' << 16) | ('U' << 8) | 'L', ('A' << 16) | ('U' << 8) | 'G',
	('S' << 16) | ('E' << 8) | 'P', ('O' << 16) | ('C' << 8) | 'T',
	('N' << 16) | ('O' << 8) | 'V', ('D' << 16) | ('E' << 8) | 'C'
};

static const unsigned long days[] = {
	/* Official RFC 2822 week day names. */
	('M' << 16) | ('o' << 8) | 'n', ('T' << 16) | ('u' << 8) | 'e',
	('W' << 16) | ('e' << 8) | 'd', ('T' << 16) | ('h' << 8) | 'u',
	('F' << 16) | ('r' << 8) | 'i', ('S' << 16) | ('a' << 8) | 't',
	('S' << 16) | ('u' << 8) | 'n',

	/* Lower case version just in case some other dick can't read an RFC. */
	('m' << 16) | ('o' << 8) | 'n', ('t' << 16) | ('u' << 8) | 'e',
	('w' << 16) | ('e' << 8) | 'd', ('t' << 16) | ('h' << 8) | 'u',
	('f' << 16) | ('r' << 8) | 'i', ('s' << 16) | ('a' << 8) | 't',
	('s' << 16) | ('u' << 8) | 'n',

	/* Upper case version just in case some other prick can't read an RFC. */
	('M' << 16) | ('O' << 8) | 'N', ('T' << 16) | ('U' << 8) | 'E',
	('W' << 16) | ('E' << 8) | 'D', ('T' << 16) | ('H' << 8) | 'U',
	('F' << 16) | ('R' << 8) | 'I', ('S' << 16) | ('A' << 8) | 'T',
	('S' << 16) | ('U' << 8) | 'N'
};

typedef struct {
	const char *zone;
	size_t length;
	int offset;
} tzEntry;

static tzEntry tzList[] = {
	{ "NZDT",	sizeof ("NZDT")-1,	+1300 }, 	/* New Zealand Daylight-Saving Time */
	{ "IDLE",	sizeof ("IDLE")-1,	+1200 }, 	/* International Date Line, East */
	{ "NZST",	sizeof ("NZST")-1,	+1200 }, 	/* New Zealand Standard Time */
	{ "NZT",	sizeof ("NZT")-1,	+1200 }, 	/* New Zealand Time */
	{ "AESST",	sizeof ("AESST")-1,	+1100 }, 	/* Australia Eastern Summer Standard Time */
	{ "ACSST",	sizeof ("ACSST")-1,	+1030 }, 	/* Central Australia Summer Standard Time */
	{ "CADT",	sizeof ("CADT")-1,	+1030 }, 	/* Central Australia Daylight-Saving Time */
	{ "SADT",	sizeof ("SADT")-1,	+1030 }, 	/* South Australian Daylight-Saving Time */
	{ "AEST",	sizeof ("AEST")-1,	+1000 }, 	/* Australia Eastern Standard Time */
	{ "EAST",	sizeof ("EAST")-1,	+1000 }, 	/* East Australian Standard Time */
	{ "GST",	sizeof ("GST")-1,	+1000 }, 	/* Guam Standard Time, Russia zone 9 */
	{ "LIGT",	sizeof ("LIGT")-1,	+1000 }, 	/* Melbourne, Australia */
	{ "SAST",	sizeof ("SAST")-1,	+930 }, 	/* South Australia Standard Time */
	{ "CAST",	sizeof ("CAST")-1,	+930 }, 	/* Central Australia Standard Time */
	{ "AWSST",	sizeof ("AWSST")-1,	+900 }, 	/* Australia Western Summer Standard Time */
	{ "JST",	sizeof ("JST")-1,	+900 }, 	/* Japan Standard Time, Russia zone 8 */
	{ "KST",	sizeof ("KST")-1,	+900 }, 	/* Korea Standard Time */
	{ "MHT",	sizeof ("MHT")-1,	+900 }, 	/* Kwajalein Time */
	{ "WDT",	sizeof ("WDT")-1,	+900 }, 	/* West Australian Daylight-Saving Time */
	{ "MT",		sizeof ("MT")-1,	+830 }, 	/* Moluccas Time */
	{ "AWST",	sizeof ("AWST")-1,	+800 }, 	/* Australia Western Standard Time */
	{ "CCT",	sizeof ("CCT")-1,	+800 }, 	/* China Coastal Time */
	{ "WADT",	sizeof ("WADT")-1,	+800 }, 	/* West Australian Daylight-Saving Time */
	{ "WST",	sizeof ("WST")-1,	+800 }, 	/* West Australian Standard Time */
	{ "JT",		sizeof ("JT")-1,	+730 }, 	/* Java Time */
	{ "ALMST",	sizeof ("ALMST")-1,	+700 }, 	/* Almaty Summer Time */
	{ "WAST",	sizeof ("WAST")-1,	+700 }, 	/* West Australian Standard Time */
	{ "CXT",	sizeof ("CXT")-1,	+700 }, 	/* Christmas (Island) Time */
	{ "MMT",	sizeof ("MMT")-1,	+630 }, 	/* Myanmar Time */
	{ "ALMT",	sizeof ("ALMT")-1,	+600 }, 	/* Almaty Time */
	{ "MAWT",	sizeof ("MAWT")-1,	+600 }, 	/* Mawson (Antarctica) Time */
	{ "IOT",	sizeof ("IOT")-1,	+500 }, 	/* Indian Chagos Time */
	{ "MVT",	sizeof ("MVT")-1,	+500 }, 	/* Maldives Island Time */
	{ "TFT",	sizeof ("TFT")-1,	+500 }, 	/* Kerguelen Time */
	{ "AFT",	sizeof ("AFT")-1,	+430 }, 	/* Afghanistan Time */
	{ "EAST",	sizeof ("EAST")-1,	+400 }, 	/* Antananarivo Summer Time */
	{ "MUT",	sizeof ("MUT")-1,	+400 }, 	/* Mauritius Island Time */
	{ "RET",	sizeof ("RET")-1,	+400 }, 	/* Reunion Island Time */
	{ "SCT",	sizeof ("SCT")-1,	+400 }, 	/* Mahe Island Time */
	{ "IRT",	sizeof ("IRT")-1,	+330 }, 	/* Iran Time */
	{ "IT",		sizeof ("IT")-1,	+330 }, 	/* Iran Time */
	{ "EAT",	sizeof ("EAT")-1,	+300 }, 	/* Antananarivo, Comoro Time */
	{ "BT",		sizeof ("BT")-1,	+300 }, 	/* Baghdad Time */
	{ "EETDST",	sizeof ("EETDST")-1,	+300 }, 	/* Eastern Europe Daylight-Saving Time */
	{ "HMT",	sizeof ("HMT")-1,	+300 }, 	/* Hellas Mediterranean Time (?) */
	{ "BDST",	sizeof ("BDST")-1,	+200 }, 	/* British Double Standard Time */
	{ "CEST",	sizeof ("CEST")-1,	+200 }, 	/* Central European Summer Time */
	{ "CETDST",	sizeof ("CETDST")-1,	+200 }, 	/* Central European Daylight-Saving Time */
	{ "EET",	sizeof ("EET")-1,	+200 }, 	/* Eastern European Time, Russia zone 1 */
	{ "FWT",	sizeof ("FWT")-1,	+200 }, 	/* French Winter Time */
	{ "IST",	sizeof ("IST")-1,	+200 }, 	/* Israel Standard Time */
	{ "MEST",	sizeof ("MEST")-1,	+200 }, 	/* Middle European Summer Time */
	{ "METDST",	sizeof ("METDST")-1,	+200 }, 	/* Middle Europe Daylight-Saving Time */
	{ "SST",	sizeof ("SST")-1,	+200 }, 	/* Swedish Summer Time */
	{ "BST",	sizeof ("BST")-1,	+100 }, 	/* British Summer Time */
	{ "CET",	sizeof ("CET")-1,	+100 }, 	/* Central European Time */
	{ "DNT",	sizeof ("DNT")-1,	+100 }, 	/* Dansk Normal Tid */
	{ "FST",	sizeof ("FST")-1,	+100 }, 	/* French Summer Time */
	{ "MET",	sizeof ("MET")-1,	+100 }, 	/* Middle European Time */
	{ "MEWT",	sizeof ("MEWT")-1,	+100 }, 	/* Middle European Winter Time */
	{ "MEZ",	sizeof ("MEZ")-1,	+100 }, 	/* Mitteleuropäische Zeit */
	{ "NOR",	sizeof ("NOR")-1,	+100 }, 	/* Norway Standard Time */
	{ "SET",	sizeof ("SET")-1,	+100 }, 	/* Seychelles Time */
	{ "SWT",	sizeof ("SWT")-1,	+100 }, 	/* Swedish Winter Time */
	{ "WETDST",	sizeof ("WETDST")-1,	+100 }, 	/* Western European Daylight-Saving Time */
	{ "GMT",	sizeof ("GMT")-1,	+0000 }, 	/* Greenwich Mean Time */
	{ "UT",		sizeof ("UT")-1,	+0000 }, 	/* Universal Time */
	{ "UTC",	sizeof ("UTC")-1,	+0000 }, 	/* Universal Coordinated Time */
	{ "ZULU",	sizeof ("ZULU")-1,	+0000 }, 	/* Same as UTC */
	{ "WET",	sizeof ("WET")-1,	+0000 }, 	/* Western European Time */
	{ "WAT",	sizeof ("WAT")-1,	-100 }, 	/* West Africa Time */
	{ "FNST",	sizeof ("FNST")-1,	-100 }, 	/* Fernando de Noronha Summer Time */
	{ "FNT",	sizeof ("FNT")-1,	-200 }, 	/* Fernando de Noronha Time */
	{ "BRST",	sizeof ("BRST")-1,	-200 }, 	/* Brasilia Summer Time */
	{ "NDT",	sizeof ("NDT")-1,	-230 }, 	/* Newfoundland Daylight-Saving Time */
	{ "ADT",	sizeof ("ADT")-1,	-300 }, 	/* Atlantic Daylight-Saving Time */
	{ "AWT",	sizeof ("AWT")-1,	-300 }, 	/* (unknown) */
	{ "BRT",	sizeof ("BRT")-1,	-300 }, 	/* Brasilia Time */
	{ "NFT",	sizeof ("NFT")-1,	-330 }, 	/* Newfoundland Standard Time */
	{ "NST",	sizeof ("NST")-1,	-330 }, 	/* Newfoundland Standard Time */
	{ "AST",	sizeof ("AST")-1,	-400 }, 	/* Atlantic Standard Time (Canada) */
	{ "ACST",	sizeof ("ACST")-1,	-400 }, 	/* Atlantic/Porto Acre Summer Time */
	{ "EDT",	sizeof ("EDT")-1,	-400 }, 	/* Eastern Daylight-Saving Time */
	{ "ACT",	sizeof ("ACT")-1,	-500 }, 	/* Atlantic/Porto Acre Standard Time */
	{ "CDT",	sizeof ("CDT")-1,	-500 }, 	/* Central Daylight-Saving Time */
	{ "EST",	sizeof ("EST")-1,	-500 }, 	/* Eastern Standard Time */
	{ "CST",	sizeof ("CST")-1,	-600 }, 	/* Central Standard Time */
	{ "MDT",	sizeof ("MDT")-1,	-600 }, 	/* Mountain Daylight-Saving Time */
	{ "MST",	sizeof ("MST")-1,	-700 }, 	/* Mountain Standard Time */
	{ "PDT",	sizeof ("PDT")-1,	-700 }, 	/* Pacific Daylight-Saving Time */
	{ "AKDT",	sizeof ("AKDT")-1,	-800 }, 	/* Alaska Daylight-Saving Time */
	{ "PST",	sizeof ("PST")-1,	-800 }, 	/* Pacific Standard Time */
	{ "YDT",	sizeof ("YDT")-1,	-800 }, 	/* Yukon Daylight-Saving Time */
	{ "AKST",	sizeof ("AKST")-1,	-900 }, 	/* Alaska Standard Time */
	{ "HDT",	sizeof ("HDT")-1,	-900 }, 	/* Hawaii/Alaska Daylight-Saving Time */
	{ "YST",	sizeof ("YST")-1,	-900 }, 	/* Yukon Standard Time */
	{ "MART",	sizeof ("MART")-1,	-930 }, 	/* Marquesas Time */
	{ "AHST",	sizeof ("AHST")-1,	-1000 }, 	/* Alaska/Hawaii Standard Time */
	{ "HST",	sizeof ("HST")-1,	-1000 }, 	/* Hawaii Standard Time */
	{ "CAT",	sizeof ("CAT")-1,	-1000 }, 	/* Central Alaska Time */
	{ "NT",		sizeof ("NT")-1,	-1100 }, 	/* Nome Time */
	{ "IDLW",	sizeof ("IDLW")-1,	-1200 }, 	/* International Date Line, West */
	{ "A",		sizeof ("A")-1,		+100 },
	{ "B",		sizeof ("B")-1,		+200 },
	{ "C",		sizeof ("C")-1,		+300 },
	{ "D",		sizeof ("D")-1,		+400 },
	{ "E",		sizeof ("E")-1,		+500 },
	{ "F",		sizeof ("F")-1,		+600 },
	{ "G",		sizeof ("G")-1,		+700 },
	{ "H",		sizeof ("H")-1,		+800 },
	{ "I",		sizeof ("I")-1,		+900 },
	{ "K",		sizeof ("K")-1,		+1000 },
	{ "L",		sizeof ("L")-1,		+1100 },
	{ "M",		sizeof ("M")-1,		+1200 },
	{ "N",		sizeof ("N")-1,		-100 },
	{ "O",		sizeof ("O")-1,		-200 },
	{ "P",		sizeof ("P")-1,		-300 },
	{ "Q",		sizeof ("Q")-1,		-400 },
	{ "R",		sizeof ("R")-1,		-500 },
	{ "S",		sizeof ("S")-1,		-600 },
	{ "T",		sizeof ("T")-1,		-700 },
	{ "U",		sizeof ("U")-1,		-800 },
	{ "V",		sizeof ("V")-1,		-900 },
	{ "W",		sizeof ("W")-1,		-1000 },
	{ "X",		sizeof ("X")-1,		-1100 },
	{ "Y",		sizeof ("Y")-1,		-1200 },
	{ "Z",		sizeof ("Z")-1,		+0000 },
 	{ 0, 0, 0 }
};

int
isLeapYear(long year)
{
	if (year % 4000 == 0)
		return 0;
	if (year % 400 == 0)
		return 1;
	if (year % 100 == 0)
		return 0;
	if (year % 4 == 0)
		return 1;

	return 0;
}

static const char *
skipCommentWhitespace(const char *s)
{
	for ( ; *s != '\0'; s++) {
		if (*s == '(') {
			for (s++; *s != '\0' && *s != ')'; s++)
				;
			if (*s == '\0')
				break;
		} else if (!isspace(*s)) {
			break;
		}
	}

	return s;
}

/*
 *
 */
int
convertTimeZone(const char *zone_string, long *zone_value, const char **stop)
{
	long zone;
	char *next;
	tzEntry *tz;

	if (zone_string == NULL || zone_value == NULL)
		return -1;

	zone_string = skipCommentWhitespace(zone_string);

	zone = strtol(zone_string, &next, 10);
	if (zone_string == next) {
		zone = 0;
		for (tz = tzList; tz->zone != (const char *) 0; tz++) {
			if (strncmp(zone_string, tz->zone, tz->length) == 0) {
				if (isalpha(zone_string[tz->length]))
					return -1;

				next = (char *) &zone_string[tz->length];
				zone = tz->offset;
				break;
			}
		}
	}

	if (zone < -9959 || 9959 < zone)
		return -1;

	*zone_value = (zone / 100) * 3600 + (zone % 100) * 60;

	/* Eat any trailing comments like a zone name following the numerical zone. */
	next = (char *) skipCommentWhitespace(next);

	if (stop != NULL)
		*stop = next;

	return 0;
}

int
convertMonth(const char *month_string, long *month_value, const char **stop)
{
	long abbrev, month;

	if (month_string == NULL || month_value == NULL)
		return -1;

	month_string = skipCommentWhitespace(month_string);

	/* English abbreviated month name. */
   	abbrev = (month_string[0] << 16) | (month_string[1] << 8) | month_string[2];
	if (stop != NULL)
		*stop = month_string + 3;

   	for (month = 0; month < 36; month++) {
   		if (months[month] == abbrev) {
			/* All upper case version? */
			if (24 <= month)
				month -= 24;

			/* All lower case version? */
			if (12 <= month)
				month -= 12;

   			break;
   		}
	}

	*month_value = month;

	return -(12 <= month);
}

/*
 * Find day of the week.
 *
 * The day of the week is passed back as a value between -1 and 6,
 * where -1 indicates no day of the week, 0 to 6 correspond to
 * Mon. to Sun.
 */
int
convertWeekDay(const char *day_string, long *day_value, const char **stop)
{
	long abbrev, day;

	if (day_string == NULL || day_value == NULL)
		return -1;

	day_string = skipCommentWhitespace(day_string);

	/* English abbreviated week day name. */
   	abbrev = (day_string[0] << 16) | (day_string[1] << 8) | day_string[2];

   	for (day = 0; day < 21; day++) {
   		if (days[day] == abbrev) {
			/* All upper case version? */
			if (14 <= day)
				day -= 14;

			/* All lower case version? */
			if (7 <= day)
				day -= 7;

   			break;
   		}
	}

	if (stop != NULL && day < 7)
		*stop = day_string + 3;

	*day_value = day;

	return -(7 <= day);
}

/*
 * @param year
 *	The current year.
 *
 * @param month
 *	The current month of the year, zero-based.
 *
 * @param day
 *	The current day of the month, one-based.
 *
 * @param yday
 *	Pointer to a long value that will be passed back to the caller.
 *	Note that this value is zero-based, not one-based.
 *
 * @return
 *	Zero on success, otherwise -1 for a range error.
 */
int
convertDayOfYear(long year, long month, long day, long *yday)
{
	if (year < 0 || month < 0 || 12 <= month || day < 1 || yday == NULL)
		return -1;

	if ((month == 1 && isLeapYear(year)) + daysPerMonth[month] < day)
		return -1;

	if (isLeapYear(year))
		*yday = dayOfLeapYear[month] + day - 1;
	else
		*yday = dayOfYear[month] + day - 1;

	return 0;
}

int
convertDMY(const char *dmy_string, long *day, long *month, long *year, const char **stop)
{
	const char *next = dmy_string;

	dmy_string = skipCommentWhitespace(next);
	*day = strtol(dmy_string, (char **) &next, 10);
	if (dmy_string == next)
		return -1;

	dmy_string = skipCommentWhitespace(next);
	if (convertMonth(dmy_string, month, &next))
		return -1;

	dmy_string = skipCommentWhitespace(next);
	*year = strtol(dmy_string, (char **) &next, 10);
	if (dmy_string == next)
		return -1;

	if (stop != NULL)
		*stop = next;

	return 0;
}

int
convertHMS(const char *hms_string, long *hours, long *minutes, long *seconds, const char **stop)
{
	char *next;

	hms_string = skipCommentWhitespace(hms_string);
	*hours = strtol(hms_string, &next, 10);
	if (hms_string == next || *next != ':')
		return -1;

	hms_string = skipCommentWhitespace(next+1);
	*minutes = strtol(hms_string, &next, 10);
	if (hms_string == next)
		return -1;

	if (*next == ':') {
		hms_string = skipCommentWhitespace(next+1);
		*seconds = strtol(hms_string, &next, 10);
		if (hms_string == next)
			return -1;
	} else if (!isspace(*next)) {
		return -1;
	}

	/* Valid range 00:00:00 .. 23:59:60 (accounts for leap seconds). */
	if (*hours < 0 || 23 < *hours || *minutes < 0 || 59 < *minutes || *seconds < 0 || 60 < *seconds)
		return -1;

	if (stop != NULL)
		*stop = next;

	return 0;
}

static int
convertFromCtime(const char *dmy_string, long *day, long *month, long *year, long *hours, long *minutes, long *seconds, const char **stop)
{
	const char *next = dmy_string;

	dmy_string = skipCommentWhitespace(next);
	if (convertMonth(dmy_string, month, &next))
		return -1;

	dmy_string = skipCommentWhitespace(next);
	*day = strtol(dmy_string, (char **) &next, 10);
	if (dmy_string == next)
		return -1;

	if (convertHMS(next, hours, minutes, seconds, &next))
		return -1;

	dmy_string = skipCommentWhitespace(next);
	*year = strtol(dmy_string, (char **) &next, 10);
	if (dmy_string == next)
		return -1;

	if (stop != NULL)
		*stop = next;

	return 0;
}

/*
 * Convert an RFC 2822 Date & Time string into seconds from the epoch.
 *
 * This conforms:	Sun, 21 Sep 2003 22:04:27 +0200
 *
 * Obsolete form:	Sun, 21 Sep 03 11:30:38 GMT
 *
 * These do NOT:	Mon Sep 22 01:39:09 2003 -0000	(ctime() + zone)
 *			Mon,22 Sep 2003 20:02:33 PM	(AM/PM not zones)
 *
 * The following formats are supported:
 *
 *	[www[,]] dd mmm yyyy [HH:MM:SS [zzzzzz]]
 *	[www[,]] mmm dd HH:MM:SS yyyy [zzzzzz]
 *
 * If the time zone is missing, then GMT (+0000) is assumed, which may
 * cause undefined results if the time values are used for non-local
 * comparisions.
 *
 * @param date_string
 *	Pointer to an RFC 2822 Date & Time string.
 *
 * @param gmt_seconds_since_epoch
 *	Pointer to a time_t value that will be passed back to the caller.
 *
 * @param stop
 *	A pointer to a C string pointer. The point were the scan stops
 *	is passed back through this argument. This pointer can be NULL.
 *
 * @return
 *	Zero on success, otherwise -1 for a parse error.
 */
int
convertDate(const char *date_string, time_t *gmt_seconds_since_epoch, const char **stop)
{
	long day, month, year, hour, minute, second, zone, yday;

	if (date_string == NULL || gmt_seconds_since_epoch == NULL)
		return -1;

	date_string = skipCommentWhitespace(date_string);

	if (convertWeekDay(date_string, &day, &date_string) == 0)
		date_string += (*date_string == ',');

	hour = minute = second = zone = 0;

	/* First try to parse the old ctime() format. This is NOT conforming,
	 * but at least all the elements are there, that I can support it.
	 *
	 * date +'%a %b %d %H:%M:%S %Y %Z'
	 */
	if (convertFromCtime(date_string, &day, &month, &year, &hour, &minute, &second, &date_string) == 0) {
		if (*date_string != '\0' && convertTimeZone(date_string, &zone, &date_string))
			return -1;
	} else {
		/* RFC 2822 section 3.3. Date and Time Specification
		 *
		 * date +'%a, %d %b %Y %H:%M:%S %Z'
		 */
		if (convertDMY(date_string, &day, &month, &year, &date_string))
			return -1;

		if (*date_string != '\0') {
			if (convertHMS(date_string, &hour, &minute, &second, &date_string))
				return -1;

			if (*date_string != '\0' && convertTimeZone(date_string, &zone, &date_string))
				return -1;
		}
	}

	if (year < 0)
		return -1;
	else if (year <= 69)
		year += 2000;
	else if (70 <= year && year <= 999)
		year += 1900;

	if (convertDayOfYear(year, month, day, &yday))
		return -1;

	if (year < 1970)
		return -1;

	/* Compute GMT time. The epoch is 1 Jan 1970 00:00:00 +0000
	 *
	 * POSIX.1 B.2.2.2:
	 *
	 *	536 457 599 seconds since the epoch is
	 *	31 Dec 1986 23:59:59 +0000
	 *
	 *	                59 =          59
	 *                 59 * 60 =       3 540
	 *               23 * 3600 =      82 800
	 *	       364 * 86400 =  31 449 600
	 *	(86-70) * 31536000 = 504 576 000
	 *     ((86-69)/4) * 86400 =     345 600
	 *			    ------------
	 *			     536 457 599
	 */
	*gmt_seconds_since_epoch =
		second + minute * 60 + hour * 3600
		+ yday * 86400 + (year - 1970) * 31536000
		+ ((year - 1969) / 4) * 86400 - zone;

	if (stop != NULL)
		*stop = date_string;

	return 0;
}

#ifdef TEST

#include <com/snert/lib/io/Log.h>

int debug;
int strict;
const char usage[] = \
"usage: convertDate [-v] date-string ...\n"
"\n"
"Convert an RFC 2822 date & time specification into GMT seconds.\n"
"\n"
LIBSNERT_COPYRIGHT "\n"
"\n"
;

int
options(int argc, char **argv)
{
	int argi;

	for (argi = 1; argi < argc; argi++) {
		if (argv[argi][0] != '-' || (argv[argi][1] == '-' && argv[argi][2] == '\0'))
			break;

		switch (argv[argi][1]) {
		case 'v':
			debug = 1;
			break;
		default:
			fprintf(stderr, "invalid option -%c\n%s", argv[argi][1], usage);
			exit(2);
		}
	}

	return argi;
}

int
main(int argc, char **argv)
{
	int i;
	time_t gmt;
	const char *stop;

	LogSetProgramName("convertDate");
	i = options(argc, argv);

	if (argc <= i) {
		fprintf(stderr, "%s", usage);
		exit(2);
	}

	if (debug)
		LogSetLevel(LOG_DEBUG);

	for ( ; i < argc; i++) {
		if (convertDate(argv[i], &gmt, &stop)) {
			LogStderr(LOG_ERR, "\"%s\" does not conform to RFC 2822 section 3.3. Date and Time Specification", argv[i]);
			exit(1);
		}
		printf("%lu\t \"%s\"\t %lu\n", (unsigned long) gmt, argv[i], stop - argv[i]);
	}

	exit(0);
}

#endif
