/*
 * dkim-hash.c
 *
 * DKIM Hash test tool.
 *
 * gcc -I../../../include -L../../../lib -g -O0 -odkim-hash dkim-hash.c ../../../lib/libsnert.a
 */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <com/snert/lib/version.h>
#include <com/snert/lib/mail/mime.h>
#include <com/snert/lib/util/getopt.h>

#if HAVE_INTTYPES_H
# include <inttypes.h>
#else
# if HAVE_STDINT_H
# include <stdint.h>
# endif
#endif

#ifdef HAVE_MD4_H
# include <md4.h>
#endif
#ifdef HAVE_MD5_H
# include <md5.h>
#endif
#ifdef HAVE_SHA1_H
# include <sha1.h>
#endif
#ifdef HAVE_SHA2_H
# include <sha2.h>
#endif

static const uint8_t crlf[] = { ASCII_CR, ASCII_LF };

typedef struct {
	const char *name;
	void (*init)();
	void (*update)();
	void (*final)();
	size_t digest_length;
	size_t sizeof_ctx;
} Hash;

static Hash hash_map[] = {
#ifdef HAVE_SHA2_H
	{ "sha256", SHA256_Init, SHA256_Update, SHA256_Final, SHA256_DIGEST_LENGTH, sizeof (SHA256_CTX) },
	{ "sha512", SHA512_Init, SHA512_Update, SHA512_Final, SHA512_DIGEST_LENGTH, sizeof (SHA512_CTX) },
#endif
#ifdef HAVE_SHA1_H
	{ "sha1", SHA1Init, SHA1Update, SHA1Final, SHA1_DIGEST_LENGTH, sizeof (SHA1_CTX) },
#endif
#ifdef HAVE_MD5_H
	{ "md5", MD5Init, MD5Update, MD5Final, MD5_DIGEST_LENGTH, sizeof (MD5_CTX) },
#endif
	{ NULL, NULL, NULL, NULL, 0, 0 }
};

#define FLAG_PARSE_HDR		0x0001
#define FLAG_PARSE_BODY		0x0002
#define FLAG_DUMP_HEX		0x0004

typedef enum {
	STATE_START, STATE_END, STATE_ERROR,
	STATE_TEXT, STATE_WSP, STATE_CR, STATE_LF,
	STATE_HDR, STATE_HDR_WSP, STATE_COLON, STATE_EOH_CR
} DKIM_STATE;

typedef struct canonicalisation Canon;

typedef struct {
	DKIM_STATE state;
	int has_header;
	void *ctx_crlf;
	void *ctx;
	Canon *canon;
	Hash *hash;
	FILE *fp;
} CanonState;

typedef void (*CanonFn)(CanonState *, int);

struct canonicalisation {
	const char *name;
	CanonFn header;
	CanonFn body;
};

static CanonState *
canon_create(FILE *fp, Hash *hash, Canon *canon)
{
	CanonState *cs;

	if ((cs = malloc(sizeof (*cs) + 2 * hash->sizeof_ctx)) != NULL) {
		cs->fp = fp;
		cs->hash = hash;
		cs->canon = canon;
		cs->has_header = 0;
		cs->ctx_crlf = &cs[1];
		cs->ctx = (char *)cs->ctx_crlf + hash->sizeof_ctx;
		cs->state = STATE_START;
	}

	return cs;
}

/*
 * https://tools.ietf.org/html/rfc6376#section-3.4.1
 */
static void
canon_identity(CanonState *cs, int octet)
{
	uint8_t byte[1];

	if (octet != EOF) {
		*byte = (uint8_t) octet;
		(*cs->hash->update)(cs->ctx, byte, 1);
	}
}

static int
canon_is_crlf(CanonState *cs, int octet)
{
	int next;

	if (octet == ASCII_CR) {
		next = fgetc(cs->fp);
		ungetc(next, cs->fp);
		return next == ASCII_LF;
	}

	return 0;
}

