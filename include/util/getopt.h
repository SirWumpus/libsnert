/*
 * getopt.h
 *
 * Copyright 1992, 2004 by Anthony Howe.  All rights reserved.
 */

#ifndef __com_snert_lib_util_getopt_h__

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Mac OS X links libc symbols ahead of replacement functions. In
 * order to ensure we link with our version, we need to use an
 * alternative name.
 */
#define getopt alt_getopt
#define optopt alt_optopt
#define optind alt_optind
#define opterr alt_opterr
#define optarg alt_optarg

/*
 * gcc, sunos /usr/5bin/cc, and aix c89 all fail to correctly
 * declare getopt() in <unistd.h>.  Note we provide our own
 * getopt() with extensions.
 */
extern int alt_getopt(int, char * const *, const char *);
extern int alt_optopt;
extern int alt_optind;
extern int alt_opterr;
extern char *alt_optarg;

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_util_getopt_h__ */
