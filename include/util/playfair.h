/*
 * playfair.h
 *
 * http://en.wikipedia.org/wiki/Playfair_cipher
 *
 * Copyright 2010 by Anthony Howe. All rights reserved.
 */

#ifndef __playfair__
#define __playfair__	1

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************************
 ***
 ***********************************************************************/

typedef struct {
	char table[65];
} Playfair;

/**
 * @param fp
 *	A pointer to an output FILE stream.
 *
 * @param message
 *	A pointer to a C string to be written out as space separated
 *	digraphs.
 */
extern void playfair_print(FILE *fp, const char *message);

/**
 * @param fp
 *	A pointer to an output FILE stream.
 *
 * @param pf
 *	A key table previously generated by playfair_build() to be
 *	written out as a 5x5 or 6x6 space separated grid.
 */
extern void playfair_dump(FILE *fp, Playfair *pf);

/**
 * @param alphabet
 *	Either a 25 or 36 alphabet. A classic Playfair 25 character
 * 	alphabet excludes either I or J (they are considered equivalent).
 *	A 36 character alphabet consists of all alpha and digits. The
 *	alphabet order does not have to be sequential.
 *
 * @param key
 *	The cipher key. Upto 25 or 36 characters will be used depending
 *	on the alphabet.
 *
 * @param pf
 *	The generated key table based on the key and remaining alphabet.
 *
 * @return
 *	Zero on success.
 */
extern int playfair_init(Playfair *pf, const char *alphabet, const char *key);

/**
 * @param pf
 *	A key table previously generated by playfair_init().
 *
 * @param message
 *	A message string to encode.
 *
 * @return
 *	A pointer to an allocated C string containing the transformed
 *	message. It is the caller's responsiblity to free() this pointer.
 */
extern char *playfair_encode(Playfair *pf, const char *message);

/**
 * @param pf
 *	A key table previously generated by playfair_init().
 *
 * @param message
 *	A message string to decode.
 *
 * @return
 *	A pointer to an allocated C string containing the transformed
 *	message. It is the caller's responsiblity to free() this pointer.
 */
extern char *playfair_decode(Playfair *pf, const char *message);

/***********************************************************************
 ***
 ***********************************************************************/

#ifdef  __cplusplus
}
#endif

#endif /* __playfair__ */

