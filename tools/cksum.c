/*
 * cksum.c
 *
 * Copyright 1991, 2003 by Anthony Howe.  All rights reserved.
 */

#include <com/snert/lib/version.h>

#include <stdio.h>
#include <string.h>
#include <limits.h>

#ifdef HAVE_IO_H
# include <io.h>
#endif

#include <com/snert/lib/crc/Crc.h>
#include <com/snert/lib/io/posix.h>
#include <com/snert/lib/io/Error.h>
#include <com/snert/lib/util/getopt.h>

char filemsg[] = "File \"%s\" ";

char usage_msg[] =
"\033[1musage: cksum [-c 16|32|ccitt|bsd|sysv] files...\033[0m\n"
"\n"
"-c\tCRC algorithum to use (default CRC-32)\n"
"-\tFilename for standard input.\n"
"\n"
"\033[1mcksum/1.0 Copyright 1991, 2005 by Anthony Howe. All rights reserved.\033[0m\n"
;

unsigned long crc, count;

FILE *
stdopen(const char *file, const char *mode, int stdio)
{
	if (file == NULL || (file[0] == '-' && file[1] == '\0')) {
		switch (stdio) {
		case 0:
#if defined(__BORLANDC__) || defined(__CYGWIN__)
			setmode(0, O_BINARY);
#endif
			return stdin;
		case 1:
#if defined(__BORLANDC__) || defined(__CYGWIN__)
			setmode(1, O_BINARY);
#endif
			return stdout;
		}
		return NULL;
	}

	return fopen(file, mode);
}

int
crc_file(FILE *fin, unsigned long (*func)(unsigned long, unsigned))
{
	int ch;
	unsigned long number;

	while ((ch = fgetc(fin)) != EOF) {
		crc = (*func)(crc, ch);
		++count;
	}

	if (ferror(fin))
		return -1;

	for (number = count; number != 0; number >>= CHAR_BIT)
		crc = (*func)(crc, (unsigned) (number & UCHAR_MAX));

	crc = ~crc;

	if (func != crc32)
		crc &= 0xffff;

	return 0;
}

/*** UNTESTED ***/
int
crc_16(FILE *fin)
{
	return crc_file(fin, crc16);
}

int
crc_32(FILE *fin)
{
	return crc_file(fin, crc32);
}

/*** UNTESTED ***/
int
crc_ccitt(FILE *fin)
{
	return crc_file(fin, crcccitt);
}

/*
 *
 */
int
crc_bsd(FILE *fin)
{
	int ch;
	unsigned bsd = 0;

	while ((ch = fgetc(fin)) != EOF) {
		if (bsd & 1)
			bsd = 0x8000 | (bsd >> 1);
		else
			bsd >>= 1;
		bsd += (unsigned char) ch;
		++count;
	}

	if (ferror(fin))
		return -1;

	crc = bsd;

	return 0;
}

/*
 * System V
 *
 * s = sum of all bytes
 * r = s % 2^16 + (s % 2^32) / 2^16
 * cksum = (r % 2^16) + r / 2^16
 */
int
crc_sysv(FILE *fin)
{
	int ch;
	unsigned long r;

	while ((ch = fgetc(fin)) != EOF) {
		crc += ch;
		++count;
	}

	if (ferror(fin))
		return -1;

	r = (crc & 0xffff) + (crc >> 16);
	crc = (r & 0xffff) + (r >> 16);

	return 0;
}

typedef struct {
	const char *name;
	int (*method)(FILE *fp);
} lookup;

lookup table[] = {
	{ "16", crc_16 },
	{ "32", crc_32 },
	{ "ccitt", crc_ccitt },
	{ "bsd", crc_bsd },
	{ "sysv", crc_sysv },
	{ (const char *) 0, (void *) 0 }
};

int
main(int argc, char **argv)
{
	lookup *t;
	FILE *fin;
	int c, err;
	int (*method)(FILE *);

	ErrorSetProgramName("cksum");

	method = crc_32;

	while ((c = getopt(argc, argv, "c:")) != -1) {
		switch (c) {
		case 'c':
			for (t = table; t->name != (const char *) 0; ++t) {
				if (strcmp(t->name, optarg) == 0) {
					method = t->method;
	                		break;
				}
			}
			if (t->name != (const char *) 0)
				break;
			/*@fallthrough@*/
		default:
			UsagePrintLine(usage_msg);
		}
	}

	if (argc <= optind)
		UsagePrintLine(usage_msg);

	for (err = 0; optind < argc; ++optind) {
		fin = stdopen(argv[optind], "rb", FILENO_STDIN);
		if (fin == (FILE *) 0) {
			err = 2;
			continue;
		}

		clearerr(fin);
		crc = count = 0;

		if ((*method)(fin) != 0) {
			ErrorPrintLine(0, 0, filemsg, argv[optind]);
			err = 2;
		}

		(void) printf("%lu %lu %s\n", crc, count, argv[optind]);
		(void) fclose(fin);
	}

	return err;
}

