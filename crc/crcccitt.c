/*
 * crcccitt.c
 *
 * Copyright 1994, 2004 by Anthony Howe.  All rights reserved. No warranty.
 */

#include <com/snert/lib/crc/Crc.h>

/*@ +ignoresigns @*/
static unsigned long _crc_ccitt_table[] = {
#include "crcccitt.tbl"
};

/*
 * Return an updated CRC-ccitt value given a current CRC and a byte.
 */
unsigned long
crcccitt(unsigned long curr, unsigned byte)
{
	return crcfn(_crc_ccitt_table, 0xffffL, curr, byte);
}

/*
 * Use the CRC-ccitt to compute a hash for a byte string.
 */
unsigned long
hashccitt(const unsigned char *buf, int len)
{
	return hashfn(_crc_ccitt_table, 0xffffL, buf, len);
}
