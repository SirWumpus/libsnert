/*
 * uue.c
 *
 * Copyright 1996, 1997 by Anthony Howe.  All rights reserved.
 */

#include <com/snert/lib/version.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_IO_H
# include <io.h>
#endif

#include <com/snert/lib/io/posix.h>
#include <com/snert/lib/io/Error.h>
#include <com/snert/lib/util/Text.h>
#include <com/snert/lib/util/getopt.h>

char filemsg[] = "File \"%s\" ";

char usage_msg[] =
"\033[1musage: uue [-b|h|u][-dD] file\033[0m\n"
"\n"
"-b\tBase64\n"
"-h\tBinHex 4.0\n"
"-u\tUnix to Unix (default)\n"
"-d\tDecode to file named by encoded file.\n"
"-D\tDecode to (binary) standard output.\n"
"-\tFilename for standard input.\n"
"\n"
"\033[1muue/1.1 Copyright 2000, 2005 by Anthony Howe. All rights reserved.\033[0m\n"
;

char line[BUFSIZ];

char header[] = "(This file must be converted with BinHex 4.0)\n";

/*
 * Mapping table of 6-bit values (0..64) to characters.
 */
char code2char40[] =
"!\"#$%&'()*+,- 012345689@ABCDEFGHIJKLMNPQRSTUVXYZ[`abcdefhijklmpqr:";

/*
 * Base64 mapping table of 6-bit values (0..63) to characters.
 * Padding/terminating character is '='.
 */
char code2char64[] =
"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=";

/*
 * Mapping of characters to 6-bit values.
 */
int char2code[256];

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

void
make_char2code(const char *map)
{
	int i;

	/* Set all entries in table to the ignore code. */
	for (i = 0; i < 256; ++i)
		char2code[i] = -2;

	/* Set each character index to its index value in code2char[]. */
	for (i = 0; *map != '\0'; ++map)
		char2code[(int) *map] = i++;
}

void
decode64(const char *input, int flag)
{
	FILE *in, *out;
	int i, ch, code[4];

	if ((in = stdopen(input, "r", FILENO_STDIN)) == NULL)
		exit(2);
	if ((out = stdopen("-", "wb", FILENO_STDOUT)) == NULL)
		exit(2);

	make_char2code(code2char64);

	for (i = 0; (ch = fgetc(in)) != EOF; ) {
		/* Convert character to codes. */
		if ((code[i] = char2code[ch]) < 0 || ++i < 4)
			continue;

		/* Decode 4 character block into 3 bytes. */
		if (63 < code[0])
			break;
		if (63 < code[1])
			FatalPrintLine(0, 0, "File \"%s\" : padding error", input);
		(void) fputc((code[0] << 2) | (code[1] >> 4), out);
		if (63 < code[2])
			break;
		(void) fputc((code[1] << 4) | (code[2] >> 2), out);
		if (63 < code[3])
			break;
		(void) fputc((code[2] << 6) | code[3], out);
		i = 0;
	}

	if (ferror(in))
		FatalPrintLine(0, 0, filemsg, input);

	(void) fclose(out);
	(void) fclose(in);
}

void
encode64(const char *input)
{
	FILE *in;
	int i, n, ch, code[72];

	if ((in = stdopen(input, "rb", FILENO_STDIN)) == NULL)
		exit(2);

	do {
		/* Read in enough characters for one line. */
		for (n = 0; n < 72; ) {
			if ((ch = fgetc(in)) == EOF)
				break;
			code[n++] = (ch >> 2) & 0x3f;
			code[n++] = (ch << 4) & 0x30;

			if ((ch = fgetc(in)) == EOF)
				break;
			code[n-1] |= (ch >> 4) & 0xf;
			code[n++] = (ch << 2) & 0x3c;

			if ((ch = fgetc(in)) == EOF)
				break;
			code[n-1] |= (ch >> 6) & 0x3;
			code[n++] = ch & 0x3f;
		}

		if (ferror(in))
			FatalPrintLine(0, 0, filemsg, input);

		for (i = 0; i < n; ++i)
			(void) fputc(code2char64[code[i]], stdout);

		if (72 <= n)
			(void) fputc('\n', stdout);
	} while (!feof(in));

	for ( ; n % 4 != 0; ++n)
		(void) fputc('=', stdout);
	(void) fputc('\n', stdout);

	(void) fclose(in);
}

