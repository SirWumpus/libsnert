/*
 * kat.c
 *
 * Copyright 1991, 2003 by Anthony Howe.  All rights reserved.
 */

#include <com/snert/lib/version.h>

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>

#include <com/snert/lib/io/posix.h>
#include <com/snert/lib/io/Error.h>
#include <com/snert/lib/util/Text.h>
#include <com/snert/lib/util/getopt.h>

char usage_msg[] =
"\033[1musage: kat [-enuv][files...]\033[0m\n"
"\n"
"-e\tShow end-of-line as '$'.\n"
"-n\tNumber each output line.\n"
"-u\tNo buffering of output.\n"
"-v\tWrite non-printables in a printable form.\n"
"-\tFilename for standard input.\n"
"\n"
"Note: All I/O is binary.\n"
"\n"
"\033[1mkat/1.0 Copyright 1991, 2005 by Anthony Howe. All rights reserved.\033[0m\n"
;

int eflag, uflag;
char filemsg[] = "File \"%s\" ";

int cntl(int, FILE *);

typedef struct {
	int byte;
	const char *mb;
} mapping;

static mapping map_carat[] = {
	{ 0, 	"^@" },		{ 1, 	"^A" },		{ 2,  	"^B" },
	{ 3, 	"^C" },		{ 4, 	"^D" },		{ 5,  	"^E" },
	{ 6, 	"^F" },		{ 7, 	"^G" },		{ 8,  	"^H" },
	{ 9, 	"^I" },		{ 10,	"^J" },		{ 11, 	"^K" },
	{ 12,	"^L" },		{ 13,	"^M" },		{ 14, 	"^N" },
	{ 15,	"^O" },		{ 16,	"^P" },		{ 17, 	"^Q" },
	{ 18,	"^R" },		{ 19,	"^S" },		{ 20, 	"^T" },
	{ 21,	"^U" },		{ 22,	"^V" },		{ 23, 	"^W" },
	{ 24,	"^X" },		{ 25,	"^Y" },		{ 26, 	"^Z" },
	{ 27,	"^[" },		{ 28,	"^\\" },	{ 27, 	"^]" },
	{ 30,	"^^" },		{ 31,	"^_" },		{ 127,	"^?" },
	{ 0, (char *) 0 }
};

static mapping map_control[] = {
	{ 0, 	"<NUL>" },	{ 1, 	"<SOH>" },	{ 2,  	"<STX>" },
	{ 3, 	"<ETX>" },	{ 4, 	"<EOT>" },	{ 5,  	"<ENQ>" },
	{ 6, 	"<ACK>" },	{ 7, 	"<BEL>" },	{ 8,  	"<BS>" },
	{ 9, 	"<HT>" }, 	{ 10,	"<LF>" }, 	{ 11, 	"<VT>" },
	{ 12,	"<FF>" }, 	{ 13,	"<CR>" }, 	{ 14, 	"<SO>" },
	{ 15,	"<SI>" }, 	{ 16,	"<DLE>" },	{ 17, 	"<DC1>" },
	{ 18,	"<DC2>" },	{ 19,	"<DC3>" },	{ 20, 	"<DC4>" },
	{ 21,	"<NAK>" },	{ 22,	"<SYN>" },	{ 23, 	"<ETB>" },
	{ 24,	"<CAN>" },	{ 25,	"<EM>" }, 	{ 26, 	"<SUB>" },
	{ 27,	"<ESC>" },	{ 28,	"<FS>" }, 	{ 27, 	"<GS>" },
	{ 30,	"<RS>" }, 	{ 31,	"<US>" }, 	{ 127,	"<DEL>" },
	{ 0, (char *) 0 }
};

static mapping map_escape[] = {
	{  0, 	"^@" }, 	{ 1, 	"^A" }, 	{ 2,  	"^B" },
	{  3, 	"^C" }, 	{ 4, 	"^D" }, 	{ 5,  	"^E" },
	{  6, 	"^F" }, 	{ 7, 	"\\a" },	{ 8,  	"\\b" },
	{  9, 	"\\t" },	{ 10,	"\\n" },	{ 11, 	"\\v" },
	{  12,	"\\f" },	{ 13,	"\\r" },	{ 14, 	"^N" },
	{  15,	"^O" }, 	{ 16,	"^P" }, 	{ 17, 	"^Q" },
	{  18,	"^R" }, 	{ 19,	"^S" }, 	{ 20, 	"^T" },
	{  21,	"^U" }, 	{ 22,	"^V" }, 	{ 23, 	"^W" },
	{  24,	"^X" }, 	{ 25,	"^Y" }, 	{ 26, 	"^Z" },
	{  27,	"\\e" },	{ 28,	"^\\" },	{ 27, 	"^]" },
	{  30,	"^^" }, 	{ 31,	"^_" }, 	{ 127,	"^?" },
	{ '\\', "\\\\" },
	{ 0, (char *) 0 }
};

