/*
 * mf.h
 *
 * Copyright 2002, 2006 by Anthony Howe.  All rights reserved.
 */

#ifndef __com_snert_lib_mail_mf_h__
#define __com_snert_lib_mail_mf_h__	1

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include <sys/types.h>

#ifdef SET_SMFI_VERSION
/* Have to define this before loading libmilter
 * headers to enable the extra handlers.
 */
# define SMFI_VERSION		SET_SMFI_VERSION
#endif

#include <libmilter/mfapi.h>

/**
 * @param ctx
 *	A milter context.
 *
 * @param rcode
 *	A pointer to a C string used for the SMTP return code. If NULL,
 *	then the return code is taken from the formatted reply; otherwise
 *	450 is assumed.
 *
 * @param xcode
 *	A pointer to a C string used for the SMTP extended return code.
 *	If NULL, then the extended return code is taken from the formatted
 *	reply; otherwise 4.7.1 or 5.7.1 is assumed depending on rcode.
 *
 * @param fmt
 *	A pointer to a C printf format string.
 *
 * @param args
 *	A va_list of printf arguments.
 *
 * @return
 *	If the SMTP return code is 5xy, return SMFIS_REJECT; else assume
 *	the SMTP return code is 4xy and return SMFIS_TEMPFAIL. If malloc()
 *	fails then the formatted SMTP text reply will not be set.
 */
extern sfsistat mfReplyV(SMFICTX *ctx, const char *rcode, const char *xcode, const char *fmt, va_list args);

/**
 * @param ctx
 *	A milter context.
 *
 * @param rcode
 *	A pointer to a C string used for the SMTP return code. If NULL,
 *	then the return code is taken from the formatted reply; otherwise
 *	450 is assumed.
 *
 * @param xcode
 *	A pointer to a C string used for the SMTP extended return code.
 *	If NULL, then the extended return code is taken from the formatted
 *	reply; otherwise 4.7.1 or 5.7.1 is assumed depending on rcode.
 *
 * @param fmt
 *	A pointer to a C printf format string.
 *
 * @param ...
 *	A list of printf arguments.
 *
 * @return
 *	If the SMTP return code is 5xy, return SMFIS_REJECT; else assume
 *	the SMTP return code is 4xy and return SMFIS_TEMPFAIL. If malloc()
 *	fails then the formatted SMTP text reply will not be set.
 */
extern sfsistat mfReply(SMFICTX *ctx, const char *rcode, const char *xcode, const char *fmt, ...);

#ifdef HAVE_SMFI_SETMLREPLY

/**
 * @param ctx
 *	A milter context.
 *
 * @param rcode
 *	A pointer to a C string used for the SMTP return code. If NULL,
 *	then the return code is taken from the formatted reply; otherwise
 *	450 is assumed.
 *
 * @param xcode
 *	A pointer to a C string used for the SMTP extended return code.
 *	If NULL, then the extended return code is taken from the formatted
 *	reply; otherwise 4.7.1 or 5.7.1 is assumed depending on rcode.
 *
 * @param lines
 *	A NULL terminated array of pointers to a C strings. At most the
 *	first 32 strings are used.
 *
 * @return
 *	If the SMTP return code is 5xy, return SMFIS_REJECT; else assume
 *	the SMTP return code is 4xy and return SMFIS_TEMPFAIL.
 */
extern sfsistat mfMultiLineReplyA(SMFICTX *ctx, const char *rcode, const char *xcode, char **lines);

/**
 * @param ctx
 *	A milter context.
 *
 * @param rcode
 *	A pointer to a C string used for the SMTP return code. If NULL,
 *	then the return code is taken from the formatted reply; otherwise
 *	450 is assumed.
 *
 * @param xcode
 *	A pointer to a C string used for the SMTP extended return code.
 *	If NULL, then the extended return code is taken from the formatted
 *	reply; otherwise 4.7.1 or 5.7.1 is assumed depending on rcode.
 *
 * @param args
 *	A NULL terminated va_list of pointers to a C strings. At most the
 *	first 32 strings are used.
 *
 * @return
 *	If the SMTP return code is 5xy, return SMFIS_REJECT; else assume
 *	the SMTP return code is 4xy and return SMFIS_TEMPFAIL.
 */
extern sfsistat mfMultiLineReplyV(SMFICTX *ctx, const char *rcode, const char *xcode, va_list args);

/**
 * @param ctx
 *	A milter context.
 *
 * @param rcode
 *	A pointer to a C string used for the SMTP return code. If NULL,
 *	then the return code is taken from the formatted reply; otherwise
 *	450 is assumed.
 *
 * @param xcode
 *	A pointer to a C string used for the SMTP extended return code.
 *	If NULL, then the extended return code is taken from the formatted
 *	reply; otherwise 4.7.1 or 5.7.1 is assumed depending on rcode.
 *
 * @param ...
 *	A NULL terminated variable argument list of pointers to a C strings.
 *	Each string may contain multiple lines delimited by CRLF. At most
 *	the first 32 lines are used.
 *
 * @return
 *	If the SMTP return code is 5xy, return SMFIS_REJECT; else assume
 *	the SMTP return code is 4xy and return SMFIS_TEMPFAIL.
 */
extern sfsistat mfMultiLineReply(SMFICTX *ctx, const char *rcode, const char *xcode, ...);

#endif /* HAVE_SMFI_SETMLREPLY */

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_mail_mf_h__ */
