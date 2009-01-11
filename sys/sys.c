/*
 * sys.c
 *
 * Copyright 2008 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <stdlib.h>

#ifdef HAVE_UNISTD_H
# ifdef __linux__
#  /* See Linux man setresgid */
#  define _GNU_SOURCE
# endif
# include <unistd.h>
#endif

#if defined(__linux__) && defined(HAVE_SYS_SYSINFO_H)
# include <sys/sysinfo.h>
#endif
#if defined(__OpenBSD__) && defined(HAVE_SYS_SYSCTL_H)
# include <sys/param.h>
# include <sys/sysctl.h>
#endif

#include <com/snert/lib/sys/process.h>

#ifdef __WIN32__
# include <windows.h>
#endif

#if defined(__OpenBSD__) && defined(HAVE_SYS_SYSCTL_H)
int
getSysCtlInt(int mib0, int mib1)
{
	size_t size;
	int mib[2], one, value;

	one = 1;
	mib[0] = mib0;
	mib[1] = mib1;
	size = sizeof (value);

	if (sysctl(mib, 2, &value, &size, NULL, 0))
		return -1;

	return value;
}

char *
getSysCtlString(int mib0, int mib1)
{
	size_t size;
	char *string;
	int mib[2], one;

	one = 1;
	mib[0] = mib0;
	mib[1] = mib1;

	if (sysctl(mib, 2, NULL, &size, NULL, 0))
		return NULL;

	if ((string = malloc(size)) == NULL)
		return NULL;

	if (sysctl(mib, 2, string, &size, NULL, 0))
		return NULL;

	return string;
}
#endif

long
sysGetCpuOnline(void)
{
#if defined(HAVE_SYSCONF) && defined(_SC_NPROCESSORS_ONLN)
	return sysconf(_SC_NPROCESSORS_ONLN);
#elif defined(HAVE_SYSCONF) && defined(_SC_NPROC_ONLN)
	return sysconf(_SC_NPROC_ONLN);
#elif defined(HAVE_SYSCONF) && defined(_SC_CRAY_NCPU)
	return sysconf(_SC_CRAY_NCPU);
#elif defined(HAVE_GET_NPROCS)
	return get_nprocs();
#elif defined(HAVE_SYS_SYSCTL_H) && defined(CTL_HW) && defined(HW_NCPU)
	return getSysCtlInt(CTL_HW, HW_NCPU);
#elif defined(__WIN32__)
	SYSTEM_INFO info;
	GetSystemInfo(&info);
	return info.dwNumberOfProcessors;
#endif
	return -1;
}

long
sysGetCpuCount(void)
{
#if defined(HAVE_SYSCONF) && defined(_SC_NPROCESSORS_CONF)
	return sysconf(_SC_NPROCESSORS_CONF);
#elif defined(HAVE_SYSCONF) && defined(_SC_NPROC_CONF)
	return sysconf(_SC_NPROC_CONF);
#elif defined(HAVE_GET_NPROCS_CONF)
	return get_nprocs_conf();
#else
	return sysGetCpuOnline();
#endif
}

#ifdef TEST

#include <stdio.h>

int
main(int argc, char **argv)
{
	printf("cpu-count=%ld cpu-active=%ld\n", sysGetCpuCount(), sysGetCpuOnline());

	return 0;
}
#endif
