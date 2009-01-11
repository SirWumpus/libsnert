/*
 * posix.h
 *
 * Copyright 2001, 2006 by Anthony Howe.  All rights reserved.
 */

#ifndef __com_snert_lib_io_posix_h__
#define __com_snert_lib_io_posix_h__	1

#ifndef LIBSNERT_VERSION
# include <com/snert/lib/version.h>
#endif

#ifndef BUFSIZ
# include <stdio.h>
#endif
#ifdef HAVE_SYS_FILE_H
# include <sys/file.h>
#endif
#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif

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

#ifdef __cplusplus
extern "C" {
#endif

#if HAVE_UNISTD_H
# include <unistd.h>
#else
# if defined(__BORLANDC__) && defined(HAVE_IO_H)
#  include <io.h>
#  include <sys/locking.h>
# else

extern int close(int);
extern pid_t getpgid(pid_t);
extern int open(const char *, int, ...);
extern long read(int, void *, size_t);
extern long write(int, void *, size_t);

# endif /* defined(__BORLANDC__) && defined(HAVE_IO_H) */

# if defined(__WIN32__)
#  if defined(__VISUALC__)
#   define _WINSOCK2API_
#  endif

extern long getpid(void);
extern unsigned int sleep(unsigned int);
extern void *sbrk(long);

# endif /* defined(__WIN32__) */
#endif /* HAVE_UNISTD_H */

#if ! defined(O_BINARY)
# define O_BINARY		0
#endif

extern pid_t getpgid(pid_t);

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_io_posix_h__ */
