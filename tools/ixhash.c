/*
 * ixhash.c
 *
 * Copyright 2007 by Anthony Howe. All rights reserved.
 */

/* The original procmail script...
 *
 *	 # The first checksum requires at least 1 line break and 16 spaces/tabs:
 *	 :0B
 *	 * .$+.
 *	 * -15^0
 *	 * 1^1 [		]
 *	 {
 *	   :0 bw
 *	   md5hash=|tr -s '[:space:]' \
 *	           |tr -d '[:graph:]' \
 *	           |md5sum \
 *	           |tr -d ' -'
 *	   # 1st hash already generated from a previously received email?
 *	   :0 Aw
 *	   * ? fgrep -s $md5hash $HASHFILE
 *	   { KNOWN=YES }
 *	 }
 *
 *	 :0B
 *	 # Try another checksum if there was no match:
 *	 * ! KNOWN ?? YES
 *	 # Minimum requirements this time: 3 of the
 *	 # following incidences within the mail body.
 *	 * -2^0
 *	 * 1^1 ([<>()|@*'!?,]|:/)
 *	 { :0 bw
 *	   # Remove numbers, chars, '=', carriage returns,
 *	   # and "%&#;" (often in obfuscated HTML code) and
 *	   # convert underscores into dots:
 *	   md5hash2=|tr -d '[:cntrl:][:alnum:]%&#;=' \
 *	            |tr '_' '.' \
 *	            |tr -s '[:print:]' \
 *	            |md5sum \
 *	            |tr -d ' -'
 *	   # 2nd hash already generated from a previously received email?
 *	   :0 Aw
 *	   * ? fgrep -s $md5hash2 $HASHFILE
 *	   { KNOWN=YES }
 *	   # Merge hashes:
 *	   :0
 *	   * md5hash2 ?? .
 *	   { md5hash="$md5hash   $md5hash2" }
 *	 }
 *
 *	 :0B
 *	 # This is a default checksum for emails without enough
 *	 # whitespace or punctuation structure and therefore still no hash:
 *	 * ! md5hash ?? .
 *	 # Check only if some content exists:
 *	 * ........
 *	 {
 *	   :0 bw
 *	   md5hash=|tr -d '[:cntrl:][:space:]=' \
 *	           |tr -s '[:graph:]' \
 *	           |md5sum \
 *	           |tr -d ' -'
 *	 :0 Aw
 *	   * ? fgrep -s $md5hash $HASHFILE
 *	   { KNOWN=YES }
 *	 }
 *
 *	 :0
 *	 * KNOWN ?? YES
 *	 {
 *	   # Separate recognized emails if desired:
 *	   :0
 *	   * KNOWNMAIL ?? ..
 *	   $KNOWNMAIL
 *	 # Otherwise mark the checksum matches in the subject:
 *	 #  :0 Efhw
 *	 #  * ^Subject:\/.*
 *	 #  | formail -i "Subject: [KNOWN]$MATCH"
 *	 #  :0 Efhw
 *	 #  | formail -I "Subject: [KNOWN]"
 *	 :0 Efhw
 *	   | formail -A "X-AXHASH: Known"
 *	 }
 *	 :0Eci:$HASHFILE$LOCKEXT
 *	 # Save unrecognized hashes if desired:
 *	 * SAVE_HASHES ?? YES
 *	 #| perl -e 'print "$ARGV[0]\n";' $md5hash >> $HASHFILE
 *	 | newecho $md5hash >> $HASHFILE
 */

#include <com/snert/lib/version.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <com/snert/lib/util/md5.h>
#include <com/snert/lib/util/getopt.h>

#define CHUNK_SIZE		(64 * 1024)

/* Actual ASCII character code vs. the C compiler's
 * interpretation of some special character constants.
 */
#define ASCII_NUL		0x00
#define ASCII_BS		0x08
#define ASCII_TAB		0x09
#define ASCII_LF		0x0A
#define ASCII_FF		0x0C
#define ASCII_CR		0x0D
#define ASCII_SPACE		0x20
#define ASCII_DEL		0x7F

int body_only;
md5_state_t md5;
unsigned long size;
unsigned char digest[16];
char chunk[CHUNK_SIZE], digest_string[33];

const char abs_url[] = ":/";
const char special_glyphs[] = "<>()|@*'!?,";

const char usage[] =
"usage: ixhash [-b] < message\n"
"\n"
"-b\t\tskip message headers, ixhash message body only\n"
"\n"
"Copyright 2007 by Anthony Howe. All rights reserved.\n"
;

static const char hex_digit[] = "0123456789abcdef";

static void
digestToString(unsigned char digest[16], char digest_string[33])
{
	int i;

	for (i = 0; i < 16; i++) {
		digest_string[i << 1] = hex_digit[(digest[i] >> 4) & 0x0F];
		digest_string[(i << 1) + 1] = hex_digit[digest[i] & 0x0F];
	}
	digest_string[32] = '\0';
}

const char *
find_newline(const char *body, unsigned long size)
{
	for ( ; 0 < size; size--, body++) {
		if (*body == '\n')
			return body;
	}

	return NULL;
}

