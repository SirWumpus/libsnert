#######################################################################
# And so it begins...
#######################################################################

AC_INIT(LibSnert, 1.75, [Anthony Howe <achowe@snert.com>])
AC_PREREQ(2.69)

dnl
dnl AC_INIT sets ac_default_prefix=value and prefix=NONE. --prefix will
dnl set prefix, but the default is not applied until AC_OUTPUT.  So we
dnl need a working copy for AC_DEFINE and any other uses between here
dnl and AC_OUTPUT
dnl
AS_IF([test "$prefix" = 'NONE'],[
	prefix_copy="$ac_default_prefix"
],[
	prefix_copy="$prefix"
])
AC_SUBST(prefix_copy)

SNERT_INIT(LIBSNERT, [Copyright 1996, 2015 by Anthony Howe. All rights reserved.])

AC_DEFINE_UNQUOTED(LIBSNERT_UNAME, [["${platform}"]])
AH_TEMPLATE(LIBSNERT_UNAME)

#######################################################################
# Auxliary files to be modified later
#######################################################################

AC_CONFIG_HEADERS([
	config.h
	include/version.h:version.h.in
])
AC_CONFIG_FILES([
	version.h.in
	version.sh
	libsnert.pc
	makefile
	crc/makefile
	type/makefile
	util/makefile
	mail/makefile
	net/makefile
	sys/makefile
	io/makefile
	tools/makefile
])
dnl AC_CANONICAL_TARGET

dnl Not yet supported.
dnl AM_INIT_AUTOMAKE([-Wall -Werror -Wno-portability foreign dist-bzip2 no-define no-installinfo subdir-objects])

#######################################################################
# Setup paths
#######################################################################

# The total perspective vortex "You are here..."
AC_CONFIG_SRCDIR($PACKAGE_NAME.c)

abs_tardir=`pwd | sed -e 's,/cygdrive/\(.\),\1:,' -e 's,^\(.*\)/com/snert/src/.*,\1,'`
AC_SUBST(abs_incdir, $abs_tardir/com/snert/include)
AC_SUBST(abs_libdir, $abs_tardir/com/snert/lib)
AC_SUBST(abs_tardir, $abs_tardir)

dnl Substitions for make.
AC_SUBST(snert_tardir,'${top_srcdir}/../../../..')
AC_SUBST(snert_incdir,'${top_srcdir}/../../include')
AC_SUBST(snert_libdir,'${top_srcdir}/../../lib')

dnl Remember the command line settings.
start_CPPFLAGS="$CPPFLAGS"
start_LDFLAGS="$LDFLAGS"
start_CFLAGS="$CFLAGS"
start_LIBS="$LIBS"

dnl Make sure we can find our includes during configure.
SNERT_JOIN_UNIQ([CPPFLAGS],[-I$srcdir/../../include],[head])
SNERT_JOIN_UNIQ([LDFLAGS],[-L$srcdir/../../lib],[head])

#######################################################################
#	Configuration options: settings
#######################################################################

SNERT_OPTION_ENABLE_64BIT

AC_ARG_ENABLE(access-tagless,
	[AC_HELP_STRING([--enable-access-tagless],[support for tagless access records (Sendmail, Postfix)])],
	[
		AC_DEFINE(ENABLE_ACCESS_TAGLESS,[],[Support for tagless access records (Sendmail, Postfix)])
		AC_SUBST(enable_access_tagless)
	]
)

AC_ARG_ENABLE(cache-db-185,
	[AC_HELP_STRING([--enable-cache-db-185],[try cache with Berkeley DB 1.85 (unsupported)])],
	[AC_DEFINE_UNQUOTED(IGNORE_CORRUPT_CACHE_ISSUE_WITH_DB_185)]
)

SNERT_OPTION_ENABLE_DEBUG

AC_ARG_ENABLE(debug-mutex,
	[AC_HELP_STRING([--enable-debug-mutex],[enable lockpick mutex debug functions])],
	[AC_DEFINE_UNQUOTED(DEBUG_MUTEX, [$enable_debug_mutex], [Enable application mutex debugging; set to 2 to include library hooking.])]
)

