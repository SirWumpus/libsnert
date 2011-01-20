/*
 * Log.h
 *
 * Copyright 2002, 2006 by Anthony Howe.  All rights reserved.
 */

#ifndef __com_snert_lib_io_Log_h__
#define __com_snert_lib_io_Log_h__	1

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdarg.h>

extern FILE *logFile;

/*
 * Traditional syslog.h log levels
 */
#ifndef LOG_EMERG
# define LOG_EMERG      	0       /* system is unusable */
# define LOG_ALERT      	1       /* action must be taken immediately */
# define LOG_CRIT       	2       /* critical conditions */
# define LOG_ERR        	3       /* error conditions */
# define LOG_WARNING    	4       /* warning conditions */
# define LOG_NOTICE     	5       /* normal but significant condition */
# define LOG_INFO       	6       /* informational */
# define LOG_DEBUG      	7       /* debug-level messages */
#endif

/*
 * Snert's applications only use 5 levels with these names:
 *
 *	LOG_FATAL, LOG_ERROR, LOG_WARN, LOG_INFO, LOG_DEBUG
 */
#define LOG_PANIC		LOG_EMERG
#define LOG_FATAL		LOG_CRIT
#define LOG_ERROR		LOG_ERR
#define LOG_WARN		LOG_WARNING

extern int LogOpen(const char *fname);
extern void LogOpenLog(const char *ident, int option, int facility);
extern void LogClose(void);

extern /*@observer@*/ const char *LogGetProgramName(void);
extern void LogSetProgramName(const char *);
extern void LogSetLevel(int level);

/*
 * Write to log file.
 *
 * @return
 *	Zero if the message was logged, otherwise -1 the message was not
 *	logged, because the priority was not high enough, a NULL fmt string,
 *	or the log file is not opened.
 */
extern int LogDebug(const char *fmt, ...);
extern int LogInfo(const char *fmt, ...);
extern int LogWarn(const char *fmt, ...);
extern int LogError(const char *fmt, ...);
extern int LogErrorV(const char *fmt, va_list args);
extern int Log(int level, const char *fmt, ...);
extern int LogV(int level, const char *fmt, va_list args);
extern int LogPrintV(int level, const char *fmt, va_list args);

/*
 * Write to log file and standard error.
 */
extern int LogStderr(int level, const char *fmt, ...);
extern int LogStderrV(int level, const char *fmt, va_list args);

/*
 * Write to log file and standard error, then exit.
 */
extern void LogFatal(const char *fmt, ...);

#ifndef LOG_PID
/***
 *** Based on OpenBSD syslog.h and is same for Linux.
 ***/

/*
 * priorities/facilities are encoded into a single 32-bit quantity, where the
 * bottom 3 bits are the priority (0-7) and the top 28 bits are the facility
 * (0-big number).  Both the priorities and the facilities map roughly
 * one-to-one to strings in the syslogd(8) source code.  This mapping is
 * included in this file.
 *
 * priorities (these are ordered)
 */
# define LOG_PRIMASK     	0x07    /* mask to extract priority part (internal) */
# define LOG_PRI(p)     	((p) & LOG_PRIMASK)
# define LOG_MAKEPRI(fac, pri)  (((fac) << 3) | (pri))

/*
 * arguments to setlogmask.
 */
# define LOG_MASK(pri)   	(1 << (pri))            /* mask for one priority */
# define LOG_UPTO(pri)   	((1 << ((pri)+1)) - 1)  /* all priorities through pri */

/*
 * Option flags for openlog.
 *
 * LOG_ODELAY no longer does anything.
 * LOG_NDELAY is the inverse of what it used to be.
 */
# define LOG_PID         0x01    /* log the pid with each message */
# define LOG_CONS        0x02    /* log on the console if errors in sending */
# define LOG_ODELAY      0x04    /* delay open until first syslog() (default) */
# define LOG_NDELAY      0x08    /* don't delay open */
# define LOG_NOWAIT      0x10    /* don't wait for console forks: DEPRECATED */
# define LOG_PERROR      0x20    /* log to stderr as well */

/* facility codes */
# define LOG_KERN        (0<<3)  /* kernel messages */
# define LOG_USER        (1<<3)  /* random user-level messages */
# define LOG_MAIL        (2<<3)  /* mail system */
# define LOG_DAEMON      (3<<3)  /* system daemons */
# define LOG_AUTH        (4<<3)  /* authorization messages */
# define LOG_SYSLOG      (5<<3)  /* messages generated internally by syslogd */
# define LOG_LPR         (6<<3)  /* line printer subsystem */
# define LOG_NEWS        (7<<3)  /* network news subsystem */
# define LOG_UUCP        (8<<3)  /* UUCP subsystem */
# define LOG_CRON        (9<<3)  /* clock daemon */
# define LOG_AUTHPRIV    (10<<3) /* authorization messages (private) */
                                /* Facility #10 clashes in DEC UNIX, where */
                                /* it's defined as LOG_MEGASAFE for AdvFS  */
                                /* event logging.                          */
# define LOG_FTP         (11<<3) /* ftp daemon */
# define LOG_NTP         (12<<3) /* NTP subsystem */
# define LOG_SECURITY    (13<<3) /* security subsystems (firewalling, etc.) */
# define LOG_CONSOLE     (14<<3) /* /dev/console output */

        /* other codes through 15 reserved for system use */
# define LOG_LOCAL0      (16<<3) /* reserved for local use */
# define LOG_LOCAL1      (17<<3) /* reserved for local use */
# define LOG_LOCAL2      (18<<3) /* reserved for local use */
# define LOG_LOCAL3      (19<<3) /* reserved for local use */
# define LOG_LOCAL4      (20<<3) /* reserved for local use */
# define LOG_LOCAL5      (21<<3) /* reserved for local use */
# define LOG_LOCAL6      (22<<3) /* reserved for local use */
# define LOG_LOCAL7      (23<<3) /* reserved for local use */

# define LOG_NFACILITIES 24      /* current number of facilities */
# define LOG_FACMASK     0x03f8  /* mask to extract facility part */
                                /* facility of pri */
# define LOG_FAC(p)      (((p) & LOG_FACMASK) >> 3)
#endif

#if !defined(LOG_PRI)
# define LOG_PRI(p)     	((p) & LOG_PRIMASK)
#endif
#if !defined(LOG_FAC)
# define LOG_FAC(p)      	(((p) & LOG_FACMASK) >> 3)
#endif

/*
 * This is for syslog.h compatibility.
 */
#if defined(WITHOUT_SYSLOG) || defined(__WIN32__) || defined(__MINGW32__)

#ifdef __FOR_REFERENCE_ONLY__
extern void syslog(int level, const char *fmt, ...);
extern void vsyslog(int level, const char *fmt, void *args);
extern void closelog(void);
extern void openlog(const char *ident, int option, int facility);
#endif

#define syslog		Log
#define vsyslog		LogV
#define closelog	LogClose
#define openlog		LogOpenLog
#define setlogmask(x)	LogSetMask(x)

#endif

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_io_Log_h__ */
