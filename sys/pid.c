/*
 * pid.c
 *
 * PID File Routines
 *
 * Copyright 2004, 2006 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#define _REENTRANT	1

#include <com/snert/lib/version.h>

#include <errno.h>
#include <stdio.h>

#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#endif
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include <com/snert/lib/io/file.h>
#include <com/snert/lib/sys/pid.h>

/***********************************************************************
 ***
 ***********************************************************************/

/**
 * @param filename
 *	A pointer to a C string containing the path to the .pid file
 *	in which to save the process-id.
 *
 * @return
 *	A non-zero pid, otherwise zero (0) on error.
 */
int
pidSave(const char *filename)
{
	FILE *fp;

	if (filename == NULL || *filename == '\0' || (fp = fopen(filename, "w")) == NULL) {
		/* If the daemon is configured to be run typically as
		 * root, then you want to exit when a normal user runs
		 * and fails to write the pid file into a root-only
		 * location like /var/run.
		 */
		return -1;
	}

	(void) flock(fileno(fp), LOCK_EX);
	(void) fprintf(fp, "%d\n", getpid());
	(void) flock(fileno(fp), LOCK_UN);
	(void) fclose(fp);

	return 0;
}

/**
 * @param filename
 *	A pointer to a C string containing the path to the .pid file
 *	from which to fetch a process-id.
 *
 * @return
 *	A non-zero pid, otherwise zero (0) on error.
 */
pid_t
pidLoad(const char *filename)
{
	FILE *fp;
	pid_t pid = 0;

	if ((fp = fopen(filename, "r")) != NULL) {
		(void) flock(fileno(fp), LOCK_SH|LOCK_NB);
		fscanf(fp, "%d", &pid);
		(void) flock(fileno(fp), LOCK_UN);
		fclose(fp);
	}

	return pid;
}

/**
 * @param filename
 *	A pointer to a C string containing the path to the .pid file
 *	from which to fetch a process-id to be killed.
 *
 * @return
 *	A non-zero pid, otherwise zero (0) on error.
 */
int
pidKill(const char *filename, int signal)
{
#ifndef __MINGW32__
	pid_t pid;

	if ((pid = pidLoad(filename)) != 0) {
		return kill(pid, signal);
	}
#endif
	return -1;
}

/**
 * @param filename
 *	A pointer to a C string containing the path to the .pid file
 *	for which to get an exclusive file lock.
 *
 * @return
 *	A file descriptor of an exclusively locked .pid file. Otherwise
 *	-1 on failure.
 */
int
pidLock(const char *filename)
{
#ifdef __unix__
	int fd;

	/* Open the file... */
	if ((fd = open(filename, O_RDWR, 0)) < 0) {
		/* ... or if it doesn't exist, create it. */
		if (errno != ENOENT || (fd = open(filename, O_RDWR|O_CREAT|O_EXCL, 0640)) < 0)
			return -1;
	}

	/* Get the file lock. */
	if (flock(fd, LOCK_EX | LOCK_NB)) {
		close(fd);
		return -1;
	}

	return fd;
#else
	return 0;
#endif
}

/**
 * @param fd
 *	Unlock and close a previous .pid file lock.
 */
void
pidUnlock(int fd)
{
#ifdef __unix__
	if (0 < fd) {
		(void) flock(fd, LOCK_UN);
		(void) close(fd);
	}
#endif
}
