/*
 * crc16.c
 *
 * Copyright 1994, 2004 by Anthony Howe.  All rights reserved. No warranty.
 */

#include <com/snert/lib/crc/Crc.h>

/*@ +ignoresigns @*/
static unsigned long _crc_16_table[] = {
#include "crc16.tbl"
};

/*
 * Return an updated CRC-16 value given a current CRC and a byte.
 */
unsigned long
crc16(unsigned long curr, unsigned byte)
{
	return crcfn(_crc_16_table, 0xffffL, curr, byte);
}

/*
 * Use the CRC-16 to compute a hash for a byte string.
 */
unsigned long
hash16(const unsigned char *buf, int len)
{
	return hashfn(_crc_16_table, 0xffffL, buf, len);
}
