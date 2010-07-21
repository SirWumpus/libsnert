/*
 * ixhash.c
 *
 * Copyright 2007, 2010 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <com/snert/lib/util/md5.h>
#include <com/snert/lib/util/ixhash.h>

static const char abs_url[] = ":/";
static const char special_glyphs[] = "<>()|@*'!?,";

size_t
ixhash_count_lf(const unsigned char *body, size_t size)
{
	size_t count;

	for (count = 0; 0 < size; size--, body++) {
		if (*body == '\n')
			count++;
	}

	return count;
}

size_t
ixhash_count_space_tab(const unsigned char *body, size_t size)
{
	size_t count;

	for (count = 0; 0 < size; size--, body++) {
		if (*body == ' ' || *body == '\t')
			count++;
	}

	return count;
}

size_t
ixhash_count_delims_or_abs_url(const unsigned char *body, size_t size)
{
	size_t count;

	for (count = 0; 0 < size; size--, body++) {
		if ((*body == ':' && 0 < size && body[1] == '/'))
			count++;
		else if (strchr(special_glyphs, *body) != NULL)
			count++;
	}

	return count;
}

/*
 * Use hash1 if the message contains at least 2 lines and at least
 * 20 spaces or tabs.
 */
int
ixhash_condition1(const unsigned char *body, size_t size)
{
	return 2 <= ixhash_count_lf(body, size) && 20 <= ixhash_count_space_tab(body, size);
}

/*
 * The first checksum requires at least 1 line break and 16 spaces/tabs:
 *
 *	md5hash=|tr -s '[:space:]' \
 *		|tr -d '[:graph:]' \
 *		|md5sum \
 *		|tr -d ' -'
 */
void
ixhash_hash1(md5_state_t *md5, const unsigned char *body, size_t size)
{
	int prev, ch;

	for (prev = -1; 0 < size; size--, body++) {
		ch = *body;

		/* Compress runs of same whitespace character. Note that
		 * CRLF should be compressed to LF; this is because the
		 * original procmail script was feed messages with LF
		 * newlines, not the original SMTP data that use CRLF.
		 */
		if (prev == '\r' && ch == '\n')
			continue;
		if (isspace(ch) && prev == ch)
			continue;

		prev = ch;

		/* Delete graphical characters (non-whitespace). */
		if (isgraph(ch))
			continue;

		md5_append(md5, body, 1);
	}
}

/*
 * Use hash2 if the message contains at least three occurences of:
 *
 *	< > ( ) | @ * ' ! ? ,
 *
 * or the combination ":/"
 */
int
ixhash_condition2(const unsigned char *body, size_t size)
{
	return 3 <= ixhash_count_delims_or_abs_url(body, size);
}

/*
 * Remove numbers, chars, '=', carriage returns, and "%&#;" (often in
 * obfuscated HTML code) and convert underscores into dots.
 *
 *	md5hash=|tr -d '[:cntrl:][:alnum:]%&#;=' \
 *		|tr '_' '.' \
 *		|tr -s '[:print:]' \
 *		|md5sum \
 *		|tr -d ' -'
 */
void
ixhash_hash2(md5_state_t *md5, const unsigned char *body, size_t size)
{
	int prev, ch;

	for (prev = -1; 0 < size; size--, body++) {
		ch = *body;

		if (iscntrl(ch) || isalnum(ch) || strchr("%&#;=", ch) != NULL)
			continue;
		if (ch == '_')
			ch = '.';
		if (isprint(ch) && prev == ch)
			continue;
		prev = ch;

		md5_append(md5, body, 1);
	}
}

/*
 * Otherwise use hash3 if the message has a minimum amount of content.
 */
int
ixhash_condition3(const unsigned char *body, size_t size)
{
	return 8 <= size;
}

/*
 *	md5hash=|tr -d '[:cntrl:][:space:]=' \
 *		|tr -s '[:graph:]' \
 *		|md5sum \
 *		|tr -d ' -'
 */
