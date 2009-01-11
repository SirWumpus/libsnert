/*
 * crc32.c
 *
 * Copyright 1994, 2004 by Anthony Howe.  All rights reserved. No warranty.
 */

#include <com/snert/lib/crc/Crc.h>

/*@ +ignoresigns @*/
static unsigned long _crc_32_table[] = {
#include "crc32.tbl"
};

/*
 * Return an updated POSIX 32-bit CRC value given a current CRC and a byte.
 */
unsigned long
crc32(unsigned long curr, unsigned byte)
{
	return crcfn(_crc_32_table, 0xffffffffL, curr, byte);
}

/*
 * Use the POSIX 32-bit CRC to compute a hash for a byte string.
 */
unsigned long
hash32(const unsigned char *buf, int len)
{
	return hashfn(_crc_32_table, 0xffffffffL, buf, len);
}
