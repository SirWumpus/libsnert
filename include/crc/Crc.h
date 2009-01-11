/*
 * crc.h
 *
 * Copyright 1991, 2004 by Anthony Howe.  All rights reserved.
 */

#ifndef __com_snert_lib_crc_Crc_h__
#define __com_snert_lib_crc_Crc_h__	1

#ifdef __cplusplus
extern "C" {
#endif

extern unsigned long crcfn(unsigned long *, unsigned long, unsigned long, unsigned);
extern unsigned long hashfn(unsigned long *, unsigned long, const unsigned char *, int);

extern unsigned long crc32(unsigned long, unsigned);
extern unsigned long hash32(const unsigned char *, int);
extern unsigned long crc16(unsigned long, unsigned);
extern unsigned long hash16(const unsigned char *, int);
extern unsigned long crc12(unsigned long, unsigned);
extern unsigned long hash12(const unsigned char *, int);
extern unsigned long crcccitt(unsigned long, unsigned);
extern unsigned long hashccitt(const unsigned char *, int);

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_crc_Crc_h__ */
