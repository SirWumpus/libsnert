/*
 * victor.h
 *
 * http://en.wikipedia.org/wiki/VIC_cipher
 *
 * Copyright 2010 by Anthony Howe. All rights reserved.
 */

#ifndef __victor__
#define __victor__	1

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************************
 ***
 ***********************************************************************/

typedef struct {
	/* Public */
	char *key;		/* Alpha-numeric upto 36 characters. */
	char *seed;		/* ASCII number string, "1953". */
	char *freq7;		/* Seven most "frequent" alpha-numeric, eg "ESTONIA". */

	/* Private */
	char chain[51];		/* Chain addition table based on seed; 5x10 */
	char columns[11];	/* Digit order based on last row of chain table. */
	char table[3][38];	/* 1st row is the alphabet seeded with key.
				 * 2nd and 3rd rows are the ASCII digit codes
				 * for each glyph in the straddling checkerboard.
				 */
} Victor;

extern void victor_dump_alphabet(FILE *fp, char table[3][38]);
extern void victor_dump_table(FILE *fp, char table[3][38]);

/**
 * @param vic
 *	A pointer to a Victor structure with the key and seed member
 *	defined. Defingin the freq7 member is optional.
 *
 * @return
 *	Zero on success.
 */
extern int victor_init(Victor *vic);

/**
 * @param vic
 *	A pointer to a Victor structure that has been initialised with
 *	victor_init();
 *
 * @param message
 *	A message string to encode.
 *
 * @return
 *	A pointer to an allocated C string containing the transformed
 *	message. It is the caller's responsiblity to free() this pointer.
 */
extern char *victor_encode(Victor *vic, const char *message);

/**
 * @param vic
 *	A pointer to a Victor structure that has been initialised with
 *	victor_init();
 *
 * @param message
 *	A message string to decode.
 *
 * @return
 *	A pointer to an allocated C string containing the transformed
 *	message. It is the caller's responsiblity to free() this pointer.
 */
extern char *victor_decode(Victor *vic, const char *message);

/***********************************************************************
 ***
 ***********************************************************************/

#ifdef  __cplusplus
}
#endif

#endif /* __victor__ */