unsigned
count_horizontal_whitespace(const char *body, unsigned long size, int min)
{
	unsigned count = 0;

	for ( ; 0 < size && count < min; size--, body++) {
		if (*body == ' ' || *body == '\t')
			count++;
	}

	return count;
}

#ifdef SLOW

unsigned
count_abs_url(const char *body, unsigned long size, int min)
{
	unsigned count = 0;

	for ( ; 0 < size && count < min; size--, body++) {
		if (*body == ':' && 0 < size && body[1] == '/')
			count++;
	}

	return count;
}

unsigned
count_delims(const char *body, unsigned long size, int min, const char *delims)
{
	unsigned count = 0;

	for ( ; 0 < size && count < min; size--, body++) {
		if (strchr(delims, *body) != NULL)
			count++;
	}

	return count;
}

#else

unsigned
count_delims_or_abs_url(const char *body, unsigned long size, int min, const char *delims)
{
	unsigned count = 0;

	for ( ; 0 < size && count < min; size--, body++) {
		if ((*body == ':' && 0 < size && body[1] == '/'))
			count++;
		else if (strchr(delims, *body) != NULL)
			count++;
	}

	return count;
}

#endif

/*
 *   md5hash=|tr -s '[:space:]' \
 *           |tr -d '[:graph:]' \
 *           |md5sum \
 */
void
filter1(const char *body, unsigned long size)
{
	int prev;
	unsigned char ch;

	for (prev = -1; 0 < size; size--, body++) {
		ch = (unsigned char) *body;

		if (isspace(ch) && prev == ch)
			continue;
		prev = ch;

		if (isgraph(ch))
			continue;

		md5_append(&md5, &ch, 1);
	}
}

/*
 *   # Remove numbers, chars, '=', carriage returns,
 *   # and "%&#;" (often in obfuscated HTML code) and
 *   # convert underscores into dots:
 *   md5hash2=|tr -d '[:cntrl:][:alnum:]%&#;=' \
 *            |tr '_' '.' \
 *            |tr -s '[:print:]' \
 *            |md5sum \
 */
void
filter2(const char *body, unsigned long size)
{
	int prev;
	unsigned char ch;

	for (prev = -1; 0 < size; size--, body++) {
		ch = (unsigned char) *body;

		if (iscntrl(ch) || isalnum(ch) || strchr("%&#;=", ch) != NULL)
			continue;
		if (ch == '_')
			ch = '.';
		if (isprint(ch) && prev == ch)
			continue;
		prev = ch;

		md5_append(&md5, &ch, 1);
	}
}

/*
 *   md5hash=|tr -d '[:cntrl:][:space:]=' \
 *           |tr -s '[:graph:]' \
 *           |md5sum \
 */
void
filter3(const char *body, unsigned long size)
{
	int prev;
	unsigned char ch;

	for (prev = -1; 0 < size; size--, body++) {
		ch = (unsigned char) *body;

		if (iscntrl(ch) || isspace(ch) || ch == '=')
			continue;

		if (isgraph(ch) && prev == ch)
			continue;
		prev = ch;

		md5_append(&md5, &ch, 1);
	}
}

int
main(int argc, char **argv)
{
	int ch;
	const char *body;
	void (*filter)(const char *body, unsigned long size);

	while ((ch = getopt(argc, argv, "b")) != -1) {
		switch (ch) {
		case 'b':
			body_only = 1;
			break;
		default:
			(void) fprintf(stderr, usage);
			exit(2);
		}
	}

	if ((size = fread(chunk, 1, sizeof (chunk), stdin)) <= 0) {
		fprintf(stderr, feof(stdin) ? "premature EOF\n" : "read error\n");
		exit(2);
	}

	body = chunk;

	if (body_only) {
		ch = chunk[size];
		chunk[sizeof (chunk)-1] = '\0';

		if ((body = strstr(chunk, "\n\n")) == NULL
		&&  (body = strstr(chunk, "\n\r\n")) == NULL) {
			fprintf(stderr, "end of message headers not found\n");
			exit(2);
		}

		body += strspn(body, "\n\r");
		size -= body - chunk;
		chunk[size] = ch;
	}

	/* # The first checksum requires at least 1 line break and 16 spaces/tabs:
	 * * .$+.
	 * * -15^0
	 * * 1^1 [		]
	 */
	if (find_newline(body, size) != NULL && 16 <= count_horizontal_whitespace(body, size, 16))
		filter = filter1;

	/* # Minimum requirements this time: 3 of the
	 * # following incidences within the mail body.
	 * * -2^0
	 * * 1^1 ([<>()|@*'!?,]|:/)
	 */
	else if (3 <= count_delims_or_abs_url(body, size, 3, special_glyphs))
		filter = filter2;

	/* # Check only if some content exists:
	 * * ........
	 */
	else if (8 <= size - (body - chunk))
		filter = filter3;

	/* Insufficient content. */
	else
		return 1;

	md5_init(&md5);

	do {
		(*filter)(body, size);
		size = fread(chunk, 1, sizeof (chunk), stdin);
		body = chunk;
	} while (0 < size);

	md5_finish(&md5, (md5_byte_t *) digest);
	digestToString(digest, digest_string);
	fputs(digest_string, stdout);
	fputc('\n', stdout);

	return 0;
}
