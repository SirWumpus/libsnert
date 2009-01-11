/*
 * MIT Hackmem Count
 *
 * MIT Hackmem Count is funky. Consider a 3 bit number as being 4a+2b+c.
 * If we shift it right 1 bit, we have 2a+b. Subtracting this from the
 * original gives 2a+b+c. If we right-shift the original 3-bit number
 * by two bits, we get a, and so with another subtraction we have a+b+c,
 * which is the number of bits in the original number.
 *
 * How is this insight employed? The first assignment statement in the
 * routine computes tmp. Consider the octal representation of tmp. Each
 * digit in the octal representation is simply the number of 1's in the
 * corresponding three bit positions in n. The last return statement
 * sums these octal digits to produce the final answer.
 *
 * The key idea is to add adjacent pairs of octal digits together and
 * then compute the remainder modulus 63. This is accomplished by right-
 * shifting tmp by three bits, adding it to tmp itself and ANDing with
 * a suitable mask. This yields a number in which groups of six adjacent
 * bits (starting from the LSB) contain the number of 1's among those
 * six positions in n. This number modulo 63 yields the final answer.
 * For 64-bit numbers, we would have to add triples of octal digits and
 * use modulus 1023.
 *
 * This is HACKMEM 169, as used in X11 sources. Source: MIT AI Lab memo,
 * late 1970's.
 */

int
bitcount32(unsigned long n)
{
	/* works for 32-bit numbers only    */
	/* fix last line for 64-bit numbers */

	register unsigned int tmp;

	tmp = n - ((n >> 1) & 033333333333)
	        - ((n >> 2) & 011111111111);
	return ((tmp + (tmp >> 3)) & 030707070707) % 63;
}

/*
 * Remove right most bit:		n & (n-1)
 *
 * Isolate the bottom most bit:		n & -n
 *
 * Quick test for power of two number:
 *
 *	((x & -x) == x)
 *
 * returns true if x is a power of 2 (or equal to 0)
 */