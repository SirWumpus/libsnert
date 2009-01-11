/*
 * crc12.c
 *
 * Copyright 1994, 2004 by Anthony Howe.  All rights reserved. No warranty.
 */

#include <com/snert/lib/crc/Crc.h>

/*@ +ignoresigns @*/
static unsigned long _crc_12_table[] = {
#include "crc12.tbl"
};

/*
 * Return an updated CRC-12 value given a current CRC and a byte.
 */
unsigned long
crc12(unsigned long curr, unsigned byte)
{
	return crcfn(_crc_12_table, 0xfffL, curr, byte);
}

/*
 * Use the CRC-12 to compute a hash for a byte string.
 */
unsigned long
hash12(const unsigned char *buf, int len)
{
	return hashfn(_crc_12_table, 0xfffL, buf, len);
}
