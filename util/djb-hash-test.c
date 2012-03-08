/*
 * djb-hash-test.c
 *
 * Copyright 2012 by Anthony Howe. All rights reserved.
 */

#ifndef HASH_TABLE_SIZE
#define HASH_TABLE_SIZE		(4 * 1024)
#endif

#include <com/snert/lib/version.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <com/snert/lib/util/getopt.h>
#include <com/snert/lib/util/Text.h>

/*
 * D.J. Bernstien Hash version 2 (+ replaced by ^).
 */
static unsigned long
djb_hash_index(const unsigned char *buffer, size_t size, size_t table_size)
{
        unsigned long hash = 5381;

        while (0 < size--)
                hash = ((hash << 5) + hash) ^ *buffer++;

        return hash & (table_size-1);
}

static unsigned long
get_hash(const char *s, size_t table_size)
{
	return djb_hash_index(s, strlen(s), table_size);
}

int
main(int argc, char **argv)
{
	int i, ch;
	size_t tsize = HASH_TABLE_SIZE;

	while ((ch = getopt(argc, argv, "t:")) != -1) {
		switch (ch) {
		case 't':
			tsize = (size_t) strtol(optarg, NULL, 10);
			break;
		default:
			fprintf(stderr, "usage: djb-hash-test [-t table_size] string ...\n");
			return 2;
		}
	}

	printf("table_size=%d\n", tsize);
	if (optind < argc && strcmp(argv[optind], "-") == 0) {
		char line[256];
		while (fgets(line, sizeof (line), stdin) != NULL)
			printf("%lu %s\n", get_hash(line, tsize), line);
		optind++;
	}

	for (i = optind; i < argc; i++)
		printf("%lu %s\n", get_hash(argv[i], tsize), argv[i]);

	return 0;
}
