/*
 * crctable.c
 *
 * Generate POSIX CRC 32-bit Table.
 *
 * Copyright 1994, 2004 by Anthony Howe.  All rights reserved. No warranty.
 */

#include <com/snert/lib/version.h>

/*@ -exitarg +charint @*/

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if HAVE_UNISTD_H
# include <unistd.h>
#else
# if __BORLANDC__ && HAVE_IO_H
#  include <io.h>
# endif
#endif

#include <com/snert/lib/crc/Crc.h>
#include <com/snert/lib/util/getopt.h>

#define MSB		(~(~(unsigned long) 0 >> 1))

/*@ -formatconst @*/
static const char usage_msg[] = "\
usage:crctable [-cpst][-o file]\n\
\tGenerate a CRC table.\n\
-c\tCRC-CCITT 16-bit.\n\
-p\tCRC-32 (POSIX, default).\n\
-s\tCRC-16.\n\
-t\tCRC-12.\n\
-o\tDirect output to file.\n\
-\tFile name for standard output.\n\
";

int
main(int argc, char **argv)
{
	FILE *fp;
	int ch, bit;
	char *outfile;
	unsigned *coeff;
	unsigned long byte, crc, poly, count, mask;
	/* Length of array, n bits, coefficients. */
	static unsigned coeff_12[] = {
		8, 12,
		12, 11, 3, 2, 1, 0
	};
	static unsigned coeff_16[] = {
		6, 16,
		16, 15, 2, 0
	};
	static unsigned coeff_ccitt[] = {
		6, 16,
		16, 12, 5, 0
	};
	static unsigned coeff_32[] = {
		17, 32,
		32, 26, 23, 22, 16, 12, 11, 10, 8, 7, 5, 4, 2, 1, 0
	};

	outfile = "-";
	coeff = coeff_32;

	/*@ -branchstate @*/
	while ((ch = getopt(argc, argv, "cpsto:")) != -1) {
		switch (ch) {
		case 'c':
			coeff = coeff_ccitt;
			break;
		case 'p':
			coeff = coeff_32;
			break;
		case 's':
			coeff = coeff_16;
			break;
		case 't':
			coeff = coeff_12;
			break;
		case 'o':
			outfile = optarg;
			break;
		default:
			(void) fprintf(stderr, usage_msg);
			exit(2);
		}
	}

	if (outfile[0] == '-' && outfile[1] == '\0') {
		outfile = "(standard output)";
		fp = stdout;
	} else if ((fp = fopen(outfile, "w")) == (FILE *) 0) {
		(void) fprintf(stderr, "File \"%s\": %s", outfile, strerror(errno));
		exit(1);
	}

	for (poly = 0, count = 2; count < (unsigned) *coeff; ++count)
		poly |= 1L << coeff[count];

	for (mask = count = 0; count < (unsigned) coeff[1]; ++count)
		mask |= 1L << count;

	for (count = 0; count <= UCHAR_MAX; ++count) {
		crc = 0;
		byte = count << ((sizeof count - 1) * CHAR_BIT);

		for (bit = CHAR_BIT; 0 < bit--; ) {
			if ((byte ^ crc) & MSB)
				crc = (crc << 1) ^ poly;
			else
				crc <<= 1;
			byte <<= 1;
		}

		(void) fprintf(
			fp, (count-1) % 5 ? "0x%08lxL, " : "\n\t0x%08lxL, ",
			crc & mask
		);
	}
	(void) fputc('\n', fp);

	if (fclose(fp) < 0) {
		(void) fprintf(stderr, "File \"%s\": %s", outfile, strerror(errno));
		exit(1);
	}

	return 0;
}
