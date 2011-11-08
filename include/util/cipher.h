/*
 * cipher.c
 *
 * An API implementing pen & paper cipher techniques.
 *
 * http://users.telenet.be/d.rijmenants/
 *
 * Copyright 2010, 2011 by Anthony Howe. All rights reserved.
 */

#ifndef __com_snert_lib_util_cipher_h__
#define __com_snert_lib_util_cipher_h__	1

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************************
 ***
 ***********************************************************************/

/**
 * @param fp
 *	An output FILE pointer.
 *
 * @param table
 *	A conversion table for CT28 or CT37.
 */
extern void cipher_dump_alphabet(FILE *fp, cipher_ct table);

/**
 * @param fp
 *	An output FILE pointer.
 *
 * @param table
 *	A conversion table CT28 or CT37 to output in straddling
 *	checkerboard format.
 */
extern void cipher_dump_ct(FILE *fp, cipher_ct table);

/**
 * @param fp
 *	An output FILE pointer.
 *
 * @param chain
 *	A chain addition table.
 */
extern void cipher_dump_chain(FILE *fp, const char *chain);

/**
 * @param fp
 *	An output FILE pointer.
 *
 * @param text
 *	A numeric C string to output in space separated
 *	groups of 5 characters.
 */
extern void cipher_dump_grouping(FILE *fp, int grouping, const char *text);

/**
 * @param fp
 *	An output FILE pointer.
 *
 * @param num_key
 *	A numeric C string representing the transposition key.
 *
 * @param source
 *	A numeric C string representing the transposition table.
 */
extern void cipher_dump_transposition(FILE *fp, const char *num_key, const char *source);

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
 * @param source
 *	A numeric C string of 10 digits.
 *
 * @param out
 *	An output buffer of at least 11 bytes, that will contain
 *	the digits from 0 to 9 inclusive corresponding to the
 *	order of digits from source string. The buffer will be
 *	NUL terminated.
 */
extern void cipher_digit_order(const char source[10], char out[11]);

/**
 * @param source
 *	A C string of 10 alphabetic letters.
 *
 * @param out
 *	An output buffer of at least 11 bytes, that will contain
 *	the digits from 0 to 9 inclusive corresponding to the
 *	order of letters from source string. The buffer will be
 *	NUL terminated.
 */
extern void cipher_alpha_order(const char source[10], char out[11]);

/**
 * @param num_key
 *	A numeric C string representing the transposition key.
 *
 * @param in
 *	A C string containing the message.
 *
 * @return
 *	A dynamic C string of the message after applying a
 *	simple column transposition encoding. It is the caller's
 *	responsibility to free this memory when done.
 */
extern char *cipher_simple_transposition_encode(const char *num_key, const char *in);

/**
 * @param num_key
 *	A numeric C string representing the transposition key.
 *
 * @param in
 *	A C string containing the message.
 *
 * @return
 *	A dynamic C string of the message after applying a
 *	simple column transposition decoding. It is the caller's
 *	responsibility to free this memory when done.
 */
extern char *cipher_simple_transposition_decode(const char *num_key, const char *in);

/**
 * @param num_key
 *	A numeric C string representing the transposition key.
 *
 * @param in
 *	A C string containing the message.
 *
 * @return
 *	A dynamic C string of the message after applying a
 *	disrupted column transposition encoding.
 */
extern char *cipher_disrupted_transposition_encode(const char *num_key, const char *in);

/**
 * @param num_key
 *	A numeric C string representing the transposition key.
 *
 * @param in
 *	A C string containing the message.
 *
 * @return
 *	A dynamic C string of the message after applying a
 *	disrupted column transposition decoding.
 */
extern char *cipher_disrupted_transposition_decode(const char *num_key, const char *in);

