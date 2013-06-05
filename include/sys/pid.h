/*
 * pid.h
 *
 * Copyright 2004, 2006 by Anthony Howe.  All rights reserved.
 */

#ifndef __com_snert_lib_sys_pid_h__
#define __com_snert_lib_sys_pid_h__	1

#ifdef __cplusplus
extern "C" {
#endif

#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#ifdef HAVE_SIGNAL_H
# include <signal.h>
#endif

#if defined(__MINGW32__) && !defined(SIGKILL)
#include <windows.h>
# define SIGKILL		9
extern int kill(HANDLE, int);
#endif

/**
 * @param filename
 *	A pointer to a C string containing the path to the .pid file
 *	in which to save the process-id.
 *
 * @return
 *	A non-zero pid, otherwise zero (0) on error.
 */
extern int pidSave(const char *filename);

/**
 * @param filename
 *	A pointer to a C string containing the path to the .pid file
 *	from which to fetch a process-id.
 *
 * @return
 *	A non-zero pid, otherwise zero (0) on error.
 */
extern pid_t pidLoad(const char *filename);

/**
 * @param filename
 *	A pointer to a C string containing the path to the .pid file
 *	from which to fetch a process-id to be killed.
 *
 * @return
 *	A non-zero pid, otherwise zero (0) on error.
 */
extern int pidKill(const char *filename, int signal);

/**
 * @param filename
 *	A pointer to a C string containing the path to the .pid file
 *	for which to get an exclusive file lock.
 *
 * @return
 *	A file descriptor of an exclusively locked .pid file. Otherwise
 *	-1 on failure.
 */
extern int pidLock(const char *filename);

/**
 * @param fd
 *	Unlock and close a previous .pid file lock.
 */
extern void pidUnlock(int fd);

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_sys_pid_h__ */
