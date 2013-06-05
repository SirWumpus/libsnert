/*
 * comm.c
 *
 * Copyright 1991, 2003 by Anthony Howe.  All rights reserved.
 */

#include <stdio.h>
#include <string.h>

#include <com/snert/lib/version.h>
#include <com/snert/lib/io/posix.h>
#include <com/snert/lib/io/Error.h>
#include <com/snert/lib/util/getopt.h>

char file[] = "File \"%s\" ";

char usage_msg[] =
"\033[1musage: comm [-123] file1 file2\033[0m\n"
"\n"
"-1\tSuppress lines unique to file1.\n"
"-2\tSuppress lines unique to file2.\n"
"-3\tSuppress lines which appear in both files.\n"
"-\tFilename for standard input.\n"
"\n"
"\033[1mcomm/1.0 Copyright 1991, 2003 by Anthony Howe. All rights reserved.\033[0m\n"
;

#define FILE_1			1
#define FILE_2			2

#define COLUMN_1		1
#define COLUMN_2		2
#define COLUMN_3		4

int selector, fetch, eof_1_2;
char line1[BUFSIZ+1];
char line2[BUFSIZ+1];
char *lead[] = { "", "\t", "\t", "\t\t" };

int
main(argc, argv)
int argc;
char **argv;
{
	int i;
	FILE *fp1, *fp2;

	ErrorSetProgramName("comm");

	selector = COLUMN_1 | COLUMN_2 | COLUMN_3;

	while ((i = getopt(argc, argv, "123")) != -1) {
		switch (i) {
		case '1':
			selector &= ~COLUMN_1;
			break;
		case '2':
			selector &= ~COLUMN_2;
			break;
		case '3':
			selector &= ~COLUMN_3;
			break;
		default:
			UsagePrintLine(usage_msg);
		}
	}

	if (optind + 2 != argc)
		UsagePrintLine(usage_msg);

	if (argv[optind][0] == '-' && argv[optind][1] == '\0') {
		argv[optind] = "(standard input)";
		fp1 = stdin;
	} else if ((fp1 = fopen(argv[optind], "r")) == (FILE *) 0) {
		FatalPrintLine(0, 0, file, argv[optind]);
	}

	if (argv[optind+1][0] == '-' && argv[optind+1][1] == '\0') {
		argv[optind+1] = "(standard input)";
		fp2 = stdin;
	} else if ((fp2 = fopen(argv[optind+1], "r")) == (FILE *) 0) {
		FatalPrintLine(0, 0, file, argv[optind+1]);
	}

	if (fp1 == fp2)
		FatalPrintLine(0, 0, "Both files refer to standard input.");

	for (fetch = FILE_1 | FILE_2, eof_1_2 = 0; ; ) {
		i = 0;
		if (fetch & FILE_1) {
			(void) fgets(line1, sizeof line1, fp1);
			if (ferror(fp1))
				FatalPrintLine(0, 0, file, argv[optind]);
			if (feof(fp1)) {
				eof_1_2 |= FILE_1;
				i = 1;
			}
		}
		if (fetch & FILE_2) {
			(void) fgets(line2, sizeof line2, fp2);
			if (ferror(fp2))
				FatalPrintLine(0, 0, file, argv[optind+1]);
			if (feof(fp2)) {
				eof_1_2 |= FILE_2;
				i = -1;
			}
		}

		if (eof_1_2 == (FILE_1 | FILE_2))
			break;
		else if (eof_1_2 == 0)
			i = strncmp(line1, line2, sizeof line1);

		if (i == 0) {
			if (selector & COLUMN_3) {
				(void) fputs(
					lead[selector & ~COLUMN_3], stdout
				);
				(void) fputs(line1, stdout);
			}
			fetch = FILE_1 | FILE_2;
		} else if (0 < i) {
			if (selector & COLUMN_2) {
				(void) fputs(
					lead[selector & COLUMN_1], stdout
				);
				(void) fputs(line2, stdout);
			}
			fetch = FILE_2;
		} else {
			if (selector & COLUMN_1)
				(void) fputs(line1, stdout);
			fetch = FILE_1;
		}
	}

	(void) fclose(fp1);
	(void) fclose(fp2);

	return 0;
}

