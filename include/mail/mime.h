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
typedef void (*MimeHook)(Mime *);
typedef void (*MimeHookOctet)(Mime *, int);

struct mime {
	/* Private state. */
	B64 b64;
	int is_multipart;			/* True if message is multipart. */
	int start_of_line;			/* True if at start-of-line. */
	int (*state)(struct mime *, int);	/* Current state. */
	int (*state_newline)(struct mime *, int);
	int (*state_next_part)(struct mime *, int);

	/* Public data. */
	void *mime_data;			/* Data for parser call-backs. */
	MimeBuffer source;			/* Original encoded source data. */
	MimeBuffer decode;			/* Decoded data based on source. */
	unsigned mime_part_number;		/* Number of boundary lines crossed. */
	unsigned long mime_part_length; 	/* MIME part (headers & body) length. */
	unsigned long mime_body_length; 	/* Encoded MIME body length */
	unsigned long mime_body_decoded_length;	/* Decoded MIME body length. */

	/* Parsing call-back hooks. */
	MimeHook mime_flush;			/* Source & decode buffers are flushed. */
	MimeHook mime_header;			/* On complete header line. */
	MimeHook mime_body_start;		/* At end of MIME headers, start of MIME body. */
	MimeHook mime_body_finish;		/* At end of MIME body, start of next MIME headers. */
	MimeHook mime_source_line;		/* On source line or full buffer. */
	MimeHookOctet mime_header_octet;	/* Each header octet. */
	MimeHookOctet mime_decoded_octet;	/* Each decoded body octet. */
};

/**
 * @param data
 *	Pointer to an opaque data structure for use by call-back hooks.
 *
 * @return
 *	Poitner to a Mime context structure or NULL on error.
 */
extern Mime *mimeCreate(void *data);

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

/**
 * @param m
 *	Pointer to a Mime context structure.
 *
 * @param ch
 *	Parsed input octet to add to the decode buffer.
 */
extern void mimeDecodeBodyAdd(Mime *m, int ch);
extern void mimeDecodeHeaderAdd(Mime *m, int ch);

/**
 * @param m
 *	Pointer to a Mime context structure.
 */
extern void mimeBuffersFlush(Mime *);
extern void mimeSourceFlush(Mime *m);
extern void mimeDecodeFlush(Mime *m);

/**
 * @param m
 *	Pointer to a Mime context structure.
 */
extern void mimeNoHeaders(Mime *m);

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
