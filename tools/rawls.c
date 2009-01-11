/*
 * rawls.c
 *
 * Copyright 2004 by Anthony Howe.  All rights reserved.
 *
 * usage: rawls directory
 */

#include <time.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

int
main(int argc, char **argv)
{
	DIR *dir;
	long length;
	struct stat sb;
	struct dirent *d;
	char *timestamp, *s;
	
	if (argc != 2) {
		fprintf(stderr, "usage: rawls directory\n");
		return 2;
	}

	chdir(argv[1]);
	dir = opendir(argv[1]);
	while ((d = readdir(dir)) != NULL) {
		if (stat(d->d_name, &sb)) {
			printf("????????\t%s\n", d->d_name);
			continue;
		}
		
		timestamp = ctime(&sb.st_mtime);
		for (s = timestamp; *s != '\0'; s++) {
			if (*s == '\r' || *s == '\n')
				*s = '\0';
		}
		
		printf("%8lu\t\"%s\"\t%s\n", sb.st_size, timestamp, d->d_name);
	}	
	closedir(dir);
	
	return 0;
}
