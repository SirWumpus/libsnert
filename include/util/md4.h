/*
 * md4.h
 *
 * Source taken from RFC 1320. Modified by Anthony Howe for C portability.
 */

/* Copyright (C) 1991-2, RSA Data Security, Inc. Created 1991. All
   rights reserved.

   License to copy and use this software is granted provided that it
   is identified as the "RSA Data Security, Inc. MD4 Message-Digest
   Algorithm" in all material mentioning or referencing this software
   or this function.

   License is also granted to make and use derivative works provided
   that such works are identified as "derived from the RSA Data
   Security, Inc. MD4 Message-Digest Algorithm" in all material
   mentioning or referencing the derived work.

   RSA Data Security, Inc. makes no representations concerning either
   the merchantability of this software or the suitability of this
   software for any particular purpose. It is provided "as is"
   without express or implied warranty of any kind.

   These notices must be retained in any copies of any part of this
   documentation and/or software.
 */

#ifndef __com_snert_lib_util_md4_h__
#define __com_snert_lib_util_md4_h__	1

#ifdef __cplusplus
extern "C" {
#endif

#if HAVE_INTTYPES_H
# include <inttypes.h>
#else
# if HAVE_STDINT_H
# include <stdint.h>
# endif
#endif

typedef struct {
	uint32_t state[4]; 	/* state (ABCD) */
	uint32_t count[2]; 	/* number of bits, modulo 2^64 (lsb first) */
	uint8_t buffer[64];	/* input buffer */
} md4_state_t;

extern void md4_init(md4_state_t *state);

extern void md4_append(md4_state_t *state, uint8_t *buffer, unsigned length);

extern void md4_finish(md4_state_t *state, uint8_t digest[16]);

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_util_md4_h__ */