void
expand(int byte, FILE *out)
{
	static int last;
	static int flag = 0;

	if (flag) {
		flag = 0;

		if (byte == 0)
			/* Repeat marker is a literal value. */
			(void) fputc(0x90, out);
		else
			/* Repeat last byte, n-1 times. */
			while (0 < --byte)
				(void) fputc(last, out);
	} else if (byte == 0x90) {
		flag = 1;
	} else {
		(void) fputc(byte, out);
		last = byte;
	}
}

void
decode40(const char *input, int flag)
{
	FILE *in, *out;
	int i, ch, code[4];

	if ((in = stdopen(input, "r", FILENO_STDIN)) == NULL)
		exit(2);

	/* Find header line marker. */
	while (fgets(line, sizeof line, in) != (char *) 0) {
		if (strcmp(header, line) == 0)
			break;
	}

	if (ferror(in))
		FatalPrintLine(0, 0, filemsg, input);
	if (feof(in))
		FatalPrintLine(0, 0, "File \"%s\" : unexpected EOF", input);

	if (fgetc(in) != ':')
		FatalPrintLine(0, 0, "File \"%s\" : encoding error", input);

	if ((out = stdopen("-", "wb", FILENO_STDOUT)) == NULL)
		exit(2);

	make_char2code(code2char40);

	for (i = 0; (ch = fgetc(in)) != EOF; ) {
		/* Convert character to codes. */
		if ((code[i] = char2code[ch]) < 0 || ++i < 4)
			continue;

		/* Decode 4 character block into 3 bytes. */
		if (63 < code[0])
			break;
		if (63 < code[1])
			FatalPrintLine(0, 0, "File \"%s\" : encoding error", input);
		expand((code[0] << 2) | (code[1] >> 4), out);
		if (63 < code[2])
			break;
		expand((code[1] << 4) | (code[2] >> 2), out);
		if (63 < code[3])
			break;
		expand((code[2] << 6) | code[3], out);
		i = 0;
	}

	if (ferror(in))
		FatalPrintLine(0, 0, filemsg, input);

	(void) fclose(out);
	(void) fclose(in);
}

void
encode40(const char *input)
{
}

/*
 * Convert the given input file from binary bytes into printable
 * characters and write the result to standard output.
 */
void
uuencode(const char *input)
{
	char *bp;
	FILE *in;
	int a, b, c, d, n;
	static char buf[45];

	if ((in = stdopen(input, "rb", FILENO_STDIN)) == NULL)
		exit(2);

	(void) fprintf(stdout, "begin 600 %s\n", input);

	while (0 < (n = fread(buf, sizeof *buf, sizeof buf, in))) {
		/* Pad out a short buffer to next multiple of 3. */
		for (a = n + n % 3; n <= --a; )
			buf[a] = '\0';

		/* Encode number of bytes being encoded. */
		(void) fputc(' ' + (n == 0 ? 64 : n), stdout);

		/* Encode buffer from bytes to printable characters. */
		for (bp = buf; 0 < n; n -= 3, bp += 3) {
			a = (bp[0] >> 2) & 0x3f;
			b = ((bp[0] << 4) & 0x30) | ((bp[1] >> 4) & 0xf);
			c = ((bp[1] << 2) & 0x3c) | ((bp[2] >> 6) & 0x3);
			d = bp[2] & 0x3f;

			(void) fputc(' ' + (a == 0 ? 64 : a), stdout);
			(void) fputc(' ' + (b == 0 ? 64 : b), stdout);
			(void) fputc(' ' + (c == 0 ? 64 : c), stdout);
			(void) fputc(' ' + (d == 0 ? 64 : d), stdout);
		}

		(void) fputc('\n', stdout);
	}

        if (ferror(in))
        	FatalPrintLine(0, 0, filemsg, input);

	/* Empty decode line and end marker. */
	(void) fprintf(stdout, "`\nend\n");

	(void) fclose(in);
}