SNERT_OPTION_ENABLE_FORK
SNERT_OPTION_ENABLE_FCNTL_LOCKS
SNERT_OPTION_ENABLE_MINGW
SNERT_OPTION_ENABLE_POPAUTH

#######################################################################
#	Configuration options: packages
#######################################################################

SNERT_OPTION_WITH_DB
SNERT_OPTION_WITH_LIBEV
SNERT_OPTION_WITH_LUA
SNERT_OPTION_WITH_MILTER
SNERT_OPTION_WITH_OPENSSL
dnl Not used yet.
dnl SNERT_OPTION_WITH_SASL2
SNERT_OPTION_WITH_PTHREAD
SNERT_OPTION_WITH_SQLITE3
SNERT_BUILD_THREADED_SQLITE3
SNERT_OPTION_WITH_WINDOWS_SDK

AC_ARG_WITH(mutex,
	[AC_HELP_STRING([--with-mutex=API],[where API is fcntl, flock, posix, systemv, win32])], [
		if test "$withval" = "fcntl"; then
			with_mutex='FCNTL_API'
		elif test "$withval" = "flock"; then
			with_mutex='FLOCK_API'
		elif test "$withval" = "posix"; then
			with_mutex='POSIX_API'
		elif test "$withval" = "systemv"; then
			with_mutex='SYSTEMV_API'
		elif test "$withval" = "win32"; then
			with_mutex='WIN32_API'
		else
			with_mutex='UNKNOWN_API'
		fi
	],[
		with_mutex='UNKNOWN_API'
	]
)

AC_ARG_WITH(shar-mem,
	[AC_HELP_STRING([--with-shar-mem=API],[where API is malloc, mmap-anon, posix, systemv, squeeze])], [
		if test "$withval" = "malloc"; then
			with_shar_mem='MALLOC_API'
		elif test "$withval" = "mmap-anon"; then
			with_shar_mem='MMAP_ANON'
		elif test "$withval" = "posix"; then
			with_shar_mem='POSIX_API'
		elif test "$withval" = "systemv"; then
			with_shar_mem='SYSTEMV_API'
		elif test "$withval" = "squeeze"; then
			echo
			echo "Oi! Don't squeeze the shar-mem! :>"
			echo
			with_shar_mem='UNKNOWN_API'
		else
			with_shar_mem='UNKNOWN_API'
		fi
	],[
		with_shar_mem='UNKNOWN_API'
	]
)

#######################################################################
#	Check for programs
#
#	Must do this AFTER setting up Borland specifc changes that
#	AC_PROG_CC won't detect otherwise.
#######################################################################

AS_ECHO()
AS_ECHO("Checking C compiler and common headers...")
AS_ECHO()
SNERT_CC_INFO

AC_HEADER_ASSERT
AC_C_CONST
AC_SYS_LONG_FILE_NAMES

AC_CHECK_TYPES([mode_t, off_t, pid_t, uid_t, gid_t, size_t, ssize_t, time_t, intptr_t, uintptr_t])

SNERT_CHECK_PREDEFINE([__WIN32__])

#######################################################################
# Check for programs.
#######################################################################

AS_ECHO()
AS_ECHO("Checking for assorted tools...")
AS_ECHO()

AC_PROG_LN_S
AC_PROG_MAKE_SET
dnl AC_PROG_INSTALL
AC_PATH_PROGS([MD5SUM],[md5sum md5])
SNERT_TAR_SETTINGS

dnl Not yet.
dnl LT_PREREQ([2.2.6])
dnl LT_INIT([disable-fast-install])
dnl AC_SUBST([LIBTOOL_DEPS])

#######################################################################
#	Check for library, header, and function families.
#######################################################################

