/*
 * Memory.c
 *
 * Copyright 2001, 2004 by Anthony Howe.  All rights reserved.
 *
 * A generic memory manager that can be used to allocate memory
 * from larger memory blocks allocated from the heap or shared
 * memory. This is a fairly simple technique and is patterned
 * after K&R2 page 185.
 */

#define NDEBUG

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <errno.h>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <com/snert/lib/util/Memory.h>

struct memory {
	long size;		/* Size of chunk, includes this struct. */
};

struct memory_header {
	struct memory *first;	/* Base of memory being partitioned. */
	int freeFirst;		/* True if we should free the memory block.*/
	long size;		/* Overall size of memory block. */
};

#define MINIMUM_CHUNK_SIZE	(long)(sizeof (struct memory))

#define CHUNK_ALIGN_TYPE	long

/*
 * The alignment mask must be a power of two.
 */
#define CHUNK_ALIGN_MASK	(long)(sizeof (CHUNK_ALIGN_TYPE) - 1)

/*
 * Make sure we encode the chunk size as a 2's complement number.
 */
#if ~1+1 == -1
# define CHUNK_NEGATE(s)		(-(s))
#elif ~1 == -1
# define CHUNK_NEGATE(s)		(~(s)+1)
#else
# error "Neither 1's complement nor 2's complement system"
#endif

/*
 * A 2's complement number has an interesting property: given the 
 * requested size of a chunk, negate the number, mask off the 
 * alignment size (assuming a power of two), then negative number
 * once more to get the requested size round to the next multiple
 * of the alignment size.
 */
#define CHUNK_ALIGN(s)		CHUNK_NEGATE(CHUNK_NEGATE(s) & ~CHUNK_ALIGN_MASK)

/*
 * The sign bit of the memory chunk size is used to indiciate a free
 * or allocated chunk: a positive number for a free chunk that is a
 * multiple of the alignment size and a negative numebr for an 
 * allocated chunk with the requested size.
 */
#define CHUNK_ALIGNED_SIZE(x)	((x)->size < 0 ? CHUNK_NEGATE((x)->size & ~CHUNK_ALIGN_MASK) : (x)->size)

#define CHUNK_REAL_SIZE(x)	((x)->size < 0 ? CHUNK_NEGATE((x)->size) : (x)->size)

#define CHUNK_NEXT(x)		((struct memory *)((char *)(x) + CHUNK_ALIGNED_SIZE(x)))

#define CHUNK_IS_FREE(x)	(MINIMUM_CHUNK_SIZE <= (x)->size)

#define CHUNK_IS_USED(x)	((x)->size <= -MINIMUM_CHUNK_SIZE)

#define CHUNK_ENCODE_SIZE(s)	CHUNK_NEGATE((s)+MINIMUM_CHUNK_SIZE)

#define CHUNK_SENTINEL_BYTE	'#'

/*
 * Initialise a large block of memory from which we will allocate
 * smaller chunks. Return a pointer to an opaque memory header on
 * success, otherwise null on error.
 *
 * Previous versions of this code kept the memory header within the
 * provided memory block, which works fine, but is harder to verify
 * for consistency, especially of the given block is shared memory
 * that could be changed by several processes for good or ill.
 *
 * By using an independant header allocated from the process' heap,
 * we can improve the realiablity of verification by cross checking
 * with values kept in different memory spaces.
 */
void *
MemoryCreate(void *block, long size)
{
	struct memory_header *head;
	
	if (size < MINIMUM_CHUNK_SIZE) {
		errno = EINVAL;
		return (void *) 0;
	}

	size = CHUNK_ALIGN(size);

	head = malloc(sizeof *head);
	if (head == (struct memory_header *) 0)
		return (void *) 0;

	head->freeFirst = 0;
	if (block == (void *) 0) {
		if ((block = malloc(size)) == (void *) 0) {
			free(head);
			return (void *) 0;
		}
		
		head->freeFirst = 1;
	}
		
	head->first = (struct memory *) block;
	head->size = head->first->size = size;

	return (void *) head;
}

/*
 * Release the memory header object.
 */
void
MemoryDestroy(void *head)
{
	if (((struct memory_header *) head)->freeFirst)
		free(((struct memory_header *) head)->first);
	free(head);
}

/*
 * Return the size of either an allocated or unallocated chunk of memory.
 * The size returned does not count the memory accounting structure.
 */
long
MemorySizeOf(void *chunk)
{
	if (chunk == (void *) 0)
		return 0;

	return CHUNK_REAL_SIZE((struct memory *) chunk - 1) - MINIMUM_CHUNK_SIZE;
}

/*
 * Return the total amount of free space remaining for the memory
 * block or zero (0).
 */
