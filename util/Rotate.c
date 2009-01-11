/*
 * Rotate.c
 *
 * Copyright 1994, 2004 by Anthony Howe.  All rights reserved.
 */

#include <limits.h>

#include <com/snert/lib/util/Rotate.h>

/*
 * Shift byte buffer (in place) left count bits.
 * Return the carry bit.
 */
int
shl(unsigned char *buf, long len, long count)
{
	register unsigned shifter;
	register unsigned char *p;

	if (len <= 0 || count <= 0)
		return 0;

	p = buf;
	do {
		shifter = 0;
		p += len;

		do {
			shifter = (shifter >> BITS_PER_BYTE) | (*p << 1);
			*p = (unsigned char) shifter;
		} while (buf != p);
	} while (0 < --count);

	/* Return carry bit. */
	return shifter >> BITS_PER_BYTE;

}

/*
 * Rotate byte buffer (in place) left count bits.
 * Return the carry bit.
 */
int
rol(unsigned char *buf, long len, long count)
{
	register unsigned shifter;
	register unsigned char *p;

	if (len <= 0 || count <= 0)
		return 0;

	p = buf + len - 1;
	do {
		shifter = 0;

		do {
			shifter = (shifter >> BITS_PER_BYTE) | (*p << 1);
			*p = (unsigned char) shifter;
		} while (buf <= --p);

		p += len;
		*p |= (unsigned char)(shifter >>= BITS_PER_BYTE);
	} while (0 < --count);

	/* Return carry bit. */
	return shifter;
}

/*
 * Shift byte buffer (in place) right count bits.
 * Return the carry bit.
 */
int
shr(unsigned char *buf, long len, long count)
{
	register unsigned shifter;
	register unsigned char *ebuf;

	if (len <= 0 || count <= 0)
		return 0;

	ebuf = buf + len;
	do {
		shifter = 0;

		do {
			/* Wrap the carry bit to the left side. */
			shifter <<= BITS_PER_BYTE + 1;
			/* Integrate the byte and shift. */
			shifter = (shifter | (*buf << 1)) >> 1;
			/* Update the byte in the buffer. */
			*buf = (unsigned char) (shifter >> 1);
		} while (++buf != ebuf);

		buf -= len;
	} while (0 < --count);

	/* Return carry bit. */
	return shifter & 1;
}

/*
 * Rotate byte buffer (in place) right count bits.
 * Return the carry bit.
 */
int
ror(unsigned char *buf, long len, long count)
{
	register unsigned shifter;
	register unsigned char *ebuf;

	if (len <= 0 || count <= 0)
		return 0;

	ebuf = buf + len;
	do {
		shifter = 0;

		do {
			/* Wrap the carry bit to the left side. */
			shifter <<= BITS_PER_BYTE + 1;
			/* Integrate the byte and shift. */
			shifter = (shifter | (*buf << 1)) >> 1;
			/* Update the byte in the buffer. */
			*buf = (unsigned char) (shifter >> 1);
		} while (++buf != ebuf);

		buf -= len;
		*buf |= (unsigned char) (shifter << BITS_PER_BYTE);
	} while (0 < --count);

	/* Return carry bit. */
	return shifter & 1;
}