/*
 * http://tools.ietf.org/html/rfc5322#section-2.2
 * https://tools.ietf.org/html/rfc6376#section-3.4.2
 *
 *	     	S   0   1   2   3   4   5   6   7
 *	-------------------------------------------
 *	other	0   0   B   4   4   4   E   0   E
 *	SP/TAB	B   1   1   3   3   3   E   3   E
 *	colon	B   2   3   4   4   4   E   E   E
 *	CR	7   B   B   5   5   5   E   7   3
 *	LF	B   B   B   6   6   6   6   E   X
 *	EOF	X   E   B   E   E   E   E   X   X
 *
 *	S = start, X = exit, E = error, B = body/error
 */
static void
canon_header_relaxed(CanonState *cs, int octet)
{
	uint8_t byte[1];

	if (octet == EOF) {
		switch (cs->state) {
		case STATE_START:	/* No headers, no body */
		case STATE_LF:		/* Headers, no EOH, no body. */
		case STATE_END:		/* Headers, EOH, no body. */
			cs->state = STATE_END;
			break;

		default:
			cs->state = STATE_ERROR;
			break;
		}
		return;
	}

	switch (cs->state) {
	case STATE_START:
		if (octet == ASCII_CR) {
			/* Message without headers starts with CRLF? */
			cs->state = STATE_EOH_CR;
			return;
		}
		/*@fallthrough@*/

	case STATE_HDR:
		if (octet == ':') {
			cs->has_header = 1;
			cs->state = STATE_COLON;
		} else if (isgraph(octet) && octet < ASCII_DEL) {
			cs->state = STATE_HDR;
			octet = tolower(octet);
		} else if (octet == ASCII_SPACE || octet == ASCII_TAB) {
			/* Strip whitespace between name and colon. */
			cs->state = STATE_HDR_WSP;
			return;
		} else {
			cs->state = STATE_ERROR;
			return;
		}
		break;

	case STATE_HDR_WSP:
		if (octet == ':') {
			/* End of header name. */
			cs->has_header = 1;
			cs->state = STATE_COLON;
		} else if (octet == ASCII_SPACE || octet == ASCII_TAB) {
			/* Strip whitespace between name and colon. */
			cs->state = STATE_HDR_WSP;
			return;
		} else {
			cs->state = STATE_ERROR;
			return;
		}
		break;

	case STATE_COLON:
		if (isgraph(octet) && octet < ASCII_DEL) {
			cs->state = STATE_TEXT;
		} else if (octet == ASCII_SPACE || octet == ASCII_TAB) {
			/* Strip whitespace between colon and value. */
			cs->state = STATE_COLON;
			return;
		} else if (octet == ASCII_CR) {
			/* Delay hashing CRLF incase of folded header. */
			cs->state = STATE_CR;
			return;
		} else {
			cs->state = STATE_ERROR;
			return;
		}
		break;

	case STATE_TEXT:
		if (isgraph(octet) && octet < ASCII_DEL) {
			cs->state = STATE_TEXT;
		} else if (octet == ASCII_SPACE || octet == ASCII_TAB) {
			/* Compress horizontal whitespace. */
			cs->state = STATE_WSP;
			return;
		} else if (octet == ASCII_CR) {
			/* Delay hashing CRLF incase of folded header. */
			cs->state = STATE_CR;
			return;
		} else {
			cs->state = STATE_ERROR;
			return;
		}
		break;

	case STATE_WSP:
		if (octet == ASCII_SPACE || octet == ASCII_TAB) {
			/* Compress horizontal whitespace. */
			cs->state = STATE_WSP;
			return;
		} else if (octet == ASCII_CR) {
			/* Delay hashing CRLF incase of folded header. */
			cs->state = STATE_CR;
			return;
		}

		/* Hash a single space before octet. */
		*byte = (uint8_t) ASCII_SPACE;
		(*cs->hash->update)(cs->ctx, byte, sizeof (byte));
		cs->state = STATE_TEXT;
		break;

	case STATE_CR:
		if (octet == ASCII_LF) {
			/* Delay hashing CRLF incase of folded header. */
			cs->state = STATE_LF;
		} else {
			cs->state = STATE_ERROR;
		}
		return;

	case STATE_LF:
		if (isgraph(octet) && octet < ASCII_DEL) {
			cs->state = STATE_HDR;
		} else if (octet == ASCII_SPACE || octet == ASCII_TAB) {
			/* Folded header line, strip CRLF. */
			cs->state = STATE_WSP;
			return;
		} else if (octet == ASCII_CR) {
			/* Do not hash EOH. */
			cs->state = STATE_EOH_CR;
			return;
		} else {
			cs->state = STATE_ERROR;
			return;
		}
		/* Not a folded header line, hash the previous CRLF. */
		(*cs->hash->update)(cs->ctx, crlf, sizeof (crlf));
		break;

	case STATE_EOH_CR:
		if (octet == ASCII_LF) {
			/* Do not hash EOH. */
			cs->state = STATE_END;
		} else {
			cs->state = STATE_ERROR;
		}
		return;

	case STATE_ERROR:
	case STATE_END:
		return;
	}

	*byte = (uint8_t) octet;
	(*cs->hash->update)(cs->ctx, byte, sizeof (byte));
}

