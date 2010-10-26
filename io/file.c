/*
 * file.c
 *
 * Copyright 2004, 2008 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <errno.h>
#include <string.h>

#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#endif

#ifndef __MINGW32__
# if defined(HAVE_PWD_H)
#  include <pwd.h>
# endif
# if defined(HAVE_GRP_H)
#  include <grp.h>
# endif
#endif

#if defined(HAVE_SYS_STAT_H)
# include <sys/stat.h>
#endif

#include <com/snert/lib/io/file.h>

int
fileSetPermsById(int fd, uid_t uid, gid_t gid, mode_t mode)
{
#ifdef __unix__
	if (fchown(fd, uid, gid) && errno != ENOENT) {
		syslog(LOG_ERR, "uid=%d fchown(%d, %d, %d) error: %s (%d)", getuid(), fd, uid, gid, strerror(errno), errno);
		return -1;
	}

	if (fchmod(fd, mode)) {
		syslog(LOG_ERR, "fchmod(%d, %o) error: %s (%d)", fd, mode, strerror(errno), errno);
		return -2;
	}
#endif
	return 0;
}

int
pathSetPermsById(const char *path, uid_t uid, gid_t gid, mode_t mode)
{
#ifdef __unix__
	if (chown(path, uid, gid) && errno != ENOENT) {
		syslog(LOG_ERR, "uid=%d chown(\"%s\", %d, %d) error: %s (%d)", getuid(), path, uid, gid, strerror(errno), errno);
		return -1;
	}

	if (chmod(path, mode)) {
		syslog(LOG_ERR, "chmod(\"%s\", %o) error: %s (%d)", path, mode, strerror(errno), errno);
		return -2;
	}
#endif
	return 0;
}

int
fileSetPermsByName(int fd, const char *user, const char *group, mode_t mode)
{
#ifdef __unix__
	struct group *gr;
	struct passwd *pw;

	if ((pw = getpwnam(user)) == NULL) {
		syslog(LOG_ERR, "user \"%s\" not found", user);
		return -1;
	}

	if ((gr = getgrnam(group)) == NULL) {
		syslog(LOG_ERR, "group \"%s\" not found", group);
		return -1;
	}

	if (fchown(fd, pw->pw_uid, gr->gr_gid) && errno != ENOENT) {
		syslog(LOG_ERR, "uid=%d fchown(%d, \"%s\", \"%s\") error: %s (%d)", getuid(), fd, user, group, strerror(errno), errno);
		return -1;
	}

	if (fchmod(fd, mode)) {
		syslog(LOG_ERR, "fchmod(%d, %o) error: %s (%d)", fd, mode, strerror(errno), errno);
		return -2;
	}
#endif
	return 0;
}

int
pathSetPermsByName(const char *path, const char *user, const char *group, mode_t mode)
{
#ifdef __unix__
	struct group *gr;
	struct passwd *pw;

	if ((pw = getpwnam(user)) == NULL) {
		syslog(LOG_ERR, "user \"%s\" not found", user);
		return -1;
	}

	if ((gr = getgrnam(group)) == NULL) {
		syslog(LOG_ERR, "group \"%s\" not found", group);
		return -1;
	}

	if (chown(path, pw->pw_uid, gr->gr_gid) && errno != ENOENT) {
		syslog(LOG_ERR, "uid=%d chown(\"%s\", \"%s\", \"%s\") error: %s (%d)", getuid(), path, user, group, strerror(errno), errno);
		return -1;
	}

	if (chmod(path, mode)) {
		syslog(LOG_ERR, "chmod(\"%s\", %o) error: %s (%d)", path, mode, strerror(errno), errno);
		return -2;
	}
#endif
	return 0;
}

int
fileSetCloseOnExec(int fd, int flag)
{
#ifdef __unix__
	int flags;

	if ((flags = fcntl(fd, F_GETFD)) == -1)
		return -1;

	if (flag)
		flags |= FD_CLOEXEC;
	else
		flags &= ~FD_CLOEXEC;

	return fcntl(fd, F_SETFD, flags);
#else
	return 0;
#endif
}

/**
 * @return
 *	The number of open file descriptors; otherwise -1 and errno set
 *	to ENOSYS if the necessary functionality is not implemented.
 */
int
getOpenFileCount(void)
{
#if defined(HAVE_GETDTABLESIZE) && defined(HAVE_ISATTY)
	int fd, max_fd, count;

	max_fd = getdtablesize();
	for (count = 0, fd = 0; fd < max_fd; fd++) {
		/* Is it a valid tty or a valid file descriptor
		 * that doesn't point to a tty.
		 */
		if (isatty(fd) || errno != EBADF)
			count++;
	}

	return count;
#else
	errno = ENOSYS;
	return -1;
#endif
}