SNERT_FIND_LIBC
SNERT_BACKTRACE
SNERT_ANSI_TIME
SNERT_PROCESS
SNERT_POSIX_IO
SNERT_POSIX_SIGNALS
SNERT_POSIX_SEMAPHORES
SNERT_FILE_LOCKS
SNERT_TERMIOS
SNERT_PTHREAD
SNERT_HASHES
SNERT_OPENSSL
SNERT_SQLITE3
SNERT_LIBEV
SNERT_LUA
SNERT_SASL2
SNERT_BERKELEY_DB
SNERT_SYSTEMV_SEMAPHORES
SNERT_NETWORK
SNERT_SYS
SNERT_PAM
SNERT_SETJMP
SNERT_RANDOM
SNERT_EXTRA_STDIO
SNERT_ANSI_STRING
SNERT_EXTRA_STRING
SNERT_REGEX
SNERT_LIBMILTER

echo
echo 'Miscellanous header & function checks...'
echo

AC_CHECK_HEADERS([sys/mman.h sys/shm.h])
AC_CHECK_HEADERS([ctime.h grp.h pwd.h sys/file.h])
AC_CHECK_FUNCS([flockfile funlockfile])

#######################################################################
#	Check for library functions
#######################################################################

AC_FUNC_MALLOC
AC_LIBOBJ(malloc)

AC_FUNC_MMAP
AC_FUNC_SETVBUF_REVERSED

AC_CHECK_FUNCS(brk sbrk)
AC_CHECK_FUNCS(atexit getenv putenv toupper tolower)

dnl SNERT_FUNC_FLOCK
AC_CHECK_FUNCS(flock fcntl lockf locking)
SNERT_CHECK_DEFINE(O_BINARY, fcntl.h)
SNERT_CHECK_DEFINE(LOCK_SH, sys/file.h)
if test $ac_cv_define_LOCK_SH = 'yes'; then
	AC_DEFINE_UNQUOTED(HAVE_LOCK_SH)
fi

#######################################################################
#	Determine avaliable Mutex APIs
#######################################################################

has_mutex_fcntl_api='yes'
AC_CHECK_FUNCS(fcntl, [], [has_mutex_fcntl_api='no'])

has_mutex_flock_api='yes'
AC_CHECK_FUNCS(flock, [], [has_mutex_flock_api='no'])

if test "$ac_cv_header_windows_h" = 'yes'; then
	has_mutex_win32_api='yes'
else
	has_mutex_win32_api='no'
fi

AC_MSG_CHECKING([which mutex api to use])

if test "$with_mutex" = 'UNKNOWN_API'; then
	# Out in left field...
	if test "$ac_cv_define___WIN32__" = 'yes'; then
		with_mutex='WIN32_API'
	fi

	# When unknown, prefer POSIX standard over SystemV.
	if test "$with_mutex" = 'UNKNOWN_API'; then
		if test ${ac_cv_func_sem_init:-no} = 'yes'; then
			with_mutex='POSIX_API'
		elif test $snert_systemv_semaphores = 'yes'; then
			with_mutex='SYSTEMV_API'
		elif test $has_mutex_fcntl_api = 'yes'; then
			with_mutex='FCNTL_API'
		elif test $has_mutex_flock_api = 'yes'; then
			with_mutex='FLOCK_API'
		fi
	fi
fi

AC_DEFINE_UNQUOTED(SERIALIZATION_API, $with_mutex, [(deprecated)])
AC_SUBST(SERIALIZATION_API, "$with_mutex")
AC_MSG_RESULT($with_mutex)

#######################################################################
#	Determine avaliable Shared Memory APIs
#######################################################################

has_shm_mmap_api='yes'
AC_CHECK_FILE(/dev/zero)
SNERT_CHECK_DEFINE(MMAP_ANON, sys/mman.h)
AC_CHECK_FUNCS([mmap munmap], [], [has_shm_mmap_api='no'])
if test "$has_shm_mmap_api" = 'yes'; then
	if test "$ac_cv_define_MMAP_ANON" = 'yes'; then
		has_shm_mmap_api='anon'
	elif test "$ac_cv_file__dev_zero" = 'no'; then
		has_shm_mmap_api='no'
	fi
fi

has_shm_systemv_api='yes'
AC_CHECK_FUNCS([shmget shmat shmctl shmdt], [], [has_shm_systemv_api='no'])

if test "$ac_cv_header_windows_h" = 'yes'; then
	has_shm_win32_api='yes'
