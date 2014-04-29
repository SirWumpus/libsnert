/*
 * bs.h
 *
 * Binary String
 *
 * Copyright 2005, 2006 by Anthony Howe.  All rights reserved.
 */

#ifndef __com_snert_lib_util_bs_h__
#define __com_snert_lib_util_bs_h__	1

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

extern long bsLength(unsigned char *bs);
extern long bsPrint(FILE *fp, unsigned char *bs);
extern unsigned char *bsGetBytes(unsigned char *bs);

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_util_bs_h__ */
