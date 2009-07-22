/*
 * kvmap.c
 *
 * Key-Value Map
 *
 * Copyright 2006 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <stdio.h>
#include <stdlib.h>

#include <com/snert/lib/io/Log.h>
#include <com/snert/lib/type/kvm.h>
#include <com/snert/lib/util/getopt.h>
#include <com/snert/lib/util/Text.h>

/***********************************************************************
 ***
 ***********************************************************************/

static const char usage[] =
"usage: kvmap [-ablLuU] map <textfile\n"
"usage: kvmap  -d      map >textfile\n"
"\n"
"-a\t\tappend/modify an existing map\n"
"-b\t\tallow blank or empty value field\n"
"-d\t\tdump the map to standard output\n"
"-l\t\tfold keys to lower case\n"
"-L\t\tfold values to lower case\n"
"-u\t\tfold keys to upper case\n"
"-U\t\tfold values to upper case\n"
"\n"
"A map is a string of the form:\n"
"\n"
"  table-name" KVM_DELIM_S "type" KVM_DELIM_S "[sub-type" KVM_DELIM_S "]location\n"
"\n"
"The following forms of type" KVM_DELIM_S "[sub-type" KVM_DELIM_S "]location are supported:\n"
"\n"
"  text" KVM_DELIM_S "/path/map.txt\n"
#ifdef HAVE_DB_H
"  db" KVM_DELIM_S "/path/map.db\n"
"  db" KVM_DELIM_S "btree" KVM_DELIM_S "/path/map.db\n"
#endif
#ifdef HAVE_SQLITE3_H
"  sql" KVM_DELIM_S "/path/database\n"
#endif
#ifdef SOCKET_MAP_LIST
"  socketmap" KVM_DELIM_S "host[,port]\n"
"  socketmap" KVM_DELIM_S "/path/local/socket\n"
"  socketmap" KVM_DELIM_S "123.45.67.89:port\n"
"  socketmap" KVM_DELIM_S "[2001:0DB8::1234]:port\n"
"\n"
"The default port for socketmap is " KVM_PORT_S ".\n"
#endif
"\n"
LIBSNERT_COPYRIGHT "\n"
;

static int
dump(kvm_data *k, kvm_data *v, void *data)
{
	if (0 < k->size) {
		fputs((char *) k->data, stdout);
		fputc('\t', stdout);
		if (0 < v->size)
			fputs((char *) v->data, stdout);
		fputc('\n', stdout);
	}

	return 1;
}

int
main(int argc, char **argv)
{
	kvm *map;
	long length;
	kvm_data key, value;
	unsigned long lineno;
	char *table, *location;
	static char buffer[BUFSIZ];
	int rc, ch, dump_mode = 0, append_mode = 0, key_case = 0, value_case = 0, allow_empty = 0;

	while ((ch = getopt(argc, argv, "abdluLU")) != -1) {
		switch (ch) {
		case 'a':
			append_mode = 1;
			break;
		case 'b':
			allow_empty = 1;
			break;
		case 'd':
			dump_mode = 1;
			break;
		case 'l': case 'u':
			key_case = ch;
			break;
		case 'L': case 'U':
			value_case = ch;
			break;
		default:
			(void) fprintf(stderr, usage);
			exit(64);
		}
	}

	if (argc < optind + 1) {
		(void) fprintf(stderr, usage);
		exit(64);
	}

	table = argv[optind];
	if ((location = strchr(table, KVM_DELIM)) == NULL) {
		fprintf(stderr, "invalid map argument\n");
		exit(EXIT_FAILURE);
	}
	*location++ = '\0';

	if ((map = kvmOpen(table, location, 0)) == NULL) {
		fprintf(stderr, "%s" KVM_DELIM_S "%s open error\n", table, location);
		exit(EXIT_FAILURE);
	}

	rc = EXIT_SUCCESS;

	if (dump_mode) {
		map->walk(map, dump, NULL);
	} else {
		(void) map->begin(map);

		if (!append_mode)
			map->truncate(map);

		for (lineno = 1; 0 <= (length = TextInputLine(stdin, buffer, sizeof (buffer))); lineno++) {
			if (length == 0 || buffer[0] == '#')
				continue;

			/* Key ends with first white space. */
			key.data = (unsigned char *) buffer;
			key.size = strcspn(buffer, " \t");
			buffer[key.size] = '\0';

			switch (key_case) {
			case 'l':
				TextLower((char *) key.data, -1);
				break;
			case 'u':
				TextUpper((char *) key.data, -1);
				break;
			}

			if (length == key.size) {
				if (!allow_empty) {
					fprintf(stderr, "kvmap: warning at %ld: key \"%s\" has no value, skipping\n", lineno, key.data);
					continue;
				}

				value.data = (unsigned char *) "";
				value.size = 0;
			} else {
				/* Value starts with first non-white space following key. */
				value.size = key.size + 1 + strspn(buffer + key.size + 1, " \t");
				value.data = (unsigned char *) buffer + value.size;
				value.size = length - value.size;
			}

			switch (value_case) {
			case 'l':
				TextLower((char *) value.data, -1);
				break;
			case 'u':
				TextUpper((char *) value.data, -1);
				break;
			}

			if (!allow_empty && value.size == 0) {
				fprintf(stderr, "kvmap: warning at %ld: key \"%s\" has an empty value\n", lineno, key.data);
			}

			if (map->put(map, &key, &value) == KVM_ERROR) {
				fprintf(stderr, "kvmap: error at %ld: saving key \"%s\" failed\n", lineno, key.data);
				rc = EXIT_FAILURE;
			}
		}

		(void) map->commit(map);
	}

	map->close(map);

	return rc;
}

/***********************************************************************
 *** END
 ***********************************************************************/

