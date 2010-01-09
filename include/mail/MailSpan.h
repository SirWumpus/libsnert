/*
 * MailSpan.h
 *
 * Assorted span functions for validation.
 *
 * Copyright 2002, 2006 by Anthony Howe.  All rights reserved.
 */

#ifndef __com_snert_lib_mail_MailSpan_h__
#define __com_snert_lib_mail_MailSpan_h__	1

#ifdef __cplusplus
extern "C" {
#endif

/*@-exportlocal@*/

extern long MailSpanIPv4(const char *ip);
extern long MailSpanIPv6(const char *ip);
extern long MailSpanAddressLiteral(const char *s);
extern long MailSpanDomainName(const char *s, int minimumDots);
extern long MailSpanLocalPart(const char *s);
extern long MailSpanMailbox(const char *s);
extern long MailSpanAtDomainList(const char *s);
extern long MailSpanPath(const char *s);


/*@=exportlocal@*/

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_mail_MailSpan_h__ */