/**
 * @param ct_size
 *	The conversion table size, 28 or 37. Used to select
 *	both the alphabet and frequent English letter list.
 *
 * @param key
 *	A C string for the key in the conversion table alphabet.
 *	Used to scramble alphabet order. Can be NULL for the
 *	default English order.
 *
 * @parma ct_out
 *	A buffer at least ct_size+1 bytes in size to hold the
 *	reordered conversion table alphabet.
 *
 * @return
 *	Zero on success, otherwise non-zero on error.
 *
 * @note
 *	Use cipher_alphabet_fill() to initialise the 1st row
 *	of a cipher_ct table and cipher_ct_fill() to initialise
 *	the remainder of the table based on the alphabet.
 */
extern int cipher_alphabet_fill(int ct_size, const char *key, char *ct_out);

/**
 * @param order
 *	A numeric C string of 10 digits 0 to 9 used as a key
 *	to initialise the conversion table.
 *
 * @param table
 *	A pointer to a cipher_ct, where 1st row is the alphabet
 *	seeded with frequent English letters and a key. The 2nd
 *	and 3rd rows will be initialised with the ASCII digit
 *	codes (or space) for each glyph based on a straddling
 *	checkerboard.
 *
 * @return
 *	Zero on success, otherwise non-zero on error.
 *
 * @note
 *	Use cipher_alphabet_fill() to initialise the 1st row
 *	of the table.
 */
extern int cipher_ct_fill(const char *order, cipher_ct table);

/**
 * @param ct_size
 *	The conversion table size, 28 or 37. Used to select
 *	both the alphabet and frequent English letter list.
 *
 * @param key
 *	A C string for the key in the conversion table alphabet.
 *	Used to scramble alphabet order. Can be NULL for the
 *	default English order.
 *
 * @param order
 *	A numeric C string of 10 digits 0 to 9 used as a key
 *	to initialise the conversion table.
 *
 * @param table
 *	A pointer to a cipher_ct, where 1st row is the alphabet
 *	seeded with frequent English letters and a key. The 2nd
 *	and 3rd rows will be initialised with the ASCII digit
 *	codes (or space) for each glyph based on a straddling
 *	checkerboard.
 *
 * @return
 *	Zero on success, otherwise non-zero on error.
 */
extern int cipher_ct_init(int ct_size, const char *key, const char *order, cipher_ct table);

/**
 * @param table
 *	Conversion table, either CT28 or CT37.
 *
 * @param message
 *	A C string of the message.
 *
 * @return
 *	A dynamic C string of the message converted to a numeric
 *	string. It is the caller's responsibility to free this
 *	memory when done.
 */
extern char *cipher_char_to_code(cipher_ct table, const char *message);

/**
 * @param table
 *	Conversion table for CT28 or CT37.
 *
 * @param out
 *	A numeric C string that is converted back into an
 *	alpha-numeric string in place.
 */
extern void cipher_code_to_char(cipher_ct table, char *out);

/**
 * @param key_mask
 *	A numeric C string.
 *
 * @param out
 *	A numeric C string that is modified in place using
 *	column addition MOD 10.
 */
extern void cipher_mask_code(const char *key_mask, char *out);

/**
 * @param alphabet
 *	A C string for a CT28 alphabet, possibly reordered.
 *
 * @param text
 *	A C string of the message text.
 *
 * @return
 *	A dynamic C string where spaces have been converted to
 *	plus-sign (+) and numbers have been converted into to
 *	alpha, using slash (/) as a numeric shift. It is the
 *	responsibility of the caller to free this memory when
 *	done.
 */
extern char *cipher_ct28_normalise(const char *alphabet, const char *text);

/**
 * @param alphabet
 *	A C string for a CT28 alphabet, possibly reordered.
 *
 * @param text
 *	A normalised C string of the message text. The text
 *	is de-normalised, in place, converting plus-sign (+)
 *	to space and slash (/) delimited alpha back to numeric.
 *
 * @return
 *	Return the C string text argument.
 */
extern char *cipher_ct28_denormalise(const char *alphabet, char *text);

/***********************************************************************
 ***
 ***********************************************************************/

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_util_cipher_h__ */
