/*
 * convertDate.h
 *
 * Internet Date & Time parsing functions based on RFC 2822.
 *
 * Copyright 2003, 2012 by Anthony Howe.  All rights reserved.
 */

#ifndef __com_snert_lib_util_convertDate_h__
#define __com_snert_lib_util_convertDate_h__	1

#ifdef __cplusplus
extern "C" {
#endif

extern int isLeapYear(long year);
extern int convertTimeZone(const char *zone_string, long *zone_value, const char **stop);
extern int convertMonth(const char *month_string, long *month_value, const char **stop);
extern int convertWeekDay(const char *day_string, long *day_value, const char **stop);
extern int convertYMD(const char *date_string, long *year, long *month, long *day, const char **stop);
extern int convertHMS(const char *time_string, long *hour, long *min, long *sec, const char **stop);
extern int convertSyslog(const char *tstamp, long *month, long *day, long *hour, long *min, long *sec, const char **stop);
extern int convertCtime(const char *str, long *year, long *month, long *day, long *hour, long *min, long *sec, const char **stop);

/**
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
extern int convertDayOfYear(long year, long month, long day, long *yday);

/**
 * @param year
 *	Year greater than or equal to 1970.
 *
 * @param month
 *	Month from 0 to 30.
 *
 * @param day
 *	Day of month from 1 to 31.
 *
 * @param hour
 *	Hour from 0 to 23.
 *
 * @param min
 *	Minute from 0 to 59.
 *
 * @param sec
 *	Second from 0 to 59.
 *
 * @param zone
 *	Time zone bwteen -1200 and +1200.
 *
 * @param gmt_seconds
 *	Pointer to a time_t value that will be passed back to the caller.
 *
 * @return
 *	Zero (0) on success, otherwise -1 on error.
 */
extern int convertToGmt(long year, long month, long day, long hour, long min, long sec, long zone, time_t *gmt_seconds);

/**
 * Convert an RFC 2822 Date & Time string into seconds from the epoch.
 *
 * This conforms:	Sun, 21 Sep 2003 22:04:27 +0200
 *
 * Obsolete form:	Sun, 21 Sep 03 11:30:38 GMT
 *
 * Bad, but supported:	Mon Sep 22 01:39:09 2003 -0000	(ctime() + zone)
 *
 * Not supported:	Mon, 22 Sep 2003 20:02:33 PM	(AM/PM not zones)
 *
 * Not supported:	Mon 22 Sep 20:02:33 CDT 2003	(year & zone out of order)
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
extern int convertDate(const char *date_string, time_t *gmt_seconds_since_epoch, const char **stop);

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_util_convertDate_h__ */
