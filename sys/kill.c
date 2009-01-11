/*
 * kill.c
 *
 * POSIX kill() cover function
 *
 * Copyright 2007 by Anthony Howe.  All rights reserved.
 */

#ifdef __WIN32__
#include <windows.h>
#include <errno.h>

#include <com/snert/lib/version.h>
#include <com/snert/lib/sys/pid.h>

int
kill(HANDLE pid, int signum)
{
	if (signum != SIGKILL)
		errno = EINVAL;
	else if (TerminateProcess(pid, 1))
		return 0;
	else
		errno = GetLastError();

	return -1;
}
#endif
