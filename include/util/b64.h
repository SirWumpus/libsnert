/*
 * b64.h
 *
 * Copyright 2006, 2013 by Anthony Howe. All rights reserved.
 */

#ifndef __com_snert_lib_util_b64_h__
#define __com_snert_lib_util_b64_h__	1

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	int _hold;
	int _state;
	char _quantum[5];
} B64;

#define	BASE64_EOF			(-1)
#define BASE64_NEXT			(-2)
#define	BASE64_ERROR			(-3)
#define BASE64_READY			(-4)
#define BASE64_IS_OCTET(x)		(0 <= (x))

/**
 * Initialise the Base64 decoding tables.
 */
extern void b64Init(void);

/**
 * @param b64
 *	Reset the B64 state structure.
 */
extern void b64Reset(B64 *b64);

/**
 * @param b64
 *	A pointer to B64 state structure.
 *
 * @param chr
 *	A character to decode from Base64 to an octet.
 *
 * @return
 *	A decoded octet, BASE64_NEXT if more input is required,
 *	BASE64_ERROR for an invalid input value or decode state,
 *	otherwise BASE64_EOF if the EOF marker has been reached.
 */
extern int b64Decode(B64 *b64, int x);

/**
 * @param b64
 *	A pointer to B64 state structure.
 *
 * @param input
 *	The input buffer to decode.
 *
 * @param in_size
 *	The size of the input buffer.
 *
 * @param output
 *	A pointer to an output buffer to recieved the decoded octets.
 *
 * @param out_size
 *	The size of the output buffer to fill.
 *
 * @param out_length
 *	The length of the decoded output is passed back.
 *
 * @return
 *	Zero if an decoding ended with a full quantum. BASE64_NEXT
 *	if more input is expected to continue decoding.
 */
extern int b64DecodeBuffer(B64 *b64, const char *input, size_t in_size, unsigned char *output, size_t out_size, size_t *out_length);

/**
 * @param inlength
 *	The input length.
 *
 * @return
 *	The size of the output buffer required to encode the input
 *	plus a terminating null byte.
 */
extern size_t b64EncodeGetOutputSize(size_t inlength);

/**
 * @param b64
 *	A pointer to B64 state structure.
 *
 * @param chr
 *	An octet to encode from an octet to a Base64 character.
 *	Specify -1 to terminate the encoding, specify -2 to
 *	terminate an encoding that ends on a full quantum with
 *	"====" (see RFC 2045 section 6.8 paragraphs 8 and 9).
 *
 * @return
 *	A pointer to a string of 4 encoded Base64 characters or
 *	NULL if more input is required to complete the next
 *	quantum.
 */
extern char *b64Encode(B64 *b64, int x);

/**
 * @param b64
 *	A pointer to B64 state structure.
 *
 * @param input
 *	The input buffer to encode.
 *
 * @param in_size
 *	The size of the input buffer.
 *
 * @param output
 *	A pointer to an output buffer to recieved the encoded octets.
 *
 * @param out_size
 *	The size of the output buffer.
 *
 * @param out_length
 *	The length of the encoded output is passed back.
 *
 * @return
 *	Zero if an encoding ended with a full quantum. BASE64_NEXT
 *	if more input is expected to continue encoding.
 */
extern int b64EncodeBuffer(B64 *b64, const unsigned char *input, size_t in_size, char *output, size_t out_size, size_t *out_length);

/**
 * @param b64
 *	A pointer to B64 state structure.
 *
 * @param output
 *	A pointer to an output buffer to recieved the encoded octets.
 *
 * @param out_size
 *	The size of the output buffer.
 *
 * @param out_length
 *	The current length of the contents in the output buffer. The
 *	updated length of the encoded output is passed back.
 *
 * @param mark_end
 *	When true, mark end-of-data by "====" _if_ the encoding ends
 *	on a full quantum. Otherwise leave as is. See RFC 2045 section
 *	6.8 paragraphs 8 and 9.
 *
 * @return
 *	Always zero.
 */
extern int b64EncodeFinish(B64 *b64, char *output, size_t out_size, size_t *out_length, int mark_end);

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_util_b64_h__ */
