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

#include <com/snert/lib/version.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <com/snert/lib/util/token_bucket.h>

/***********************************************************************
 ***
 ***********************************************************************/

int
token_bucket_init(token_bucket *tb, size_t size, size_t rate)
{
	if (tb == NULL)
		return -1;

	tb->tokens = size;
	tb->size = size;
	tb->rate = rate;
	tb->last = 0;

	return 0;
}	

int
token_bucket_drain(token_bucket *tb, size_t drain)
{
	if (drain <= tb->tokens) {
		tb->tokens -= drain;
		return 0;
	}

	return -1;
}

void
token_bucket_fill(token_bucket *tb)
{
	time_t now;
	size_t delta;

	(void) time(&now);

	if (tb->tokens < tb->size) {
		/* How many tokens to add since the last fill. */
		delta = tb->rate * (now - tb->last);

		/* Never add more token than the bucket can hold. */
		tb->tokens = delta < tb->size - tb->tokens ? tb->tokens + delta : tb->size;
	}

	tb->last = now;
}