/*
 * Convert the uuencoded input file from printable characters to
 * original binary data and write the result to the name specified
 * in the uuencoded input.
 */
void
uudecode(const char *input, int flag)
{
	int len;
	FILE *in, *out;
	unsigned char *lp;
	unsigned long line_no;
	static char output[128];

	if ((in = stdopen(input, "r", FILENO_STDIN)) == NULL)
		exit(2);

	line_no = 1;

	/* Find "begin" marker. */
	while (fscanf(in, "begin %*o %127[^\n]%*[\n]", output) != 1) {
		if (ferror(in))
			FatalPrintLine(0, 0, filemsg, input);
		if (feof(in))
			FatalPrintLine(0, 0, "File \"%s\" : \"begin\" not found", input);

		/* Eat line. */
		(void) fscanf(in, "%*[^\n]%*[\n]");
		++line_no;
	}

	out = stdopen(flag == 1 ? "-" : output, "wb", FILENO_STDOUT);
	if (out == NULL)
		exit(2);

	for (; ; ++line_no) {
		if (fgets((char *) line, sizeof line, in) == (char *) 0)
			FatalPrintLine(0, 0, filemsg, input);

		/* Encoded line is at most 62 characters :
		 * 1 character for the length, N, representing the number
		 * of bytes encoded by this line, N bytes encoded in
		 * 60 or less characters, and a newline.
		 */
		for (len = 0; line[len] != '\n' && len < 62; ++len) {
			/* Invalid character on line? */
  			if (line[len] < ' ' || (' '+64) < line[len]) {
				FatalPrintLine(
					0, 0, "File \"%s\" : %#x invalid character at %lu, %d",
					input, line[len], line_no, len+1
				);
			}

			/* Remove <space> bias. */
			line[len] = (unsigned char) ((line[len] - ' ') & 0x3f);
		}

		/* Was newline found within the first 62 characters
		 * and is the line length a multiple of 4?
		 */
		if (62 <= len)
			FatalPrintLine(0, 0, "File \"%s\" : line > 62 characters", input);

		if ((len-1) % 4 != 0)
			FatalPrintLine(0, 0, "File \"%s\" : encoding error %lu", input, line_no);

		/* End of body? */
		if (line[0] == 0)
			break;

		/* Decode 4 characters into 3 bytes. */
		for (lp = (unsigned char *) line, len = *lp++; ; lp += 4) {
			(void) fputc((lp[0] << 2) | (lp[1] >> 4), out);
			if (--len <= 0)
				break;
			(void) fputc((lp[1] << 4) | (lp[2] >> 2), out);
			if (--len <= 0)
				break;
			(void) fputc((lp[2] << 6) | lp[3], out);
			if (--len <= 0)
				break;
		}
	}

	/* Confirm "end" marker. */
	(void) fgets((char *) line, sizeof line, in);
	if (strcmp("end\n", (char *) line) != 0)
		FatalPrintLine(0, 0, "File \"%s\" : \"end\" not found", input);

	(void) fclose(out);
	(void) fclose(in);
}

int
main(int argc, char **argv)
{
	char *input;
	int ch, xflag;
	void (*encode)(const char *);
	void (*decode)(const char *, int);

	ErrorSetProgramName("uue");

	xflag = 0;
	encode = uuencode;
	decode = uudecode;

	while ((ch = getopt(argc, argv, "bhmudDxX")) != -1) {
		switch (ch) {
		case 'h':
			/* BinHex 4.0 */
			encode = encode40;
			decode = decode40;
			break;
		case 'b':
			/* MIME Base64 */
			encode = encode64;
			decode = decode64;
			break;
		case 'u':
			/* Unix to Unix (traditional) */
			encode = uuencode;
			decode = uudecode;
			break;
		case 'D': case 'X':
			/* Decode to standard output. */
			xflag = 1;
			break;
		case 'd': case 'x':
			/* Decode to file named by encoded file. */
			xflag = 2;
			break;
		default:
			UsagePrintLine(usage_msg);
		}
	}

	if (argc <= optind)
		UsagePrintLine(usage_msg);

	input = argv[optind];

	if (xflag)
		(*decode)(input, xflag);
	else
		(*encode)(input);

	return 0;
}


