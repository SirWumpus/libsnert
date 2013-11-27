/*
 * file.h
 *
 * Copyright 2007 by Anthony Howe.  All rights reserved.
 */

#ifndef __com_snert_lib_io_file_h__
#define __com_snert_lib_io_file_h__	1

# ifdef __cplusplus
extern "C" {
# endif

/***********************************************************************
 ***
 ***********************************************************************/

#include <stdio.h>

#ifdef HAVE_SYS_FILE_H
# include <sys/file.h>
#endif
#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif

#ifndef __MINGW32__
# if defined(HAVE_GRP_H)
#  include <grp.h>
# endif
# if defined(HAVE_PWD_H)
#  include <pwd.h>
# endif
# if defined(HAVE_SYSLOG_H)
#  include <syslog.h>
# endif
#endif

#ifdef HAVE_UNISTD_H
# undef _GNU_SOURCE
# define _GNU_SOURCE
# include <unistd.h>
#else
# if defined(__BORLANDC__) && defined(HAVE_IO_H)
#  include <io.h>
#  include <sys/locking.h>
# endif
#endif

/***********************************************************************
 *** Constants
 ***********************************************************************/

/*
 * Define file descriptors for standard input, standard output, and
 * standard error.  Poor implementations of <unistd.h> don't always
 * define STDIN_FILENO, STDOUT_FILENO, and STDERR_FILENO, nor can we
 * rely on the constants 0, 1, and 2 on some non-unix systems.
 */
#ifdef STDIN_FILENO
# define FILENO_STDIN		STDIN_FILENO
#else
# define FILENO_STDIN		0
#endif

#ifdef STDOUT_FILENO
# define FILENO_STDOUT		STDOUT_FILENO
#else
# define FILENO_STDOUT		1
#endif

#ifdef STDERR_FILENO
# define FILENO_STDERR		STDERR_FILENO
#else
# define FILENO_STDERR		2
#endif

/***********************************************************************
 *** File Support Routines
 ***********************************************************************/

/**
 * @return
 *	The number of open file descriptors; otherwise -1 and errno set
 *	to ENOSYS if the necessary functionality is not implemented.
 */
extern int getOpenFileCount(void);

extern int fileSetCloseOnExec(int fd, int flag);
extern int fileSetPermsById(int fd, uid_t uid, gid_t gid, mode_t mode);
extern int pathSetPermsById(const char *path, uid_t uid, gid_t gid, mode_t mode);
extern int fileSetPermsByName(int fd, const char *user, const char *group, mode_t mode);
extern int pathSetPermsByName(const char *path, const char *user, const char *group, mode_t mode);
extern int mkpath(const char *path);

/*
 * Define the flock() constants separately, since some systems
 * have flock(), but fail to define the constants in a header.
 * These values were taken from FreeBSD.
 */
#ifndef HAVE_LOCK_SH
# define LOCK_SH	0x01		/* shared file lock */
# define LOCK_EX	0x02		/* exclusive file lock */
# define LOCK_NB	0x04		/* don't block when locking */
# define LOCK_UN	0x08		/* unlock file */
#endif

#if defined(ENABLE_ALT_FLOCK) || !defined(HAVE_FLOCK)
/*
 * Mac OS X links libc symbols ahead of replacement functions. In
 * order to ensure we link with our version, we need to use an
 * alternative name.
 */
#define flock	alt_flock

/*
 * Simulated flock() with fcntl(), lockf(), or locking().
 */
extern int alt_flock(int, int);
#endif

/***********************************************************************
 ***
 ***********************************************************************/

# ifdef  __cplusplus
}
# endif
#endif /* __com_snert_lib_io_file_h__ */

