/*
 * spf.h
 *
 * Copyright 2004, 2006 by Anthony Howe.  All rights reserved.
 */

#ifndef __com_snert_lib_mail_spf_h__
#define __com_snert_lib_mail_spf_h__	1

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include <com/snert/lib/mail/spf.h>
#include <com/snert/lib/util/option.h>

extern Option spfTempErrorDns;

extern const char spfErrorOk[];
extern const char spfErrorIpLiteral[];
extern const char spfErrorSyntax[];
extern const char spfErrorInternal[];
extern const char spfErrorNullArgument[];
extern const char spfErrorIpParse[];
extern const char spfErrorMemory[];
extern const char spfErrorCircular[];
extern const char spfErrorDnsLimit[];

#define SPF_PASS		0
#define SPF_FAIL		1
#define SPF_NONE		2
#define SPF_NEUTRAL		3
#define SPF_SOFTFAIL		4
#define SPF_TEMP_ERROR		5
#define SPF_PERM_ERROR		6

extern const char *spfResultString[];

extern void spfSetDebug(int);
extern const char *spfCheckMail(const char *client_addr, const char *mail, int *result);
extern const char *spfCheckDomain(const char *client_addr, const char *domain, int *result);
extern const char *spfCheckHeloMail(const char *client_addr, const char *helo, const char *mail, int *result);
extern const char *spfCheckHeloMailTxt(const char *client_addr, const char *helo, const char *mail, const char *txt, int *result);

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_mail_spf_h__ */
