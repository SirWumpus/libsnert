/*
 * convertDate.h
 *
 * Internet Date & Time parsing functions based on RFC 2822.
 *
 * Copyright 2003, 2006 by Anthony Howe.  All rights reserved.
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
extern int convertDMY(const char *dmy_string, long *day, long *month, long *year, const char **stop);
extern int convertHMS(const char *hms_string, long *hours, long *minutes, long *seconds, const char **stop);

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
extern int convertDayOfYear(long year, long month, long day, long *yday);

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
extern int convertDate(const char *date_string, time_t *gmt_seconds_since_epoch, const char **stop);

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_util_convertDate_h__ */
