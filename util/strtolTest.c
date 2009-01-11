#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
main(int argc, char **argv)
{
	int i;
	char *stop;
	long value, base;

	if (argc < 2 || argc % 2 != 1) {
		fprintf(stderr, "usage: strtolTest number base ....\n");
		exit(2);
	}

	for (i = 1; i < argc; i += 2) {
		base = strtol(argv[i+1], &stop, 10);
		if (argv[i+1] == stop || *stop != '\0' || base < 2 || 36 < base) {
			fprintf(stderr, "base \"%s\" must be a decimal integer between 2 and 36\n", argv[i+i]);
			exit(1);
		}

		value = strtol(argv[i], &stop, base);
		if (argv[i] == stop || *stop != '\0') {
			fprintf(stderr, "\"%s\" is not a number in base %ld\n", argv[i], base);
			exit(1);
		}
	}

	exit(0);
}
