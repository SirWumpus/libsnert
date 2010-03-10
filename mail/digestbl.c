/*
 * digestbl.c
 *
 *
 * Copyright 2007 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#if defined(HAVE_SYSLOG_H) && ! defined(__MINGW32__)
# include <syslog.h>
#endif

#include <com/snert/lib/io/Log.h>
#include <com/snert/lib/io/socket2.h>
#include <com/snert/lib/net/pdq.h>
#include <com/snert/lib/mail/mime.h>
#include <com/snert/lib/mail/tlds.h>
#include <com/snert/lib/type/Vector.h>
#include <com/snert/lib/util/md5.h>
#include <com/snert/lib/util/getopt.h>
#include <com/snert/lib/util/Text.h>

/***********************************************************************
 ***
 ***********************************************************************/

typedef struct {
	Mime *mime;
	md5_state_t md5;
	char content_type[80];
	char digest_string[33];
	const char *digest_found;
} Digest;

typedef struct {
	Vector suffixes;
	unsigned long *masks;
} DnsList;

int debug;
Digest digest;
char *digest_bl;
DnsList *dns_bl_list;

static char usage[] =
"usage: digestbl [-v][-d list] < message\n"
"\n"
"-d list,...\tDNS BL suffix[/mask] list to apply. Without the /mask\n"
"\t\ta suffix would be equivalent to suffix/0x00fffffe\n"
"-v\t\tverbose logging to system's user log\n"
"\n"
LIBSNERT_COPYRIGHT "\n"
;

/***********************************************************************
 ***
 ***********************************************************************/

#if ! defined(__MINGW32__)
void
syslog(int level, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	if (logFile == NULL)
		vsyslog(level, fmt, args);
	else
		LogV(level, fmt, args);
	va_end(args);
}
#endif

void
dnsListFree(void *_list)
{
	DnsList *list = _list;

	if (list != NULL) {
		VectorDestroy(list->suffixes);
		free(list->masks);
		free(list);
	}
}

DnsList *
dnsListCreate(const char *string)
{
	long i;
	DnsList *list;
	char *slash, *suffix;

	if ((list = malloc(sizeof (*list))) == NULL)
		goto error0;

	if ((list->suffixes = TextSplit(string, " ,;", 0)) == NULL)
		goto error1;

	if ((list->masks = calloc(sizeof (*list->masks), VectorLength(list->suffixes))) == NULL)
		goto error1;

	for (i = 0; i < VectorLength(list->suffixes); i++) {
		if ((suffix = VectorGet(list->suffixes, i)) == NULL)
			continue;

		if ((slash = strchr(suffix, '/')) == NULL) {
			list->masks[i] = (unsigned long) ~0L;
		} else {
			list->masks[i] = (unsigned long) strtol(slash+1, NULL, 0);
			*slash = '\0';
		}
	}

	return list;
error1:
	dnsListFree(list);
error0:
	return NULL;
}

const char *
dnsListIsListed(DnsList *dnslist, const char *name, PDQ_rr *list)
{
	long i;
	PDQ_A *rr;
	unsigned long bits;
	const char **suffixes;

	suffixes = (const char **) VectorBase(dnslist->suffixes);
	for (rr = (PDQ_A *) list; rr != NULL; rr = (PDQ_A *) rr->rr.next) {
		if (rr->rr.rcode != PDQ_RCODE_OK || rr->rr.type != PDQ_TYPE_A)
			continue;

		if (TextInsensitiveStartsWith(rr->rr.name.string.value, name) < 0)
			continue;

		for (i = 0; suffixes[i] != NULL; i++) {
			if (strstr(rr->rr.name.string.value, suffixes[i]) == NULL)
				continue;

			bits = NET_GET_LONG(rr->address.ip.value + rr->address.ip.offset);

			if ((bits & dnslist->masks[i]) != 0) {
				if (0 < debug)
					syslog(LOG_INFO, "found %s %s", rr->rr.name.string.value, rr->address.string.value);

				return suffixes[i];
			}
		}
	}

	return NULL;
}

