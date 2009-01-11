/*
 * Luhn.h
 *
 * Copyright 2001, 2003 by Anthony Howe.  All rights reserved.
 */

#ifndef __com_snert_lib_crc_Luhn_h__
#define __com_snert_lib_crc_Luhn_h__	1

#ifdef __cplusplus
extern "C" {
#endif

extern int LuhnIsValid(const char *str);
extern int LuhnGenerate(const char *str);

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_crc_Luhn_h__ */
