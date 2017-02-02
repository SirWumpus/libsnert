/*
 * Simple indent(1) like tool.
 *
 * Copyright 2014 by Anthony Howe. All rights reserved.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <sysexits.h>

static const char newline[] = "\n";

void
print_indent(FILE *out, const char *indent, int count)
{
	int i;

	for (i = 0; i < count; i++)
		(void) fputs(indent, out);
}

int
json_reader_dump(FILE *in, const char *indent, FILE *out)
{
	int escape = 0;
	int octet, level = 0, quote = 0;

	while ((octet = fgetc(in)) != -1) {
		if (escape) {
			fputc(octet, out);
			escape = 0;
			continue;
		}

		switch (octet) {
		case '\\':
			escape = 1;
			break;

		case ',': case ';':
			fputc(octet, out);
			if (quote == 0) {
				// Newline following comma between elements.
				fputs(newline, out);
				print_indent(out, indent, level);
			}
			continue;

		case '"': case '\'':
			if (quote == 0)
				quote = octet;
			else if (octet == quote)
				quote = 0;
			break;

		case '{': case '[':
			if (quote != 0)
				break;
			fputc(octet, out);
			fputs(newline, out);
			print_indent(out, indent, ++level);
			continue;

		case '}': case ']':
			if (quote != 0)
				break;
			fputs(newline, out);
			print_indent(out, indent, --level);
			if (level < 0)
				fprintf(stderr, "%c inbalance\n", octet);
			break;

		case '\n': case '\r':
			if (quote == 0) {
				// Discard unquoted newlines.
				continue;
			}
			break;
		}

		fputc(octet, out);
	}

	fputs(newline, out);

	return level != 0 || quote != 0;
}

int
file(const char *fn, const char *indent, int flags)
{
	int ex;
	FILE *in = stdin;

	if (fn != NULL && fn[0] != '-' && fn[1] != '\0') {
		if ((in = fopen(fn, "r")) == NULL) {
			fprintf(stderr, "%s: %s\n", fn, strerror(errno));
			return EX_IOERR;
		}
	}

	ex = json_reader_dump(in, indent, stdout) ? EX_DATAERR : EXIT_SUCCESS; 
	(void) fclose(in);

	if (ex != EXIT_SUCCESS) {
		(void) fprintf(stderr, "missing closing \", }, and/or ]\n");
	}

	return ex;
}

int
main(int argc, char **argv)
{
	char *indent = "\t";
	int ch, err, argi, flags;

	flags = 0;
	while ((ch = getopt(argc, argv, "i:")) != -1) {
		switch (ch) {
		case 'i':
			indent = optarg;
			break;
		default:
			fprintf(stderr, "usage: %s [-i string] [file...]\n", argv[0]);
			return EX_USAGE;
		}
	}

	err = EXIT_SUCCESS;

	if (optind < argc) {
		for (argi = optind; argi < argc; argi++) {
			if (file(argv[argi], indent, flags))
				err = EXIT_FAILURE;
		}
	} else if (file(NULL, indent, flags)) {
		err = EXIT_FAILURE;
	}

	return err;
}
