/*
 * crcfn.c
 *
 * Copyright 1994, 2004 by Anthony Howe.  All rights reserved. No warranty.
 */

#include <limits.h>
#include <com/snert/lib/crc/Crc.h>

/*@ -shiftnegative  @*/
#define SHL_BYTE(var)	(var << CHAR_BIT)
#define SHR_BYTE(var)	(var >> ((sizeof var - 1) * CHAR_BIT))

/*
 * Compute a CRC with a given table of 256 values,
 * the previous CRC, and the input byte.
 */
unsigned long
crcfn(unsigned long *table, unsigned long mask, unsigned long curr, unsigned byte)
{
	return (SHL_BYTE(curr) ^ table[SHR_BYTE(curr) ^ byte]) & mask;
}

/*
 * Use a given CRC table of 256 values to compute a hash for a byte string.
 */
unsigned long
hashfn(unsigned long *table, unsigned long mask, const unsigned char *buf, int len)
{
	unsigned long hash = 0;

	if (len < 0) {
		/*@ +ignoresigns @*/
		while (*buf != '\0')
			hash = crcfn(table, mask, hash, (unsigned) *buf++);
	} else {
		while (0 < len--)
			hash = crcfn(table, mask, hash, (unsigned) *buf++);
	}

	return hash;
}

