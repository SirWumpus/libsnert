/*
 * mime.h
 *
 * RFC 2396
 *
 * Copyright 2007 by Anthony Howe. All rights reserved.
 */

#ifndef __com_snert_lib_util_mime_h__
#define __com_snert_lib_util_mime_h__	1

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <com/snert/lib/util/b64.h>

#ifndef MIME_BUFFER_SIZE
#define MIME_BUFFER_SIZE	1024
#endif

/* Actual ASCII character code vs. the C compiler's
 * interpretation of some special character constants.
 */
#define ASCII_NUL		0x00
#define ASCII_BS		0x08
#define ASCII_TAB		0x09
#define ASCII_LF		0x0A
#define ASCII_VT		0x0B
#define ASCII_FF		0x0C
#define ASCII_CR		0x0D
#define ASCII_SPACE		0x20
#define ASCII_DEL		0x7F

typedef struct {
	unsigned length;
	unsigned char buffer[MIME_BUFFER_SIZE];
} MimeBuffer;

typedef struct mime Mime;
typedef void (*MimeHook)(Mime *, void *);
typedef void (*MimeHookOctet)(Mime *, int, void *);
typedef int (*MimeStateFn)(Mime *, int);

typedef enum {
	MIME_NONE,
	MIME_BASE64,
	MIME_QUOTED_PRINTABLE
} MimeEncoding;

typedef struct {
	B64 b64;
	int is_multipart;
	int decode_state_cr;
	int has_content_type;
	int is_message_rfc822;			/* HACK for uri.c */
	MimeEncoding encoding;
	MimeStateFn source_state;
	MimeStateFn decode_state;
} MimeState;

typedef struct mime_hooks {
	void *data;				/* Data for parser call-backs. */
	MimeHook free;				/* How to clean up hooks & data. */
	MimeHook header;			/* On complete header line. */
	MimeHook body_start;			/* At end of MIME headers, start of MIME body. */
	MimeHook body_finish;			/* At end of MIME body, start of next MIME headers. */
	MimeHook source_flush;			/* When source buffer is flushed. */
	MimeHook decode_flush;			/* When decode buffer is flushed. */
	MimeHookOctet decoded_octet;		/* Each decoded body octet. */
	struct mime_hooks *next;
} MimeHooks;

struct mime {
	/* Private. */
	MimeState state;

	/* Public data. */
	MimeBuffer source;			/* Original encoded source data. */
	MimeBuffer decode;			/* Decoded data based on source. */
	unsigned mime_part_number;		/* Number of boundary lines crossed. */
	unsigned long mime_part_length; 	/* MIME part (headers & body) length. */
	unsigned long mime_body_length; 	/* Encoded MIME body length */
	unsigned long mime_body_decoded_length;	/* Decoded MIME body length. */
	unsigned long mime_message_length;	/* Overall message length. */

	MimeHooks *mime_hook;			/* Link list of Mime hooks. */
};


/**
 * @return
 *	Poitner to a Mime context structure or NULL on error.
 */
extern Mime *mimeCreate(void);

extern void mimeHooksAdd(Mime *m, MimeHooks *hook);

/**
 * @param m
 *	Pointer to a Mime context structure to reset to the start state.
 */
extern void mimeReset(Mime *);

/**
 * @param m
 *	Pointer to a Mime context structre to free.
 */
extern void mimeFree(Mime *);

/**
 * @param m
 *	Pointer to a Mime context structure.
 *
 * @param ch
 *	Next input octet to parse.
 *
 * @return
 *	Zero to continue, otherwise -1 on error.
 */
extern int mimeNextCh(Mime *, int);

#ifdef GONE
/**
 * @param m
 *	Pointer to a Mime context structure.
 *
 * @param ch
 *	Parsed input octet to add to the decode buffer.
 */
extern void mimeDecodeBodyAdd(Mime *m, int ch);
extern void mimeDecodeHeaderAdd(Mime *m, int ch);
#endif

/**
 * @param m
 *	Pointer to a Mime context structure.
 */
extern void mimeSourceFlush(Mime *m);
extern void mimeDecodeFlush(Mime *m);

/**
 * @param m
 *	Pointer to a Mime context structure.
 *
 * @param flag
 *	True if input starts with RFC 5322 message headers;
 *	otherwise input begins directly with body content.
 */
extern void mimeHeadersFirst(Mime *m, int flag);

/**
 * @param m
 *	Pointer to a Mime context structure.
 *
 * @return
 *	True if the parsing is still in the message headers.
 */
extern int mimeIsHeaders(Mime *m);

/**
 * @param octet
 *	A quoted-printable hexadecimal digit character, which are
 *	defined to be upper case only.
 *
 * @return
 *	The value of the hexadecimal digit; otherwise -1 if the
 *	character is not a hexadecimal digit.
 *
 * @see
 *	RFC 2396
 */
extern int qpHexDigit(int x);


#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_util_uri_h__ */
