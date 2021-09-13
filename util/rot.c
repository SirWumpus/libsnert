/*
 * rot.c
 *
 * Caesar cipher using English alphabet, printable ASCII, or user defined.
 *
 * Copyright 2020 by Anthony Howe.  All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char ALPHA_UPPER[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
const char ALPHA_LOWER[] = "abcdefghijklmnopqrstuvwxyz";
const char PRINTABLE_ASCII[] = "!\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";

static int
rot_rotate(const char *alphabet, int size, int rotate, int ch)
{
	char *a = strchr(alphabet, ch);
	if (a != NULL) {
		return alphabet[((a - alphabet) + rotate) % size];
	}
	return ch;
}

void
rot_init(const char *alphabet, int rotate, char table[2][256])
{
	int size, ch;
	const char *a;

	size = strlen(alphabet);

	for (ch = 0; ch < 256; ch++) {
		table[0][ch] = ch;
		table[1][ch] = ch;
	}

	for (a = alphabet; *a != '\0'; a++) {
		ch = rot_rotate(alphabet, size, rotate, *a);
		table[0][*a] = ch;	/* encoding */
		table[1][ch] = *a;	/* decoding */
	}
}

void
rot_print(FILE *fp, char table[256], char *s)
{
	for ( ; *s != '\0'; s++) {
		(void) fputc(table[*s], fp);
	}
}

#ifdef TEST

#include <getopt.h>

static char usage[] =
"usage: rot [-dp][-a set][-r rotate] [message]]\n"
"\n"
"-a set\t\tset alphabet order\n"
"-d\t\tdecode message\n"
"-p\t\talphabet is printable ASCII characters\n"
"-r rotate\trotate distance; default half alphabet size\n"
"\n"
"If message is omitted from the command line, then read the message\n"
"from standard input.\n"
"\n"
"Copyright 2020 by Anthony Howe.  All rights reserved.\n"
;

int
main(int argc, char **argv)
{
	const char *alphabet;
	int ch, rotate, decode, opt_r;
	char encode_decode[2][256], input[128], *table;

	opt_r = 0;
	decode = 0;
	alphabet = ALPHA_UPPER;

	while ((ch = getopt(argc, argv, "dpa:r:")) != -1) {
		switch (ch) {
		case 'a':
			alphabet = (const char *) optarg;
			break;
		case 'd':
			decode = 1;
			break;
		case 'p':
			alphabet = PRINTABLE_ASCII;
			break;
		case 'r':
			rotate = (int) strtol(optarg, NULL, 10);
			opt_r = 1;
			break;
		default:
			fprintf(stderr, "%s", usage);
			return 2;
		}
	}

	if (!opt_r) {
		rotate = strlen(alphabet) / 2;
	}

	rot_init(alphabet, rotate, encode_decode);
	table = encode_decode[decode];

	if (optind < argc) {
		for ( ; optind < argc; optind++) {
			rot_print(stdout, table, argv[optind]);
			if (argv[optind+1] != NULL) {
				(void) fputc(' ', stdout);
			}
		}
	} else {
		while (fgets(input, sizeof (input), stdin) != NULL) {
			rot_print(stdout, table, input);
		}
	}

	return EXIT_SUCCESS;
}

#endif /* TEST */
