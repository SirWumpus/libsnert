/*
 * process.c
 *
 * Copyright 2004, 2007 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#ifdef TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# ifdef HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#if !defined(__MINGW32__) && defined(HAVE_SYS_RESOURCE_H)
# include <sys/resource.h>
#endif

#if defined(__linux__) && defined(HAVE_SYS_PRCTL_H)
# include <sys/prctl.h>
#endif
#if defined(__OpenBSD__) && defined(HAVE_SYS_SYSCTL_H)
# include <sys/param.h>
# include <sys/sysctl.h>
#endif
#if defined(__FreeBSD__) && defined(HAVE_SYS_SYSCTL_H)
# include <sys/types.h>
# include <sys/sysctl.h>
#endif

#include <com/snert/lib/sys/process.h>

/***********************************************************************
 ***
 ***********************************************************************/

uid_t process_ruid;			/* Originally user at startup. */
uid_t process_euid;			/* Desired user. */
gid_t process_gid;			/* Desired group. */

/***********************************************************************
 *** Process Support Routines
 ***********************************************************************/

/*
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
int
processDropPrivilages(const char *run_user, const char *run_group, const char *run_dir, int run_jailed)
{
#ifdef __unix__
	struct group *gr = NULL;
	struct passwd *pw = NULL;

	process_gid = getgid();
	process_euid = geteuid();

	if ((process_ruid = getuid()) == 0) {
		if (*run_group != '\0') {
			if ((gr = getgrnam(run_group)) == NULL) {
				syslog(LOG_ERR, "group \"%s\" not found", run_group);
				return -1;
			}

			process_gid = gr->gr_gid;
		}

		if (*run_user != '\0') {
			if ((pw = getpwnam(run_user)) == NULL) {
				syslog(LOG_ERR, "user \"%s\" not found", run_user);
				return -1;
			}

			process_euid = pw->pw_uid;

			if (*run_group == '\0')
				process_gid = pw->pw_gid;
# if defined(HAVE_INITGROUPS)
			/* Make sure to set any supplemental groups  for the new
			 * user ID, which will release root's supplemental groups.
			 */
			if (initgroups(run_user, process_gid)) {
				syslog(LOG_ERR, "supplemental groups for \"%s\" not set", run_user);
				return -1;
			}
# endif
		}
# if defined(HAVE_SETGROUPS)
		/* No run-user specified, then try to drop root's suppliemental groups. */
		else if (setgroups(0, NULL)) {
			syslog(LOG_ERR, "failed to release root's supplemental groups");
			return -1;
		}
# endif
# if defined(HAVE_CHROOT)
		if (run_jailed) {
			if (chroot(run_dir)) {
				syslog(LOG_ERR, "chroot(%s) failed: %s (%d)", run_dir, strerror(errno), errno);
				return -1;
			}
			if (chdir("/")) {
				syslog(LOG_ERR, "chdir(\"/\") to jail root failed: %s (%d)", strerror(errno), errno);
				return -1;
			}
		} else
# endif
		if (*run_dir != '\0' && chdir(run_dir)) {
			syslog(LOG_ERR, "chdir(%s) failed: %s (%d)", run_dir, strerror(errno), errno);
			return -1;
		}

		/* Drop group privileges permanently. */
# if defined(HAVE_SETRESGID)
		(void) setresgid(process_gid, process_gid, process_gid);
# elif defined(HAVE_SETREGID)
		(void) setregid(process_gid, process_gid);
# else
		(void) setgid(process_gid);
# endif
		if (setuid(process_euid)) {
			syslog(LOG_ERR, "setuid(%d) failed: %s (%d)", process_euid, strerror(errno), errno);
			return -1;
		}
	}

	syslog(LOG_INFO, "process uid=%d gid=%d", getuid(), getgid());
#endif
	return 0;
}

/*
 * @param flag
 *	Set to one (1) or zero (0) to enable or disable core dumps.
 *	Otherwise specify any other value, such as -1, to query.
 *	Must be called after all setuid/setgid manipulation, else
 *	the value might be reset.
 *
 * @return
 *	Previous value of the flag.
 */
int
processDumpCore(int flag)
{
	int old_flag = 0;
#if !defined(__MINGW32__) && defined(RLIMIT_CORE)
	struct rlimit limit;

	if (getrlimit(RLIMIT_CORE, &limit) == 0) {
		old_flag = 0 < limit.rlim_cur;
		limit.rlim_cur = flag ? limit.rlim_max : 0;
		(void) setrlimit(RLIMIT_CORE, &limit);
	}
#endif
#if defined(__linux__) && defined(HAVE_SYS_PRCTL_H) && defined(PR_SET_DUMPABLE)
	old_flag = prctl(PR_GET_DUMPABLE, 0,0,0,0);
	/* Convert OpenBSD's 2nd core dump behaviour, to simply on. */
	if (0 <= flag && flag <= 2)
		(void) prctl(PR_SET_DUMPABLE, flag, 0,0,0);
#endif

#ifdef CHANGE_KERNEL_SETTINGS
/* We shouldn't do this on a per process baises. */
# if defined(__OpenBSD__) && defined(HAVE_SYS_SYSCTL_H) && defined(KERN_NOSUIDCOREDUMP)
/***
 *** On 23/02/2010 18:28, Theo de Raadt whispered from the shadows...:
 *** > 3. The program does not use file system setuid bits, BUT does use the
 *** > setuid() et al. system calls to drop privileges from root to some other
 ***
 *** In OpenBSD -- if you change uids, you don't get core dumps.
 ***/
{
	int mib[2], *new_flag = NULL;
	size_t old_size, new_size = 0;

	mib[0] = CTL_KERN;
	mib[1] = KERN_NOSUIDCOREDUMP;
	old_size = sizeof (old_flag);

	/* KERN_NOSUIDCOREDUMP interger values:
	 *
	 *   0	dump core,
	 *   1	disable dump core (default)
	 *   2	dump core to /var/crash.
	 */

	if (0 <= flag && flag <= 2) {
		/* Invert the sense of the flag for 0 or 1
		 * to match OpenBSD semantics.
		 */
		if (flag != 2)
			flag = !flag;
		new_flag = &flag;
		new_size = sizeof (flag);
	}

	(void) sysctl(mib, 2, &old_flag, &old_size, new_flag, new_size);
}
# endif
# if defined(__FreeBSD__) && defined(HAVE_SYS_SYSCTL_H)
{
	int *new_flag = NULL;
	size_t old_size, new_size = 0;

	old_size = sizeof (old_flag);

	/* Convert OpenBSD's 2nd core dump behaviour, to simply on. */
	if (flag == 2) flag = 1;
	if (flag == 0 || flag == 1) {
		new_flag = &flag;
		new_size = sizeof (flag);
	}

	errno = 0;
	(void) sysctlbyname("kern.sugid_coredump", &old_flag, &old_size, new_flag, new_size);
}
# endif
#endif /* CHANGE_KERNEL_SETTINGS */
	return old_flag;
}

