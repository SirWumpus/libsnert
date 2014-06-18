/*
 * TextHash.c
 *
 * Copyright 2006 by Anthony Howe. All rights reserved.
 */

#include <com/snert/lib/version.h>
#include <com/snert/lib/util/Text.h>

/***********************************************************************
 ***
 ***********************************************************************/

/**
 * <p>
 * Compute the hash value of a string.
 * </p>
 *
 * @param hash
 *	A previously computed hash value.
 *
 * @param s
 *	A string to hash with the previous hash.
 *
 * @return
 *	A new hash value.
 */
unsigned long
TextHash(unsigned long hash, const char *s)
{
	if (hash == 0)
		hash = 5381;

	while (*s != '\0')
 		/* D.J. Bernstien Hash version 2 (+ replaced by ^). */
		hash = ((hash << 5) + hash) ^ *s++;

	return hash;
}