const char *
dnsListLookup(DnsList *dnslist, const char *name)
{
	PDQ_rr *answers;
	const char *list_name = NULL;

	if (dnslist == NULL)
		return NULL;

	answers = pdqFetchDnsList(
		PDQ_CLASS_IN, PDQ_TYPE_A, name,
		(const char **) VectorBase(dnslist->suffixes), pdqWait
	);

	if (answers != NULL) {
		list_name = dnsListIsListed(dnslist, name, answers);
		pdqListFree(answers);
	}

	return list_name;
}

static void
digestToString(unsigned char digest[16], char digest_string[33])
{
	int i;
	static const char hex_digit[] = "0123456789abcdef";

	for (i = 0; i < 16; i++) {
		digest_string[i << 1] = hex_digit[(digest[i] >> 4) & 0x0F];
		digest_string[(i << 1) + 1] = hex_digit[digest[i] & 0x0F];
	}
	digest_string[32] = '\0';
}

void
digestHeaders(Mime *m)
{
	char *mark;
	Digest *ctx = m->mime_data;

	if (0 <= TextFind((char *) m->source.buffer, "Content-Type:*", m->source.length, 1)) {
		mark = (char *) &m->source.buffer[sizeof("Content-Type:")-1];
		mark += strspn(mark, " \t");
		mark[strcspn(mark, " \t\r\n;")] = '\0';
		TextCopy(ctx->content_type, sizeof (ctx->content_type), mark);
	}
}

static void
digestMimePartStart(Mime *m)
{
	Digest *ctx = m->mime_data;

	ctx->digest_string[0] = '\0';
	md5_init(&ctx->md5);
	digestHeaders(m);
}

static void
digestMimePartFinish(Mime *m)
{
	unsigned char digest[16];
	Digest *ctx = m->mime_data;

	md5_finish(&ctx->md5, (md5_byte_t *) digest);
	digestToString(digest, ctx->digest_string);

	printf("part=%u type=%s digest=%s ", m->mime_part_number, ctx->content_type, ctx->digest_string);

	if ((ctx->digest_found = dnsListLookup(dns_bl_list, ctx->digest_string)) != NULL) {
		printf("list=%s\n", ctx->digest_found);
	} else {
		printf("\n");
	}
}

static void
digestMimeDecodedOctet(Mime *m, int octet)
{
	Digest *ctx = m->mime_data;
	unsigned char byte = octet;

	md5_append(&ctx->md5, (md5_byte_t *) &byte, 1);
}

int
main(int argc, char **argv)
{
	int ch;

	while ((ch = getopt(argc, argv, "d:v")) != -1) {
		switch (ch) {
		case 'd':
			digest_bl = optarg;
			break;
		case 'v':
			LogOpen("(standard error)");
			LogSetProgramName("uri");
			LogSetLevel(LOG_DEBUG);
			socketSetDebug(1);
			pdqSetDebug(1);
			debug++;
			break;
		default:
			(void) fprintf(stderr, usage);
			exit(2);
		}
	}

	dns_bl_list = dnsListCreate(digest_bl);

	if ((digest.mime = mimeCreate(NULL)) == NULL) {
		fprintf(stderr, "mimeCreate error: %s (%d)\n", strerror(errno), errno);
		exit(1);
	}

	digest.mime->mime_data = &digest;
	digest.mime->mime_header = digestHeaders;
	digest.mime->mime_body_start = digestMimePartStart;
	digest.mime->mime_body_finish = digestMimePartFinish;
	digest.mime->mime_decoded_octet = digestMimeDecodedOctet;

	mimeReset(digest.mime);

	while ((ch = fgetc(stdin)) != EOF) {
		if (mimeNextCh(digest.mime, ch))
			break;
	}

	digestMimePartFinish(digest.mime);
	mimeFree(digest.mime);

	return 0;
}