/*
 * https://tools.ietf.org/html/rfc6376#section-3.4.3
 *
 *	     	0	1	2
 *	-----------------------------
 *	other	0	0	0
 *   	CR	1	1	1
 *	LF	0	2	0
 *	EOF	X	X	X
 */
static void
canon_body_simple(CanonState *cs, int octet)
{
	uint8_t byte[1];

	if (octet == EOF) {
		if (cs->state == STATE_LF) {
			/* At end-of-file, restore the context before
			 * first trailing CRLF and add a single CRLF.
			 */
			memcpy(cs->ctx, cs->ctx_crlf, cs->hash->sizeof_ctx);
		}
		(*cs->hash->update)(cs->ctx, crlf, sizeof (crlf));
		cs->state = STATE_END;
		return;
	}

	switch (cs->state) {
	case STATE_START:
	case STATE_TEXT:
		if (octet == ASCII_CR) {
			/* Save the context before the first CRLF. We
			 * can have several empty lines between lines
			 * of text, but at EOF we need to rollback any
			 * trailing CRLF into a single CRLF.
			 */
			memcpy(cs->ctx_crlf, cs->ctx, cs->hash->sizeof_ctx);
			cs->state = STATE_CR;
		}
		break;

	case STATE_CR:
		if (octet == ASCII_LF) {
			cs->state = STATE_LF;
		} else if (octet != ASCII_CR) {
			cs->state = STATE_START;
		}
		break;

	case STATE_LF:
		if (octet == ASCII_CR) {
			cs->state = STATE_CR;
		} else {
			cs->state = STATE_START;
		}
		break;
	}

	*byte = (uint8_t) octet;
	(*cs->hash->update)(cs->ctx, byte, sizeof (byte));
}

/*
 * https://tools.ietf.org/html/rfc6376#section-3.4.4
 *
 *	     	0	1	2	3	4
 *	---------------------------------------------
 *	other	1	1	1	1	1
 *   	CR	-	2	2	2	2
 *	LF	-	1	3	1	1
 *	SP/TAB	-	4	4	4	4
 *	EOF	X	X	X	X	X
 */
