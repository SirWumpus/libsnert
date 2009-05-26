/*
 * time62.h
 *
 * Copyright 2006, 2008 by Anthony Howe.  All rights reserved.
 */

#ifndef __com_snert_lib_util_time62_h__
#define __com_snert_lib_util_time62_h__	1

#ifdef __cplusplus
extern "C" {
#endif

#if defined(TIME_WITH_SYS_TIME)
# include <sys/time.h>
# include <time.h>
#else
# if defined(HAVE_SYS_TIME_H)
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#define TIME62_BUFFER_SIZE	6

extern const char base62[];

/**
 * @param when
 *
 * @param buffer
 *
 */
extern void time62Encode(time_t when, char buffer[TIME62_BUFFER_SIZE]);

/**
 * @param time_encoding
 *
 * @return
 *
 */
extern time_t time62Decode(const char time_encoding[TIME62_BUFFER_SIZE]);

extern void time62EncodeTime(const struct tm *local, char buffer[TIME62_BUFFER_SIZE]);
extern int time62DecodeTime(const char time_encoding[TIME62_BUFFER_SIZE], struct tm *decoded);

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_util_time62_h__ */
