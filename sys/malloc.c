/*
 * malloc.c
 *
 * Copyright 2009 by Anthony Howe.  All rights reserved.
 */

#include <com/snert/lib/version.h>
#include <stdlib.h>
#include <string.h>

/**
 * Alternative malloc intended for debugging, in particular
 * with Valgrind, which reports certain classes of errors
 * about uninitialised data. Sometimes production code calls
 * for malloc() while debug code wants calloc() like behaviour.
 *
 * @param size
 *	The size in bytes of the area to allocate.
 *
 * @param fill_byte
 *	A byte value to be used to fill in the allocated area.
 *	with. Specify -1 to ignore.
 *
 * @param flags
 *	A bit-wise OR of assort ALT_MALLOC_ flags.
 */
void *
alt_malloc(size_t size, int fill_byte, unsigned flags)
{
	void *mem;

	if ((mem = malloc(size)) != NULL) {
		if (0 <= fill_byte)
			(void) memset(mem, fill_byte, size);
	}

	if ((flags & ALT_MALLOC_ABORT) && mem == NULL)
		abort();

	return mem;
}

void *
alt_calloc(size_t num_elements, size_t element_size, unsigned flags)
{
	return alt_malloc(num_elements * element_size, 0, flags);
}