static void
canon_body_relaxed(CanonState *cs, int octet)
{
	uint8_t byte[1];

	if (octet == EOF) {
		if (cs->state == STATE_LF) {
			/* At end-of-file, restore the context before
			 * the first trailing CRLF.
			 */
			memcpy(cs->ctx, cs->ctx_crlf, cs->hash->sizeof_ctx);
		}
		if (cs->state != STATE_START) {
			/* Ensure a non-empty body ends with a single CRLF. */
			(*cs->hash->update)(cs->ctx, crlf, sizeof (crlf));
		}
		cs->state = STATE_END;
		return;
	}

	switch (cs->state) {
	case STATE_START:
		/* We have a non-empty body. */
		cs->state = STATE_TEXT;
		/*@fallthrough@*/

	case STATE_TEXT:
		if (octet == ASCII_SPACE || octet == ASCII_TAB) {
			/* Compress horizontal whitespace. */
			cs->state = STATE_WSP;
			return;
		} else if (octet == ASCII_CR) {
			/* Save the context before the first CRLF. We
			 * can have several empty lines between lines
			 * of text, but at EOF we need to rollback any
			 * trailing CRLF.
			 */
			memcpy(cs->ctx_crlf, cs->ctx, cs->hash->sizeof_ctx);
			cs->state = STATE_CR;
		}
		break;

	case STATE_WSP:
		if (octet == ASCII_SPACE || octet == ASCII_TAB) {
			/* Compress horizontal whitespace. */
			cs->state = STATE_WSP;
			return;
		} else if (canon_is_crlf(cs, octet)) {
			/* We've ignored all trailing horizontal
			 * whitespace before CRLF.
			 */
			memcpy(cs->ctx_crlf, cs->ctx, cs->hash->sizeof_ctx);
			cs->state = STATE_CR;
			break;
		}

		/* Hash a single space before octet. */
		*byte = (uint8_t) ASCII_SPACE;
		(*cs->hash->update)(cs->ctx, byte, sizeof (byte));
		cs->state = STATE_TEXT;
		break;

	case STATE_CR:
		if (octet == ASCII_SPACE || octet == ASCII_TAB) {
			/* Compress horizontal whitespace. */
			cs->state = STATE_WSP;
			return;
		} else if (octet == ASCII_LF) {
			cs->state = STATE_LF;
		} else if (octet != ASCII_CR) {
			cs->state = STATE_TEXT;
		}
		break;

	case STATE_LF:
		if (octet == ASCII_SPACE || octet == ASCII_TAB) {
			/* Compress horizontal whitespace. */
			cs->state = STATE_WSP;
			return;
		} else if (octet == ASCII_CR) {
			cs->state = STATE_CR;
		} else {
			cs->state = STATE_TEXT;
		}
		break;
	}

	*byte = (uint8_t) octet;
	(*cs->hash->update)(cs->ctx, byte, sizeof (byte));
}

static Canon canon_map[] = {
	{ "ss", canon_identity, canon_body_simple },
	{ "sr", canon_identity, canon_body_relaxed },
	{ "rs", canon_header_relaxed, canon_body_simple },
	{ "rr", canon_header_relaxed, canon_body_relaxed },
	{ "ii", canon_identity, canon_identity },
	{ NULL, NULL, NULL }
};

static int
digest_to_string(uint8_t *digest, size_t dsize, char *hex_string, size_t hsize)
{
	size_t i;
	static const char hex_digit[] = "0123456789abcdef";

	if (dsize == 0 || hsize-1 < 2*dsize)
		return -1;

	for (i = 0; i < dsize; i++) {
		*hex_string++ = hex_digit[(digest[i] >> 4) & 0x0F];
		*hex_string++ = hex_digit[digest[i] & 0x0F];
	}
	*hex_string = '\0';

	return 2*dsize;
}