else
	has_shm_win32_api='no'
fi

dnl Shared memory API
dnl APR_CHECK_FILE(/dev/zero)
dnl
dnl if test $with_shar_mem = 'UNKNOWN_API'; then
dnl 	with_shar_mem='POSIX_API'
dnl 	SNERT_CHECK_DEFINE(MMAP_ANON, sys/mman.h)
dnl 	AC_CHECK_FUNCS([mmap munmap], [], [with_shar_mem='UNKNOWN_API'])
dnl
dnl 	if test $with_shar_mem = 'POSIX_API'; then
dnl 		if test $ac_cv_define_MMAP_ANON = yes; then
dnl 			with_shar_mem='MMAP_ANON'
dnl 		elif test $ac_cv_file__dev_zero = no; then
dnl 			with_shar_mem='UNKNOWN_API'
dnl 		fi
dnl 	fi
dnl fi
dnl if test $with_shar_mem = 'UNKNOWN_API'; then
dnl 	with_shar_mem='SYSTEMV_API'
dnl 	AC_CHECK_FUNCS([shmget shmat shmctl shmdt], [], [with_shar_mem='UNKNOWN_API'])
dnl fi
dnl if test $with_shar_mem = 'UNKNOWN_API'; then
dnl 	if test $ac_cv_header_windows_h = yes; then
dnl 		with_shar_mem='MALLOC_API'
dnl 	fi
dnl fi

AC_MSG_CHECKING([which shared memory api to use])

if test "$with_shar_mem" = 'UNKNOWN_API'; then
	# Out in right field...
	if test "$ac_cv_define___WIN32__" = 'yes'; then
		with_shar_mem='WIN32_API'
	fi

	# When unknown, prefer POSIX standard over others.
	if test "$with_shar_mem" = 'UNKNOWN_API'; then
		if test "$has_shm_mmap_api" = 'yes'; then
			with_shar_mem='POSIX_API'
		elif test "$has_shm_mmap_api" = 'anon'; then
			with_shar_mem='MMAP_ANON'
		elif test "$has_shm_systemv_api" = 'yes'; then
			with_shar_mem='SYSTEMV_API'
		fi
	fi
fi

AC_DEFINE_UNQUOTED(SHARED_MEMORY_API, $with_shar_mem, [(deprecated)])
AC_SUBST(SHARED_MEMORY_API, "$with_shar_mem")
AC_MSG_RESULT($with_shar_mem)

#######################################################################
#	Generate output.
#######################################################################

AH_TOP([
/*
 * config.h
 *
 * Copyright 2002, 2015 by Anthony Howe. All rights reserved.
 */

#ifndef __com_snert_lib_config_h__
#define __com_snert_lib_config_h__	1

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************************
 ***
 ***********************************************************************/

#define __LIBSNERT__		1

#undef LIBSNERT_MAJOR
#undef LIBSNERT_MINOR
#undef LIBSNERT_BUILD
#undef LIBSNERT_NUMBER
#undef LIBSNERT_NAME
#undef LIBSNERT_VERSION
#undef LIBSNERT_STRING
#undef LIBSNERT_AUTHOR
#undef LIBSNERT_COPYRIGHT
#undef LIBSNERT_DEBUG
#undef LIBSNERT_UNAME

#undef LIBSNERT_CFLAGS
#undef LIBSNERT_LDFLAGS
#undef LIBSNERT_LIBS
#undef LIBSNERT_CONFIGURE
#undef LIBSNERT_BUILT
#undef LIBSNERT_BUILD_HOST
#undef LIBSNERT_PLATFORM

#undef LIBSNERT_SHARE

#ifndef __@platform@__
#define __@platform@__		1
#endif

#undef LIBC_PATH
#undef LIBPTHREAD_PATH

/* Sun defines __unix, but not __unix__ */
#if defined(__unix) && !defined(__unix__)
#define __unix__		1
#endif

#if defined(__APPLE__) && defined(__Darwin__)
#define __unix__		1
#endif

#if defined(_AIX) && !defined(__unix__)
#define __unix__		1
#endif
])

