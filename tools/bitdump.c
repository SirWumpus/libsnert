#include <err.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>

const char usage[] =
"usage: bitdump [-A base][-j skip][-N count] [file ...]\n"
"\n"
"-A base\t\taddress base, one of n, o, d, x\n"
"-j skip\t\tskip N bytes from start of input\n"
"-N count\tread at most N bytes of input\n"
"\n"
"An od(1) like tool for displaying standard input or files as\n"
"a stream of bits instead of bytes.\n"
"\n"
;

char filemsg[] = "File \"%s\" ";
const char *bits[] = {
	"0000", "0001", "0010", "0011", "0100", "0101", "0110", "0111",
	"1000", "1001", "1010", "1011", "1100", "1101", "1110", "1111"
};

typedef struct {
	int letter;
	const char *fmt;
} BaseMap;

BaseMap basemap[] = {
	{ 'n', "" },
	{ 'o', "%06o: " },
	{ 'd', "%07u: " },
	{ 'x', "%08x: " },
	{ '\0', NULL }
};

long jump;
long maxcount;
const char *basefmt;

int
dump(const char *fn)
{
	FILE *fp;
	int octet, i;
	char ascii[7];
	unsigned count, offset;

	if (fn == NULL || (fn[0] == '-' && fn[1] == '\0')) {
		fp = stdin;
		for (i = 0; i < jump; i++)
			(void) fgetc(fp);
	} else if ((fp = fopen(fn, "rb")) == NULL) {
		warn(filemsg, fn);
		return 1;
	} else if (fseek(fp, jump, SEEK_SET) != 0) {
		warn(filemsg, fn);
		return 1;
	}

	offset = jump;
	ascii[6] = '\0';
	for (i = count = 0; count < maxcount && (octet = fgetc(fp)) != EOF; count++, i++) {
		if (i == 0)
			(void) printf(basefmt, offset);
		ascii[i] = isprint(octet) ? (char)octet : '.';
		(void) printf("%s %s ", bits[(octet & 0xF0) >> 4], bits[octet & 0x0F]);
		if (i == 5) {
			(void) printf("%s\n", ascii);
			i = -1;
		}
		offset++;
	}
	if (0 < i) {
		ascii[i] = '\0';
		for ( ; i < 6; i++)
			(void) printf("          ");
		(void) printf("%s\n", ascii);
	}
	(void) printf(basefmt, offset);
	(void) printf("\n");

	if (fn != NULL)
		(void) fclose(fp);

	return 0;
}

int
main(int argc, char **argv)
{
	int ch, argi;
	int ex = EXIT_SUCCESS;

	maxcount = ~0UL >> 1;
	basefmt = basemap[3].fmt;

	while ((ch = getopt(argc, argv, "A:j:N:")) != -1) {
		switch (ch) {
		case 'A':
			for (argi = 0; basemap[argi].fmt != NULL; argi++) {
				if (*optarg == basemap[argi].letter)
					break;
			}
			basefmt = basemap[argi].fmt;
			break;
		case 'j':
			jump = strtol(optarg, NULL, 0);
			break;
		case 'N':
			maxcount = strtol(optarg, NULL, 10);
			break;
		default:
			(void) fprintf(stderr, usage);
			exit(2);
		}
	}
	if (basefmt == NULL) {
		(void) fprintf(stderr, usage);
		exit(2);
	}

	if (0 < optind) {
		for (argi = optind; argi < argc; argi++) {
			if (dump(argv[argi]))
				ex = EXIT_FAILURE;
		}
	} else {
		dump(NULL);
	}

	return ex;
}