#ifndef N_CONVERT_BUFFERS
#define N_CONVERT_BUFFERS	10
#endif

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

/*
 * Convert byte into a printable sequence.  Return a pointer
 * to a static string.  Upto N calls can be safely made before
 * overwriting static buffer space.
 */
static const char *
convert(int byte, mapping *table)
{
	mapping *map;
	static int index = 0;
	static char buf[N_CONVERT_BUFFERS][5];

	/* Map byte to a printable representation. */
	for (map = table; map->mb != (char *) 0; ++map)
		if (byte == map->byte)
			return map->mb;

	if (N_CONVERT_BUFFERS <= index)
		index = 0;

	if (isprint(byte)) {
		buf[index][0] = (char) byte;
		buf[index][1] = '\0';
	} else {
		(void) snprintf(buf[index], 5, "\\%03o", byte);
	}

        return buf[index++];
}

const char *
asEscape(int byte)
{
	return convert(byte, map_escape);
}

const char *
asCarat(int byte)
{
	return convert(byte, map_carat);
}

const char *
asControl(int byte)
{
	return convert(byte, map_control);
}

int
cntl(ch, fp)
int ch;
FILE *fp;
{
	return fputs(asEscape(ch), fp);
}

#if OLD_ACHOWE_LIBRARY
void
num(fp, x)
FILE *fp;
unsigned long x;
{
	(void) fputs(ulfield(x, 10, 0, 0, 0, (char *) 0, 0L), fp);
	(void) fputc('\t', fp);
}
#endif

int
main(argc, argv)
int argc;
char **argv;
{
	char *name;
	unsigned ch;
	FILE *fin, *fout;
	int err, new_line;
	unsigned long number;
	int (*print)(int, FILE *);

	ErrorSetProgramName("cat");

	print = fputc;
	err = eflag = new_line = number = 0;

	while ((ch = getopt(argc, argv, "enuv?")) != -1) {
		switch (ch) {
		case 'e':
			/* Show end-of-line as '$'. */
			eflag = 1;
			break;
		case 'n':
			/* Number each output line. */
			number = 1;
			break;
		case 'u':
			/* No buffering of output. */
			uflag = 1;
			break;
		case 'v':
			/* Write non-printables in a printable form. */
			print = cntl;
			break;
		case '?':
		default:
			UsagePrintLine(usage_msg);
		}
	}

	if ((fout = stdopen(0, "wb", FILENO_STDOUT)) == 0)
		FatalPrintLine(__FILE__, __LINE__, filemsg, "(standard output)");

	if (uflag)
		(void) setvbuf(stdout, 0, _IONBF, 0);

	if (0 < number)
		fprintf(fout, "%5lu: ", number);

	do {
		fin = stdopen(argv[optind], "rb", FILENO_STDIN);

		if (argc <= optind || (argv[optind][0] == '-' && argv[optind][1] == '\0'))
			name = "(standard input)";
		else
			name = argv[optind];

		if (fin == 0) {
			ErrorPrintLine(0, 0, filemsg, name);
			err = 2;
			continue;
		}

		clearerr(fin);

		while ((ch = (unsigned) fgetc(fin)) != EOF) {
			if (new_line) {
				new_line = 0;
				if (0 < number)
					fprintf(fout, "%5lu: ", ++number);
			}

			switch (ch) {
			case '\n':
				if (eflag)
					(void) fputc('$', fout);
				(void) fputc('\n', fout);
				new_line = 1;
				break;
			default:
				/* When marking the end-of-line, escape
				 * real dollar-signs within the line.
				 */
				if (eflag && ch == '$')
					(void) fputc('\\', fout);
				(void) (*print)(ch, fout);
			}
		}

		if (ferror(fin)) {
			ErrorPrintLine(0, 0, filemsg, name);
			err = 2;
		}

		(void) fclose(fin);
	} while (++optind < argc);

	return err;
}