long
MemoryAvailable(void *header)
{
	long space;
	struct memory *last, *here;
	struct memory_header *head;

	if (header == (void *) 0)
		return 0;

	head = (struct memory_header *) header;
	last = (struct memory *) ((char *) head->first + head->size);

	for (space = 0, here = head->first; here < last; ) {
		if (MINIMUM_CHUNK_SIZE <= here->size)
			space += here->size - MINIMUM_CHUNK_SIZE;

		here = CHUNK_NEXT(here);
	}

	return space;
}

/*
 * Verify the internal consistency of the allocated and freed chunks
 * within the memory block being managed. Also adjacent free blocks
 * are coalesed into one. Return the overall size of the memory block,
 * otherwise 0 on error. Also set errno to EFAULT is the consistency
 * check fails.
 */
long
MemoryVerifySize(void *header)
{
	long size;
	struct memory_header *head;
	struct memory *here, *next, *last;

	if (header == (void *) 0)
		return 0;

	head = (struct memory_header *) header;
	last = (struct memory *) ((char *) head->first + head->size);

#if !defined(NDEBUG)
printf("MemoryVerifySize last=%lx\n", last);		
#endif
	for (here = head->first; ; ) {		
		size = CHUNK_REAL_SIZE(here);
#if !defined(NDEBUG)
printf("MemoryVerifySize here=%lx here->size=%ld align-size=%ld\n", here, here->size, CHUNK_ALIGNED_SIZE(here));		
#endif
		/* Test for an invalid size. */
		if (size < MINIMUM_CHUNK_SIZE) {
			errno = EFAULT;
			return 0;
		}

		/* Test for chunk overflow. */
		if ((size & CHUNK_ALIGN_MASK) && ((char *) here)[size] != CHUNK_SENTINEL_BYTE) {
			errno = EFAULT;
			return 0;
		}

		next = CHUNK_NEXT(here);
#if !defined(NDEBUG)
printf("MemoryVerifySize next=%lx\n", next);		
#endif		
		/* Test if we're out of bounds or an uneven chunk size that
		 * might cause segmentation fault later on the next iteration.
		 */
		if (next < here || last < next || (CHUNK_ALIGNED_SIZE(here) & CHUNK_ALIGN_MASK) != 0) {
			errno = EFAULT;
			return 0;
		}

		/* Have we reached the exact end of the memory block? */
		if (last == next)
			break;

		if (CHUNK_IS_FREE(here) && CHUNK_IS_FREE(next))
			/* Join them into one. */
			here->size += next->size;
		else
			here = next;
	}

	/* Did we leave the loop exactly where we expect to? If so then the
	 * allocated and freed memory chunk sizes all add up correctly to
	 * the memory block size.
	 */
	return head->size;
}

/*
 * Return an allocated chunk of memory at least size bytes long from the
 * the given block of memory, otherwise null on error.
 */
void *
MemoryAllocate(void *header, long size)
{
	long excess, align_size;
	struct memory_header *head;
	struct memory *here, *best, *last;

	if (header == (void *) 0 || size < 0) {
		errno = EINVAL;
		return (void *) 0;
	}

	head = (struct memory_header *) header;
	last = (struct memory *) ((char *) head->first + head->size);

	if (MemoryVerifySize(header) == 0)
		return (void *) 0;

	/* Add space for accounting. */
	size += MINIMUM_CHUNK_SIZE;

	/* Align memory to size of long units. */;
	align_size = CHUNK_ALIGN(size);		
	
	/* Look for smallest free chunk that best fits the request. */
	for (best = here = head->first; here < last; here = CHUNK_NEXT(here)) {
		if (align_size <= here->size && (best->size < 0 || here->size < best->size))
			best = here;
	}

	/* Empty list or is the requested size too large? */
	if (best->size < align_size) {
		errno = ENOMEM;
		return (void *) 0;
	}

	/* Can the best chunk be split in two? */
	excess = best->size - align_size;

	if (MINIMUM_CHUNK_SIZE <= excess) {
		/* We allow free chunks as small as MINIMUM_CHUNK_SIZE,
		 * which is essentially a zero length block.
		 */
		here = (struct memory *) ((char *) best + align_size);
		here->size = excess;
		best->size = size;
	}

	/* Add a sentinel byte just after the end of the chunk which
	 * we can then use to test for an overflow condition.
	 */
	if (size & CHUNK_ALIGN_MASK)
		((char *) best)[size] = CHUNK_SENTINEL_BYTE;

	/* Mark the best chunk as allocated with a negative size. */
	best->size = CHUNK_NEGATE(best->size);

	return (void *) (best + 1);
}

/*
 * Set each byte of the given memory chunk to the specified byte value.
 */
