/*
 * daemon.c
 *
 * Copyright 2010 by Anthony Howe.  All rights reserved.
 */

#include <com/snert/lib/version.h>

#if !defined(__WIN32__)

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include <com/snert/lib/sys/process.h>

/*
 * This version of daemon() uses exit() from the parent process
 * to ensure atexit() handlers are called.  NetBSD daemon()
 * appears to use _exit().
 */
int
alt_daemon(int nochdir, int noclose)
{
	pid_t child_pid;

	if ((child_pid = fork()) == -1)
		return -1;

	if (child_pid != 0)
		exit(EXIT_SUCCESS);

	if (setsid() == -1)
		return -1;

	if (!nochdir && chdir("/"))
		return -1;

	if (!noclose) {
		if (freopen("/dev/null", "a", stderr) == NULL)
			return -1;
		if (freopen("/dev/null", "a", stdout) == NULL)
			return -1;
		if (freopen("/dev/null", "r", stdin) == NULL)
			return -1;
	}

	return 0;
}
#endif /* ! HAVE_DAEMON */
