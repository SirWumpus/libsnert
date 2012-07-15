/*
 * token_bucket.c
 *
 * Copyright 2012 by Anthony Howe. All rights reserved.
 *
 * https://en.wikipedia.org/wiki/Token_bucket
 * http://code.activestate.com/recipes/511490-implementation-of-the-token-bucket-algorithm/ 
 *
 * "The algorithm consists of a bucket with a maximum capacity of N 
 *  tokens, which refills at a rate R tokens per second. Each token 
 *  typically represents a quantity of whatever resource is being 
 *  rate limited (network bandwidth, connections, etc.).
 *
 * "This allows for a fixed rate of flow R with bursts up to N without
 *  impacting the consumer.
 */

#ifndef __com_snert_lib_util_token_bucket_h__
#define __com_snert_lib_util_token_bucket_h__	1

#ifdef __cplusplus
extern "C" {
#endif

#include <com/snert/lib/sys/Time.h>

typedef struct {
	size_t size;		/* bucket size */
	size_t rate;		/* fill rate in tokens / second */
	size_t tokens;		/* number of tokens in bucket always <= to size */
	time_t last;		/* when bucket was last filled */
} token_bucket;

extern int token_bucket_init(token_bucket *tb, size_t size, size_t rate);

extern int token_bucket_drain(token_bucket *tb, size_t drain);

extern void token_bucket_fill(token_bucket *tb);

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_util_token_bucket_h__ */