void
MemorySet(void *chunk, int byte)
{
	memset(chunk, byte, MemorySizeOf(chunk));
}

/*
 * Release an allocated chunk of memory. Return 0 on success or -1 if 
 * there is a consistency check error.
 */
int
MemoryFree(void *header, void *chunk)
{
	struct memory_header *head;
	struct memory *here, *last;

	if (header == (void *) 0) {
		errno = EINVAL;
		return -1;
	}

	if (chunk == (void *) 0)
		return 0;

	head = (struct memory_header *) header;
	last = (struct memory *) ((char *) head->first + head->size);
	here = (struct memory *) chunk - 1;

	/* Chunk allocated from this block? */
	if (here < head->first || last <= here) {
		errno = EINVAL;
		return -1;
	}

	/* Are we trying to free an already freed chunk? */
	if (!CHUNK_IS_USED(here)) {
		errno = EFAULT;
		return -1;
	}
	
	here->size = CHUNK_ALIGNED_SIZE(here);

	/* Coalesce adjacent free chunks. */
	return -(MemoryVerifySize(header) != head->size);
}

static void *
MemoryChunkResize(void *header, void *chunk, long size, int copy)
{
	void *replace;

	if (chunk == (void *) 0)
		return MemoryAllocate(header, size);

	if (size <= MemorySizeOf(chunk))
		return chunk;

	if ((replace = MemoryAllocate(header, size)) == (void *) 0)
		return (void *) 0;

	if (copy)
		memcpy(replace, chunk, MemorySizeOf(chunk));

	(void) MemoryFree(header, chunk);

	return replace;
}

/*
 * Similar to the C library realloc(), where a request for a larger
 * memory chunk results in a new memory chunk being allocated and
 * the data copied into it. It can return the same memory chunk, a
 * new memory chunk (in which case the old one will have been freed),
 * or null on error (in which case the old one has not been freed).
 */
void *
MemoryReallocate(void *header, void *chunk, long size)
{
	return MemoryChunkResize(header, chunk, size, 1);
}

/*
 * Similar to MemoryReallocate(), but does NOT preserve the
 * contents of the previous allocated chunk of memory.
 */
void *
MemoryResize(void *header, void *chunk, long size)
{
	return MemoryChunkResize(header, chunk, size, 0);
}

#if defined(TEST)

#include <stdio.h>
#include <stdlib.h>

#define MEMORY_SIZE	100
#define MARKER		__FILE__,__LINE__

void
notNull(void *ptr, char *file, long line)
{
	if (ptr == (void *) 0) {
		printf("Expected non-null pointer at %s:%ld\n", file, line);
		exit(1);
	}
}

void
isNull(void *ptr, char *file, long line)
{
	if (ptr != (void *) 0) {
		printf("Expected null pointer at %s:%ld\n", file, line);
		exit(1);
	}
}

void
expectedChunkSize(void *block, void *chunk, long size, char *file, long line)
{
	long chunksize = ((struct memory *) chunk)[-1].size;
	
	printf("Chunk %lx size: %ld\n", (long) chunk, MemorySizeOf(chunk));	
	printf("Memory space available: %ld\n", MemoryAvailable(block));

	if (chunksize != size) {
		printf("Unexpected chunk size %ld at %s:%ld\n", chunksize, file, line);
		exit(1);
	}
}