AH_BOTTOM([
/*
 * Define the flock() constants separately, since some systems
 * have flock(), but fail to define the constants in a header.
 * These values were taken from FreeBSD.
 */
#undef HAVE_LOCK_SH

#undef HAVE_FCNTL
#undef HAVE_FLOCK
#undef HAVE_LOCKF
#undef HAVE_LOCKING
#undef ENABLE_ALT_FLOCK

#ifndef EXIT_SUCCESS
# define EXIT_SUCCESS	0	/* TRUE or normal exit - ANSI C */
#endif
#ifndef EXIT_FAILURE
# define EXIT_FAILURE	1	/* FALSE or unspecified error - ANSI C */
#endif

/* This has been used fairly consistently in Snert code since 1990. */
#define EXIT_USAGE	2	/* Invalid or missing command-line option. */

#define QUOTE(x)	QUOTE_THIS(x)
#define QUOTE_THIS(x)	#x

/**
 * @param a
 *	A pointer to object A.
 *
 * @param b
 *	A pointer to object B.
 *
 * @return
 *	Zero if A == B, negative if A < B, or positive if A > B.
 */
#define HAVE_CMPFN_T
typedef int (*CmpFn)(void *a, void *b);

/***********************************************************************
 *** LibSnert Configure And Compile Time Information
 ***********************************************************************/

#ifndef LINE_WRAP
#define LINE_WRAP			72
#endif

extern void printVar(int columns, const char *name, const char *value);
extern void snertPrintVersion(void);
extern void snertPrintInfo(void);

/***********************************************************************
 *** Alternate Memory Allocator
 ***********************************************************************/

/**
 * @param ptr
 *	A pointer to an object to be freed.
 */
#define HAVE_FREEFN_T
typedef void (*FreeFn)(void *);

/*
 * Intended for debugging in combination with valgrind, which often
 * reports "...points to uninitialised byte(s)" when part of a buffer
 * remains unused. Should only be disabled once you are sure that what
 * valgrind is reporting can be safely ignored.
 */
#ifdef NDEBUG
# define MEMSET(p, b, s)
# define MALLOC(s)		malloc(s)
# define CALLOC(n,s)		calloc((n),(s))
# define REALLOC(p,s)		realloc((p),(s))
# define ALIGNED_ALLOC(a,s)	aligned_alloc((a),(s))
# define FREE(p)		free(p)
#else
# define MEMSET(p, b, s)	memset((p),(b),(s))
# define MALLOC(s)		alt_malloc((s), ALT_MALLOC_BYTE(0))
# define CALLOC(n,s)		alt_calloc((n),(s), ALT_MALLOC_BYTE(0))
# define REALLOC(p,s)		alt_realloc((p),(s), ALT_MALLOC_BYTE(0))
# define ALIGNED_ALLOC(a,s)	alt_aligned_alloc((a),(s), ALT_MALLOC_BYTE(0))
# define FREE(p)		alt_free((p), 0)
#endif

#if defined(HAVE_SYS_TYPES_H) && defined(HAVE_SIZE_T)
# include <sys/types.h>
#endif

/**
 * Alternative malloc intended for debugging, in particular
 * with Valgrind, which reports certain classes of errors
 * about uninitialised data. Sometimes production code calls
 * for malloc() while debug code wants calloc() like behaviour.
 *
 * @param size
 *	The size in bytes of the area to allocate.
 *
 * @param flags
 *	A bit-wise OR of assort ALT_MALLOC_ flags.
 *
 * @return
 *	Pointer to a continuous block of memory at least "size" bytes.
 */
extern void *alt_malloc(size_t size, unsigned flags);

extern void *alt_realloc(void *ptr, size_t size, unsigned flags);

extern void *alt_calloc(size_t num_elements, size_t element_size, unsigned flags);

extern void *alt_aligned_alloc(size_t alignment, size_t size, unsigned flags);

/*
 * @param mem
 *	Pointer to a block of memory previous returned by malloc,
 *	calloc, or realloc.
 *
 * @param flags
 *	A bit-wise OR of assort ALT_FREE_ flags.
 */
extern void alt_free(void *ptr, unsigned flags);

/*
 * @param mem
 * 	Pointer to a pointer to void, which will be set to NULL once
 *	the block of memory is freed.
 *
 * @param flags
 *	A bit-wise OR of assort ALT_FREE_ flags.
 */
extern void alt_free_clear(void **ptr, unsigned flags);

/*
 * @param ptr
 *	ptr is declared as a "void *" for compatibility with
 *	pthread_cleanup_push(), but is treated as a "void **" so
 *	that the pointer variable can be set to NULL once freed.
 */
extern void free_clear(void *ptr);

#define ALT_MALLOC_FILL		0x0100
#define ALT_MALLOC_ABORT	0x0200
#define ALT_MALLOC_BYTE_MASK	0x00FF
#define ALT_MALLOC_BYTE(x)	(ALT_MALLOC_FILL|(unsigned char)(x))

/***********************************************************************
 *** GNU Backtrace
 ***********************************************************************/

#ifdef HAVE_BACKTRACE
extern int backtrace(void **buffer, int size);
extern char **backtrace_symbols(void **buffer, int length);
extern void backtrace_symbols_fd(void **buffer, int length, int fd);
#endif

/***********************************************************************
 ***
 ***********************************************************************/

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_version_h__ */
])

