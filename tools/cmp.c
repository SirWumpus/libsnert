/*
 * cmp.c
 *
 * Copyright 1991, 2003 by Anthony Howe.  All rights reserved.
 */

#include <com/snert/lib/version.h>

#include <stdio.h>
#include <stdlib.h>

#include <com/snert/lib/io/posix.h>
#include <com/snert/lib/io/Error.h>
#include <com/snert/lib/util/getopt.h>

#ifndef BIG_BUFSIZ
#define BIG_BUFSIZ		(25 * 1024)
#endif

char m_eof[] = "EOF on %s ";
char file[] = "File \"%s\" ";
char differ[] = "%s %s differ: char %lu, line %lu\n";

char a_decimal[] = "%lu";
char a_octal[] = "%#lo";
char a_hex[] = "%#lx";

char b_decimal[] = " %d %d\n";
char b_octal[] = " %#o %#o\n";
char l_octal[] = " %o %o\n";
char b_hex[] = " %#x %#x\n";


char usage_msg[] =
"\033[1musage: cmp [-a d|o|x][-b d|o|x][-n count][-l|-s] file1 file2\033[0m\n"
"\n"
"-a\tThe radix for the byte number.\n"
"-b\tThe radix for the differing bytes.\n"
"-n\tWrite the first count differering bytes; 0 for all.\n"
"-l\tWrite the byte number and the differing bytes.\n"
"-s\tWrite nothing for differing files; return exit status only.\n"
"-\tFilename for standard input.\n"
"\n"
"\033[1mcmp/1.0 Copyright 1991, 2003 by Anthony Howe. All rights reserved.\033[0m\n"
;

long n_count;
int lflag, sflag;
char *a_radix, *b_radix;

int
main(argc, argv)
int argc;
char **argv;
{
	FILE *fp1, *fp2;
	int byte1, byte2, code;
	unsigned long count, line;

	ErrorSetProgramName("cmp");

	lflag = sflag = 0;
	a_radix = a_decimal;
	b_radix = b_octal;
	n_count = 1;

	while ((code = getopt(argc, argv, "a:b:n:ls")) != -1) {
		switch (code) {
		case 'a':
			lflag = 1;
			switch (*optarg) {
			case 'd':
				a_radix = a_decimal;
				break;
			case 'o':
				a_radix = a_octal;
				break;
			case 'x':
				a_radix = a_hex;
				break;
			default:
				UsagePrintLine(usage_msg);
			}
			break;
		case 'b':
			lflag = 1;
			switch (*optarg) {
			case 'd':
				b_radix = b_decimal;
				break;
			case 'o':
				b_radix = b_octal;
				break;
			case 'x':
				b_radix = b_hex;
				break;
			default:
				UsagePrintLine(usage_msg);
			}
			break;
		case 'n':
			n_count = strtol(optarg, 0, 0);
			break;
		case 'l':
			a_radix = a_decimal;
			b_radix = l_octal;
			lflag = 1;
			break;
		case 's':
			sflag = 1;
			break;
		default:
			UsagePrintLine(usage_msg);
		}
	}

	if ((lflag && sflag) || optind + 2 != argc)
		UsagePrintLine(usage_msg);

	if (argv[optind][0] == '-' && argv[optind][1] == '\0') {
		argv[optind] = "(standard input)";
		fp1 = stdin;
	} else if ((fp1 = fopen(argv[optind], "rb")) == (FILE *) 0) {
		FatalPrintLine(0, 0, file, argv[optind]);
	}

	if (argv[optind+1][0] == '-' && argv[optind+1][1] == '\0') {
		argv[optind+1] = "(standard input)";
		fp2 = stdin;
	} else if ((fp2 = fopen(argv[optind+1], "rb")) == (FILE *) 0) {
		FatalPrintLine(0, 0, file, argv[optind+1]);
	}

	if (fp1 == fp2)
		FatalPrintLine(0, 0, "Both files refer to standard input.");

	(void) setvbuf(fp1, (char *) 0, _IOFBF, BIG_BUFSIZ);
	(void) setvbuf(fp2, (char *) 0, _IOFBF, BIG_BUFSIZ);

	code = 0;
	count = line = 1;

	for (;;) {
		byte1 = fgetc(fp1);
		byte2 = fgetc(fp2);

		if (byte1 == EOF || byte2 == EOF) {
			if (ferror(fp1))
				FatalPrintLine(0, 0, file, argv[optind]);

			if (ferror(fp2))
				FatalPrintLine(0, 0, file, argv[optind+1]);

			if (feof(fp1) && !feof(fp2))
				FatalPrintLine(0, 0, m_eof, argv[optind]);

			if (!feof(fp1) && feof(fp2))
				FatalPrintLine(0, 0, m_eof, argv[optind+1]);
			break;
		}

        	code = (byte1 != byte2);
		if (code) {
			if (sflag)
				break;

			if (lflag) {
				(void) printf(a_radix, count);
				(void) printf(b_radix, byte1, byte2);
			} else {
				(void) printf(
					differ, argv[optind],
					argv[optind+1], count, line
				);
			}

			if (0 < n_count && --n_count == 0)
				break;
		}

		if (byte1 == '\n')
			++line;

		++count;
	}

	(void) fclose(fp1);
	(void) fclose(fp2);

	return code;
}