int
main(int argc, char **argv)
{
	long value;
	void *block, *header, *first, *a, *b, *c;

	setvbuf(stdout, (char *) 0, _IOLBF, BUFSIZ);

	block = malloc(MEMORY_SIZE);
	notNull(block, MARKER);
	header = MemoryCreate(block, MEMORY_SIZE);

	/************************************************************
	 * Show available free space.
	 ************************************************************/

	printf("TEST 1: Memory space available: %ld\n", MemoryAvailable(header));

	value = MemoryVerifySize(header);
	printf("MemoryVerifySize() returned %ld.\n", value);

	if (value == 0) {
		printf("MemoryVerifySize() failed!\n");
		exit(1);
	}

	/************************************************************
	 * Allocate & free should result in initial free space.
	 ************************************************************/

	printf("TEST 2: Allocate & free should result in initial free space.\n");

	first = MemoryAllocate(header, 17);
	notNull(first, MARKER);

	expectedChunkSize(header, first, CHUNK_ENCODE_SIZE(17), MARKER);

	MemorySet(first, '#');
	
	MemoryFree(header, first);
	expectedChunkSize(header, first, MEMORY_SIZE, MARKER);

	/************************************************************
	 * Allocate & free in different order.
	 ************************************************************/

	printf("TEST 3: Allocate & free in different order.\n");

	first = MemoryAllocate(header, 0);
	notNull(first, MARKER);
	expectedChunkSize(header, first, CHUNK_ENCODE_SIZE(0), MARKER);

	a = MemoryAllocate(header, 16);
	notNull(a, MARKER);
	expectedChunkSize(header, a, CHUNK_ENCODE_SIZE(16), MARKER);

	b = MemoryAllocate(header, 17);
	notNull(b, MARKER);
	expectedChunkSize(header, b, CHUNK_ENCODE_SIZE(17), MARKER);

	c = MemoryAllocate(header, 18);
	notNull(c, MARKER);
	expectedChunkSize(header, c, CHUNK_ENCODE_SIZE(18), MARKER);

	MemoryFree(header, c);
	MemoryFree(header, a);
	MemoryFree(header, b);
	expectedChunkSize(header, a, MEMORY_SIZE - MINIMUM_CHUNK_SIZE, MARKER);

	MemoryFree(header, first);
	expectedChunkSize(header, first, MEMORY_SIZE, MARKER);

	/************************************************************
	 * Allocate beyond free space should result in null.
	 ************************************************************/

	printf("TEST 4: Allocate beyond free space should result in null.\n");

	first = MemoryAllocate(header, 0);
	notNull(first, MARKER);
	expectedChunkSize(header, first, -MINIMUM_CHUNK_SIZE, MARKER);

	a = MemoryAllocate(header, 20);
	notNull(a, MARKER);
	expectedChunkSize(header, a, CHUNK_ENCODE_SIZE(20), MARKER);

	b = MemoryAllocate(header, 40);
	notNull(b, MARKER);
	expectedChunkSize(header, b, CHUNK_ENCODE_SIZE(40), MARKER);

	c = MemoryAllocate(header, 60);
	isNull(c, MARKER);

	MemoryFree(header, a);
	MemoryFree(header, b);
	expectedChunkSize(header, a, MEMORY_SIZE - MINIMUM_CHUNK_SIZE, MARKER);

	MemoryFree(header, first);
	expectedChunkSize(header, first, MEMORY_SIZE, MARKER);

	/************************************************************
	 * Verify that the overal size is the same we started with.
	 ************************************************************/

	printf("TEST 5: Verify that the overal size is the same we started with.\n");

	value = MemoryVerifySize(header);
	printf("MemoryVerifySize() returned %ld.\n", value);

	if (value == 0) {
		printf("MemoryVerifySize() failed!\n");
		exit(1);
	}


	/************************************************************
	 * Verify that MemoryVerifySize() detects overflow.
	 ************************************************************/

	printf("TEST 6: MemoryVerifySize() detects overflow.\n");

	first = MemoryAllocate(header, 0);
	notNull(first, MARKER);
	expectedChunkSize(header, first, CHUNK_ENCODE_SIZE(0), MARKER);

	a = MemoryAllocate(header, 18);
	notNull(a, MARKER);
	expectedChunkSize(header, a, CHUNK_ENCODE_SIZE(18), MARKER);

	b = MemoryAllocate(header, 17);
	notNull(b, MARKER);
	expectedChunkSize(header, b, CHUNK_ENCODE_SIZE(17), MARKER);

	c = MemoryAllocate(header, 16);
	notNull(c, MARKER);
	expectedChunkSize(header, c, CHUNK_ENCODE_SIZE(16), MARKER);
	
	/* Detect overflow in chunk that is a multiple of alignment bytes. */
	printf("Corrupting chunk C...\n");
	(void) memset(c, 'c', MemorySizeOf(c)+1);
	printf("MemoryVerifySize()...\n");
	if (MemoryVerifySize(header) != 0) {
		printf("MemoryVerifySize() failed to detect overflow for chunk C.\n");
		exit(1);
	}
		
	/* Detect overflow in chunk of odd length, not a multple of alignment bytes. */
	printf("Corrupting chunk B...\n");
	(void) memset(b, 'b', MemorySizeOf(b)+1);
	printf("MemoryVerifySize()...\n");
	if (MemoryVerifySize(header) != 0) {
		printf("MemoryVerifySize() failed to detect overflow for chunk B.!\n");
		exit(1);
	}

	/* Detect overlfow in chunk of even length, not a multple of alignment bytes. */
	printf("Corrupting chunk A...\n");
	(void) memset(a, 'a', MemorySizeOf(a)+1);
	printf("MemoryVerifySize()...\n");
	if (MemoryVerifySize(header) != 0) {
		printf("MemoryVerifySize() failed to detect overflow for chunk A.!\n");
		exit(1);
	}

	printf("MemoryVerifySize() good\n");

	/************************************************************
	 * Clean-Up
	 ************************************************************/

	MemoryDestroy(header);
	free(block);

	printf("OK\n");

	return 0;
}

#endif
