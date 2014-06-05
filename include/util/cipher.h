/*
 * cipher.h
 *
 * Copyright 2014 by Anthony Howe. All rights released.
 */

#ifndef __com_snert_lib_util_cipher_h__
#define __com_snert_lib_util_cipher_h__	1

/***********************************************************************
 ***
 ***********************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	int length;
	char *set;
	char *code[2];
} cipher_ct;

extern cipher_ct cipher_ct28;
extern cipher_ct cipher_ct37;
extern cipher_ct cipher_ct46;
extern cipher_ct cipher_ct106;

extern void cipher_set_debug(int level);

extern void cipher_dump_grouped(FILE *fp, int width, const char *text, int skip_ws);

/**
 * @param fp
 *      An output FILE pointer.
 *
 * @param key
 *      A numeric C string representing the transposition key.
 *
 * @param text
 *      A numeric C string representing the transposition table.
 */
extern void cipher_dump_transposition(FILE *fp, const char *key, const char *text);

/**
 * @param seed_number
 *	A numeric C string.
 *
 * @param buffer
 *	A pointer to a buffer of size bytes. The seed will be
 *	copied into buffer and then chain addition MOD 10 will
 *	be used to fill the remainder of the buffer. The buffer
 *	will be NUL terminated.
 *
 * @param size
 *	The size in bytes of buffer.
 *
 * @return
 *	Zero (0) on succes or non-zero on error.
 */
extern int cipher_chain_add(const char *seed_number, char *buffer, size_t size);

/**
 * @param in
 *      A C string of up to 255 bytes in length.
 *
 * @param out
 *      An output buffer that starts with the length N of the
 *      input string followed by N octets. Each octet in the
 *      output array contains the index by which the input
 *      string should be read according to the ordinal order
 *      of the input.
 *
 *      Examples assuming ASCII:
 *
 *          B A B Y L O N 5             input
 *          2 1 3 7 4 6 5 0             ordinal order
 *          7 1 0 2 4 6 5 3             index of ordinal
 *
 *          H E L L O W O R L D         input
 *          2 1 3 4 6 9 7 8 5 0         ordinal order
 *          9 1 0 2 3 8 4 6 7 5         index of ordinal
 *
 * @return
 *	Zero on success.
 */
extern void cipher_index_order(const char *in, int out[256]);

extern int cipher_seq_write();
extern int cipher_seq_read();

extern void cipher_columnar_transposition(const char *key, const char *in, char *out, size_t out_len, int (*seq_fn)());
extern void cipher_disrupted_transposition(const char *key, const char *in, char *out, size_t out_len, int (*seq_fn)())

extern size_t cipher_ct_encode(cipher_ct *ct, FILE *fp, char *out, size_t length);
extern void cipher_ct_decode(cipher_ct *ct, FILE *fp, char *in);


#ifdef  __cplusplus
}
#endif

/***********************************************************************
 ***
 ***********************************************************************/

#endif /* __com_snert_lib_util_cipher_h__ */
