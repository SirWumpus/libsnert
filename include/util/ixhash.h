/*
 * ixhash.h
 *
 * http://ixhash.sourceforge.net/index.html
 * ftp://ftp.ix.de/pub/ix/ix_listings/2004/05/checksums
 *
 * Copyright 2010 by Anthony Howe. All rights reserved.
 */

#ifndef __ixhash__
#define __ixhash__	1

#ifdef __cplusplus
extern "C" {
#endif

#include <com/snert/lib/util/md5.h>

/***********************************************************************
 ***
 ***********************************************************************/

typedef void (*ixhash_fn)(md5_state_t *, const unsigned char *, size_t);

/**
 * @param body
 *	A pointer to the initial body chunk of a mail message.
 *
 * @param size
 *	The length of the initial body chunk.
 *
 * @return
 *	Number of occurences.
 */
extern size_t ixhash_count_lf(const unsigned char *body, size_t size);
extern size_t ixhash_count_space_tab(const unsigned char *body, size_t size);
extern size_t ixhash_count_delims_or_abs_url(const unsigned char *body, size_t size);

/**
 * @param body
 *	A pointer to the initial body chunk of a mail message.
 *
 * @param size
 *	The length of the initial body chunk.
 *
 * @return
 *	True if the initial body chunk meets the hash condition.
 */
extern int ixhash_condition1(const unsigned char *body, size_t size);
extern int ixhash_condition2(const unsigned char *body, size_t size);
extern int ixhash_condition3(const unsigned char *body, size_t size);

/**
 * @param md5
 *	A pointer to an MD5 state object.
 *
 * @param body
 *	A pointer to a mail message body chunk.
 *
 * @param size
 *	The length of the mail message body chunk.
 */
extern void ixhash_hash1(md5_state_t *md5, const unsigned char *body, size_t size);
extern void ixhash_hash2(md5_state_t *md5, const unsigned char *body, size_t size);
extern void ixhash_hash3(md5_state_t *md5, const unsigned char *body, size_t size);

/***********************************************************************
 ***
 ***********************************************************************/

#ifdef  __cplusplus
}
#endif

#endif /* __ixhash__ */

