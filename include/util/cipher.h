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

/*
 * Eight most frequent characters in English are "SENORITA".
 * Allows for inclusion of two punctuation characters in the
 * key table, one of which is used for numeric shift. Used
 * with CT28.
 */
#define FREQUENT8		"SENORITA"

/*
 * Seven most frequent characters in English are "ESTONIA".
 * Allows for inclusion of decimal digits and one punctuation
 * character in the key table. Used with CT37.
 */
#define FREQUENT7		"ESTONIA"

/*
 * Note that both CT28 and CT37 alphabets are a subset of the
 * Base64 invariant character set by design. This allows for
 * encrypted messages to appear as though they were Base64
 * encoded.
 */
#define CT28			FREQUENT8 "BCDFGHJKLMPQUVWXYZ+/"
#define CT37			FREQUENT7 "BCDFGHJKLMPQRUVWXYZ0123456789/"

/**
 * 1st row is the alphabet seeded with frequent letters and the key.
 * 2nd and 3rd rows are the ASCII digit codes for each glyph in the
 * straddling checkerboard.
 */
typedef char (cipher_ct)[3][sizeof (CT37)];

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
 *	A C string to output in space separated groups.
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
 * @param chain
 *	A numeric C string representing the chain addition to be
 *	inverted, in place.
 */
extern void cipher_chain_invert(char *chain);

/**
 * @param in
 *	A C string of up to 255 bytes in length.
 *
 * @return
 *	An output buffer that starts with the length N of the
 *	input string followed by N octets. Each octet in the
 *	output array corresponds to the character set ordering
 *	of the input array.
 *
 * 	Examples assuming ASCII:
 *
 *	    B A B Y L O N 5		input
 *	    2 1 3 7 4 6 5 0		ordinal order
 *
 *	    H E L L O W O R L D		input
 *	    2 1 3 4 6 9 7 8 5 0		ordinal order
 *
 * @see
 *	cipher_index_order()
 */
extern unsigned char *cipher_ordinal_order(const char *in);

/**
 * @param in
 *	A C string of up to 255 bytes in length.
 *
 * @return
 *	An output buffer that starts with the length N of the
 *	input string followed by N octets. Each octet in the
 *	output array contains the index by which the input
 *	string should be read according to the ordinal order
 *	of the input.
 *
 * 	Examples assuming ASCII:
 *
 *	    B A B Y L O N 5		input
 *	    2 1 3 7 4 6 5 0		ordinal order
 *	    7 1 0 2 4 6 5 3		index of ordinal
 *
 *	    H E L L O W O R L D		input
 *	    2 1 3 4 6 9 7 8 5 0 	ordinal order
 *	    9 1 0 2 3 8 4 6 7 5		index of ordinal
 *
 * @see
 *	cipher_ordinal_order()
 */
extern unsigned char *cipher_index_order(const char *in);

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
extern char *cipher_columnar_transposition_encode(const char *num_key, const char *in);

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
extern char *cipher_columnar_transposition_decode(const char *num_key, const char *in);

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
 * @param order
 *	An array of 11 octets; a length (10) followed by 10
 *	octets specifying an ordinal ordering.
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
 * @see
 *	cipher_ordinal_order()
 */
extern int cipher_ct_init(int ct_size, const unsigned char *order, cipher_ct table);

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
