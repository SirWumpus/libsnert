/*
 * process.h
 *
 * Copyright 2004, 2005 by Anthony Howe.  All rights reserved.
 */

#ifndef __com_snert_lib_sys_process_h__
#define __com_snert_lib_sys_process_h__	1

#  ifdef __cplusplus
extern "C" {
#  endif

/***********************************************************************
 ***
 ***********************************************************************/

#include <errno.h>
#include <string.h>

#ifndef __MINGW32__
# if defined(HAVE_GRP_H)
#  include <grp.h>
# endif
# if defined(HAVE_PWD_H)
#  include <pwd.h>
# endif
# if defined(HAVE_NETDB_H)
#  include <netdb.h>
# endif
# if defined(HAVE_SYSLOG_H)
#  include <syslog.h>
# endif
# if defined(HAVE_SYS_WAIT_H)
#  include <sys/wait.h>
# endif
#endif

#ifdef HAVE_UNISTD_H
# ifdef __linux__
#  /* See Linux man setresgid */
#  define _GNU_SOURCE
# endif
# include <unistd.h>
#endif

/***********************************************************************
 ***
 ***********************************************************************/

extern uid_t process_ruid;		/* Originally user at startup. */
extern uid_t process_euid;		/* Desired user-ID. */
extern gid_t process_gid;		/* Desired group-ID. */

/***********************************************************************
 *** Process Support Routines
 ***********************************************************************/

/**
 * @param run_user
 *	Specify the run time user name.
 *
 * @param run_group
 *	Specify the run time group name.
 *
 * @param run_dir
 *	Specify an absolute path for the initial run time working
 *	directory.
 *
 * @param run_jailed
 *	Specify true or false if the process should chroot to the
 *	working directory.
 *
 * @return
 *	Zero on successful. Otherwise -1 on failure and an error
 *	message written to syslog.
 */
extern int processDropPrivilages(const char *run_user, const char *run_group, const char *run_dir, int run_jailed);

/*
 * @param flag
 *	Set to one (1) or zero (0) to enable or disable core dumps.
 *	Otherwise specify any other value, such as -1, to query.
 *	Must be called after all setuid/setgid manipulation, else
 *	the value might be reset.
 *
 * FreeBSD
 *	  0	disable dump core (default)
 *	  1	enable dump core
 *
 * OpenBSD
 *	  0	dump core,
 *	  1	disable dump core (default)
 *	  2	dump core to /var/crash.
 *
 * Linux
 *	  0	disable dump core
 *	  1	enable dump core
 *	  2	enable dump core readable by root only
 *
 *
 * @return
 *	Previous value of the flag.
 */
extern int processDumpCore(int flag);

/***********************************************************************
 *** System Information Routines
 ***********************************************************************/

#if defined(__OpenBSD__) && defined(HAVE_SYS_SYSCTL_H)
extern int getSysCtlInt(int mib0, int mib1);
extern char *getSysCtlString(int mib0, int mib1);
#endif

extern long sysGetCpuOnline(void);
extern long sysGetCpuCount(void);

/***********************************************************************
 ***
 ***********************************************************************/

#  ifdef  __cplusplus
}
#  endif

#endif /* __com_snert_lib_sys_process_h__ */

