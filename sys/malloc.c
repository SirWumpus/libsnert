/*
 * malloc.c
 *
 * Copyright 2009, 2013 by Anthony Howe.  All rights reserved.
 */

#include <com/snert/lib/version.h>

#include <stdlib.h>
#include <string.h>

#ifdef DEBUG_MALLOC
# include <com/snert/lib/util/DebugMalloc.h>
#endif

void
alt_free(void *mem, unsigned flags)
{
	free(mem);
}

/**
 * Alternative malloc intended for debugging, in particular
 * with Valgrind, which reports certain classes of errors
 * about uninitialised data. Sometimes production code calls
 * for malloc() while debug code wants calloc() like behaviour.
 *
 * @param size
 *	The size in bytes of the area to allocate.
 *
 * @param flags
 *	A bit-wise OR of assort ALT_MALLOC_ flags.
 */
void *
alt_malloc(size_t size, unsigned flags)
{
	void *mem;

	if ((mem = malloc(size)) == NULL && (flags & ALT_MALLOC_ABORT))
		abort();

	if (flags & ALT_MALLOC_FILL)
		(void) memset(mem, flags & ALT_MALLOC_BYTE_MASK, size);

	return mem;
}

void *
alt_calloc(size_t num_elements, size_t element_size, unsigned flags)
{
	return alt_malloc(num_elements * element_size, flags);
}

void *
alt_realloc(void *orig, size_t size, unsigned flags)
{
	void *mem;

	if ((mem = alt_calloc(size, 1, flags)) != NULL) {
		(void) memcpy(mem, orig, size);
		free(orig);
	}

	return mem;
}

#define is_power_two(x)		(((x) != 0) && !((x) & ((x) - 1)))

void *
alt_aligned_alloc(size_t alignment, size_t size, unsigned flags)
{
	if (is_power_two(alignment) && (size / alignment) * alignment == size)
		return alt_malloc(size, flags);
	return NULL;
}
