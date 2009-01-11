/*
 * Base64.h
 *
 * Copyright 2001, 2005 by Anthony Howe.  All rights reserved.
 */

#ifndef __com_snert_lib_util_Base64_h__
#define __com_snert_lib_util_Base64_h__	1

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <com/snert/lib/type/Object.h>

#define BASE64_EOF		(-1)
#define BASE64_NEXT		(-2)
#define BASE64_ERROR		(-3)

typedef struct base64 {
	OBJECT_OBJECT;

	/**
	 * @param self
	 *	This object.
	 *
	 * @param chr
	 *	A character to decode from Base64 to an octet.
	 *
	 * @return
	 *	A decoded octet, BASE64_NEXT if more input is required,
	 *	BASE64_ERROR for an invalid input value or decode state,
	 *	otherwise BASE64_EOF if the EOF marker has been reached.
	 *
	 */
	int (*decode)(struct base64 *self, int chr);

	/**
	 * @param self
	 *	This object.
	 *
	 * @param s
	 *	The encoded source buffer.
	 *
	 * @param slength
	 *	The length of the source buffer.
	 *
	 * @param t
	 *	A pointer to an allocated buffer of decoded octets is
	 *	passed back. The buffer passed back must be freed by
	 *	the caller.
	 *
	 * @param tlength
	 *	The length of the decoded buffer that is passed back.
	 *
	 * @return
	 *	Zero if a decoding ended with a full quantum, BASE64_NEXT
	 *	if more input is expected to continue decoding, BASE64_EOF
	 *	if the EOF marker was seen, otherwise BASE64_ERROR for a
	 *	buffer allocation error.
 	 */
	int (*decodeBuffer)(struct base64 *self, const char *s, long slength, char **t, long *tlength);

	/**
	 * @param self
	 *	This object.
	 *
	 * @param s
	 *	The source buffer to encode. NULL if no further input
	 *	is available and the encoder must terminate the remaining
	 *	octets.
	 *
	 * @param slength
	 *	The length of the source buffer.
	 *
	 * @param t
	 *	A pointer to an allocated buffer of encoded octets is
	 *	passed back. The buffer passed back must be freed by
	 *	the caller.
	 *
	 * @param tlength
	 *	The length of the encoded buffer that is passed back.
	 *
	 * @param eof
	 *	True if buffer should be terminated with remaining octets
	 *	and padding.
	 *
	 * @return
	 *	Zero if a encoding ended with a full quantum, BASE64_NEXT
	 *	if more input is expected to continue encoding; otherwise
	 *	BASE64_ERROR for a buffer allocation error.
 	 */
	int (*encodeBuffer)(struct base64 *self, const char *s, long slength, char **t, long *tlength, int eof);

	/**
	 * Abandon the current encoding/decoding state and reset.
	 *
	 * @param self
	 *	This object.
	 */
	void (*reset)(struct base64 *self);

	/**
	 * @param self
	 *	This object.
	 *
	 * @param padding
	 *	Set the padding character to something other than the default
	 *	equals-sign. The padding character must be an invariant symbol
	 *	not in the Base64 character set (see RFC 2045):
	 *
	 *		" % & ' ( ) * , - . : ; < = > ? _
	 *
	 * @return
	 *	Zero on success, other -1 for an invalid character such a
	 *	Base64 character (A-Z a-z 0-9 + /).
	 */
	int (*setPadding)(struct base64 *self, int pad);

	/*
	 * Private
	 */
	int _state;
	int _hold;
	int _pad;
} *Base64;

extern Base64 Base64Create(void);

extern void Base64Destroy(void *self);
extern void Base64Reset(Base64 self);
extern int Base64Decode(Base64 self, int chr);
extern int Base64DecodeBuffer(Base64 self, const char *s, long slength, char **t, long *tlength);
extern int Base64EncodeBuffer(Base64 self, const char *s, long slength, char **t, long *tlength, int eof);

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_util_Base64_h__ */