dnl Make sure we can find our includes relative top_srcdir in makefiles.
CPPFLAGS="-I$snert_incdir${start_CPPFLAGS:+ $start_CPPFLAGS}"
LDFLAGS="-L$snert_libdir${start_LDFLAGS:+ $start_LDFLAGS}"
CFLAGS="$start_$CFLAGS"
LIBS="$start_LIBS"

SNERT_FINI

echo
echo "Generating files..."
echo
AC_OUTPUT()

echo
echo $PACKAGE_NAME/$package_major.$package_minor.$package_build
echo $package_copyright
echo
AC_MSG_RESULT([  Platform............: $platform $CC ${GCC_MAJOR} ${GCC_MINOR} ${GCC_PATCH}])
AC_MSG_RESULT([  prefix..............: $prefix])
AC_MSG_RESULT([  datarootdir.........: $datarootdir])
AC_MSG_RESULT([  Berkeley DB.........: $bdb_version "$CFLAGS_DB" "$LDFLAGS_DB" "${HAVE_LIB_DB}"])
AC_MSG_RESULT([  Lua.................: "$CPPFLAGS_LUA" "$LDFLAGS_LUA" "$LIBS_LUA"])
AC_MSG_RESULT([  libev...............: "$CPPFLAGS_LIBEV" "$LDFLAGS_LIBEV" "$LIBS_LIBEV"])
AC_MSG_RESULT([  OpenSSL.............: "$CPPFLAGS_SSL" "$LDFLAGS_SSL" "$LIBS_SSL"])
AC_MSG_RESULT([  POSIX Semaphores....: "" "" "$HAVE_LIB_SEM"])
AC_MSG_RESULT([  POSIX Threads.......: "$CFLAGS_PTHREAD" "$LDFLAGS_PTHREAD" "$HAVE_LIB_PTHREAD"])
AC_MSG_RESULT([  Sendmail libmilter..: "$CPPFLAGS_MILTER" "$LDFLAGS_MILTER" "$LIBS_MILTER"])
AC_MSG_RESULT([  SQLite3.............: "$CPPFLAGS_SQLITE3" "$LDFLAGS_SQLITE3" "$LIBS_SQLITE3"])
AC_MSG_RESULT([  Semaphore API.......: $with_mutex])
AC_MSG_RESULT([  Shared Memory API...: $with_shar_mem])
AC_MSG_RESULT([  CFLAGS..............: $CFLAGS])
AC_MSG_RESULT([  CPPFLAGS............: $CPPFLAGS])
AC_MSG_RESULT([  LDFLAGS.............: $LDFLAGS])
AC_MSG_RESULT([  LIBS................: $LIBS])
AC_MSG_RESULT([  Network LIBS........: $NETWORK_LIBS])
echo

#######################################################################
#	-END-
#######################################################################