void
ixhash_hash3(md5_state_t *md5, const unsigned char *body, size_t size)
{
	int prev, ch;

	for (prev = -1; 0 < size; size--, body++) {
		ch = *body;

		if (iscntrl(ch) || isspace(ch) || ch == '=')
			continue;
		if (isgraph(ch) && prev == ch)
			continue;
		prev = ch;

		md5_append(md5, body, 1);
	}
}

/***********************************************************************
 ***
 ***********************************************************************/

#ifdef TEST
#include <errno.h>
#include <com/snert/lib/util/getopt.h>

#define CHUNK_SIZE		(64 * 1024)

int debug;
int is_mail_message = 1;

const char usage[] =
"usage: ixhash [-av] file ...\n"
"\n"
"-a\t\thash the whole file (otherwise mail message body only)\n"
"-v\t\tverbose debug output\n"
"\n"
"A file argument can be hyphen (-) to indicate reading from standard\n"
"input.\n"
"\n"
"Copyright 2007, 2010 by Anthony Howe. All rights reserved.\n"
;

static const char hex_digit[] = "0123456789abcdef";

static void
print_result(md5_state_t *md5)
{
	unsigned char digest[16], digest_string[33];

	md5_finish(md5, (md5_byte_t *) digest);
	md5_digest_to_string(digest, digest_string);
	fputs(digest_string, stdout);
	fputc('\n', stdout);
}

static void
ixhash_file(FILE *fp)
{
	ssize_t size;
	ixhash_fn filter;
	md5_state_t hash1, hash2, hash3;
	unsigned char chunk[CHUNK_SIZE], *body;

	if ((size = fread(chunk, 1, sizeof (chunk), fp)) <= 0) {
		fprintf(stderr, feof(fp) ? "premature EOF\n" : "read error\n");
		exit(EXIT_FAILURE);
	}

	body = chunk;

	if (is_mail_message) {
		int ch = chunk[size];
		chunk[sizeof (chunk)-1] = '\0';

		/* Find end of headers. */
		if ((body = strstr(chunk, "\n\n")) != NULL) {
			body += 2;
		} else if ((body = strstr(chunk, "\n\r\n")) != NULL) {
			body += 3;
		} else {
			fprintf(stderr, "end of message headers not found\n");
			exit(EXIT_FAILURE);
		}

		chunk[size] = ch;
		size -= body - chunk;
	}

	if (debug) {
		md5_init(&hash1);
		md5_init(&hash2);
		md5_init(&hash3);

		do {
			ixhash_hash1(&hash1, body, size);
			ixhash_hash2(&hash2, body, size);
			ixhash_hash3(&hash3, body, size);
			size = fread(chunk, 1, sizeof (chunk), fp);
			body = chunk;
		} while (0 < size);

		print_result(&hash1);
		print_result(&hash2);
		print_result(&hash3);
	} else {
		if (ixhash_condition1(body, size))
			filter = ixhash_hash1;
		else if (ixhash_condition2(body, size))
			filter = ixhash_hash2;
		else if (ixhash_condition3(body, size))
			filter = ixhash_hash3;
		else
			exit(EXIT_FAILURE);

		md5_init(&hash1);

		do {
			(*filter)(&hash1, body, size);
			size = fread(chunk, 1, sizeof (chunk), fp);
			body = chunk;
		} while (0 < size);

		print_result(&hash1);
	}
}

int
main(int argc, char **argv)
{
	int ch;
	FILE *fp;

	while ((ch = getopt(argc, argv, "av")) != -1) {
		switch (ch) {
		case 'a':
			is_mail_message = 0;
			break;
		case 'v':
			debug++;
			break;
		default:
			(void) fprintf(stderr, usage);
			exit(EXIT_FAILURE);
		}
	}

	if (argc <= optind) {
		(void) fprintf(stderr, usage);
		exit(EXIT_FAILURE);
	}

	for ( ; optind < argc; optind++) {
		if (argv[optind][0] == '-' && argv[optind][1] == '\0') {
			ixhash_file(stdin);
		} else if ((fp = fopen(argv[optind], "r")) == NULL) {
			fprintf(stderr, "%s: %s (%d)\n", argv[optind], strerror(errno), errno);
		} else {
			ixhash_file(fp);
			fclose(fp);
		}
	}

	return EXIT_SUCCESS;
}

#endif /* TEST */