int
dkim_hash(const char *file, CanonState *cs, CanonFn fn, int flags)
{
	B64 b64;
	int octet;
	uint8_t digest[64];
	char digest_string[129];
	size_t digest_length;

	/* Some files are just message bodies. */
	if ((flags & FLAG_PARSE_HDR) || cs->has_header) {
		/* An email message with both headers and body. */
		cs->state = STATE_START;
		(*cs->hash->init)(cs->ctx);
	} else if ((flags & FLAG_PARSE_BODY)) {
		/* Transitioning from failure to parse headers,
		 * handle as body only.
		 */
		cs->state = STATE_TEXT;
	}

	do {
		octet = fgetc(cs->fp);
		(*fn)(cs, octet);
		if (cs->state == STATE_ERROR) {
			ungetc(octet, cs->fp);
			if ((flags & FLAG_PARSE_HDR) && !cs->has_header) {
				/* Assume body-only content. */
				return EXIT_SUCCESS;
			}
			fprintf(stderr, "%s: parse error (%02X)\n", file, octet);
			return EXIT_FAILURE;
		}
	} while (octet != EOF && cs->state != STATE_END);
	(*cs->hash->final)(digest, cs->ctx);

	if (flags & FLAG_DUMP_HEX) {
		digest_to_string(
			digest, cs->hash->digest_length,
			digest_string, sizeof (digest_string)
		);
	} else {
		b64Reset(&b64);
		digest_length = 0;
		b64EncodeBuffer(
			&b64, digest, cs->hash->digest_length,
			digest_string, sizeof (digest_string), &digest_length
		);
		b64EncodeFinish(
			&b64, digest_string, sizeof (digest_string), &digest_length, 0
		);
	}

	printf("%s %s\n", digest_string, file);

	return EXIT_SUCCESS;
}

int
dkim_file(const char *file, Hash *hash, Canon *canon, int flags)
{
	int ex;
	FILE *fp;
	CanonState *cs;

	if (file[0] == '-' && file[1] == '\0') {
		fp = stdin;
	} else if ((fp = fopen(file, "rb")) == NULL) {
		fprintf(stderr, "%s: %s\n", file, strerror(errno));
		return EXIT_FAILURE;
	}

	if ((cs = canon_create(fp, hash, canon)) == NULL) {
		fprintf(stderr, "out of memory\n");
		return EXIT_FAILURE;
	}

	ex = EXIT_FAILURE;
	if (dkim_hash(file, cs, canon->header, flags | FLAG_PARSE_HDR) == EXIT_SUCCESS
	&&  dkim_hash(file, cs, canon->body, flags | FLAG_PARSE_BODY) == EXIT_SUCCESS)
		ex = EXIT_SUCCESS;

	fclose(fp);
	free(cs);

	return ex;
}

static const char usage[] =
"usage: dkim-hash [-x][-c alg][-h hash] file\n"
"\n"
"-c alg\t\theader/body canonicalisation: ii, ss (*), sr, rs, rr\n"
"\t\twhere i = identity, s = simple, r = relaxed\n"
"-h hash\t\thash function: md5, sha1, sha256 (*), sha512\n"
"-x\t\toutput hash in hex; default is Base64\n"
"\n"
;

int
main(int argc, char **argv)
{
	Hash *hm;
	Canon *cm;
	int ch, ex, flags;

	flags = 0;
	hm = hash_map;
	cm = canon_map;
       	ex = EXIT_SUCCESS;

	while ((ch = getopt(argc, argv, "c:h:x")) != -1) {
		switch (ch) {
		case 'h':
			for (hm = hash_map; hm->name != NULL; hm++) {
				if (strcmp(optarg, hm->name) == 0)
					break;
			}
			if (hm->name == NULL)
				optind = argc+1;
			break;
		case 'c':
			for (cm = canon_map; cm->name != NULL; cm++) {
				if (strcmp(optarg, cm->name) == 0)
					break;
			}
			if (cm->name == NULL)
				optind = argc+1;
			break;
		case 'x':
			flags |= FLAG_DUMP_HEX;
			break;
		default:
			/* Unknown option. */
			optind = argc+1;
			break;
		}
	}

	if (argc < optind) {
		fprintf(stderr, usage);
		return EXIT_FAILURE;
	}

	b64Init();
        if (argc == optind) {
        	ex = dkim_file("-", hm, cm, flags);
        } else {
                for ( ; optind < argc; optind++) {
                        if (dkim_file(argv[optind], hm, cm, flags) != EXIT_SUCCESS)
                        	ex = EXIT_FAILURE;
                }
        }

        return ex;
}
