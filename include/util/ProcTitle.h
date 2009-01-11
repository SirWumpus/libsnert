/*
 * ProcTitle.h
 *
 * Copyright 2005 by Anthony Howe. All rights reserved.
 */


#ifndef __com_snert_lib_util_ProcTitle_h__
#define __com_snert_lib_util_ProcTitle_h__	1

#ifdef __cplusplus
extern "C" {
#endif

#if defined(HAVE_SETPROCTITLE)
# if defined(__OpenBSD__)
#  include <stdlib.h>
# elif defined(__FreeBSD__)
#  ifdef HAVE_SYS_TYPES_H
#   include <sys/types.h>
#  endif
#  ifdef HAVE_UNISTD_H
#   include <unistd.h>
#  endif
# endif

#define ProcTitleInit(ac, av)
#define ProcTitleSet 		setproctitle
#define ProcTitleFini()

#else /* !defined(HAVE_SETPROCTITLE) */

extern void ProcTitleInit(int, char **);
extern void ProcTitleSet(const char *, ...);
extern void ProcTitleFini(void);

#endif /* defined(HAVE_SETPROCTITLE) */

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_util_ProcTitle_h__ */
