/*
 * Error.h
 *
 * Program error message & exit routines.
 *
 * Copyright 1994, 2004 by Anthony Howe.  All rights reserved.
 */

#ifndef __com_snert_lib_io_Error_h__
#define __com_snert_lib_io_Error_h__	1

#ifdef __cplusplus
extern "C" {
#endif

#ifndef BUFSIZ
#include <stdio.h>
#endif

#include <stdarg.h>

/*
 * Get/Set the program name to be used for error messages.
 */
extern const char *ErrorGetProgramName(void);
extern void ErrorSetProgramName(const char *);

extern void ErrorPrint(const char *file, unsigned long lineno, const char *fmt, ...);
extern void ErrorPrintLine(const char *, unsigned long, const char *, ...);
extern void ErrorPrintV(const char *, unsigned long, const char *, va_list);
extern void ErrorPrintLineV(const char *, unsigned long, const char *,va_list);

extern void FatalPrint(const char *file, unsigned long lineno, const char *fmt, ...);
extern void FatalPrintLine(const char *file, unsigned long lineno, const char * fmt, ...);
extern void FatalPrintV(const char *, unsigned long, const char *, va_list);
extern void FatalPrintLineV(const char *, unsigned long, const char *, va_list);

extern void UsagePrintLine(const char *);

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_io_Error_h__ */
