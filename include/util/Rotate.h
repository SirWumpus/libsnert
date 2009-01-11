/*
 * Rotate.c
 *
 * Copyright 1994, 2004 by Anthony Howe.  All rights reserved.
 */

#ifndef __com_snert_lib_util_Rotate_h__
#define __com_snert_lib_util_Rotate_h__	1

#define BITS_PER_BYTE		8

#ifdef __cplusplus
extern "C" {
#endif

extern int shl(unsigned char *buf, long len, long count);
extern int rol(unsigned char *buf, long len, long count);
extern int shr(unsigned char *buf, long len, long count);
extern int ror(unsigned char *buf, long len, long count);

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_util_Rotate_h__ */
