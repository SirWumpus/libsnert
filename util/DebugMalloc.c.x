/*
 * DebugMalloc.c
 *
 * Copyright 2003, 2006 by Anthony Howe.  All rights reserved.
 *
 * Inspired by Armin Biere's ccmalloc. This version is far less
 * complex and only detects four types of memory errors:
 *
 *   -	double-free
 *   -	memory over runs
 *   -	memory under runs
 *   -	releasing an invalid pointer
 *
 * However its main advantage is that its reentrant and thus
 * thread safe, and is known to work under Linux, OpenBSD, and
 * Windows.
 *
 * Note though that FreeBSD, OpenBSD, and NetBSD already provide
 * some native debugging mechanisms for their malloc() and free().
 * See man 3 malloc.
 */

#ifndef MAX_STATIC_MEMORY
#define MAX_STATIC_MEMORY	(32 * 1024)
#endif

#ifndef DOH_BYTE
#define DOH_BYTE		0xD0
#endif

#ifndef DAH_BYTE
#define DAH_BYTE		0xDA
#endif

#ifndef FREED_BYTE
#define FREED_BYTE		0xDF
#endif

#ifndef MALLOC_BYTE
#define MALLOC_BYTE		0xFF
#endif

/* This alignment known to work for Linux, OpenBSD, and Windows.
 *
 * Too small and OpenBSD barfs on thread context switching. Windows
 * appears to have a smaller alignment of 8, which is accounted for
 * in the DebugMalloc() code below.
 */
#ifndef ALIGNMENT_SIZE
#define ALIGNMENT_SIZE 		32	/* Must be a power of 2. */
#endif

#ifndef SIGNAL_MEMORY
#define SIGNAL_MEMORY		SIGSEGV
#endif

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <stdio.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>

#if defined(HAVE_SYSEXITS_H) && ! defined(__WIN32__)
# include <sysexits.h>
#endif
#if defined(DEBUG_MALLOC_THREAD_REPORT)
# include <com/snert/lib/sys/pthread.h>
#endif

extern ssize_t write(int, const void *, size_t);

/***********************************************************************
 *** Constants & Globals
 ***********************************************************************/

#undef free
#undef malloc
#undef calloc
#undef realloc

#ifndef EX_DATAERR
#define EX_DATAERR		65
#endif
#ifndef EX_OSERR
#define EX_OSERR		71
#endif

#if !defined(HAVE_PTHREAD_KEY_CREATE) && !defined(HAVE_PTHREAD_CLEANUP_PUSH)
# undef DEBUG_MALLOC_THREAD_REPORT
#endif

enum { MEMORY_INITIALISE, MEMORY_INITIALISING, MEMORY_INITIALISED };

typedef struct memory_marker {
	char *base;
	size_t size;
#if defined(DEBUG_MALLOC_THREAD_REPORT)
	struct memory_marker *prev;
	struct memory_marker *next;
#endif
	unsigned long crc;
} MemoryMarker;

#define ALIGNMENT_MASK 		(alignment_size-1)
#define ALIGNMENT_OVERHEAD(s)	(((s) | alignment_mask) + 1 + alignment_size)
#define CHUNK_OFFSET		((sizeof (MemoryMarker) | alignment_mask)+1)

void  DebugFree(void *);
void *DebugMalloc(size_t);
void *DebugCalloc(size_t, size_t);
void *DebugRealloc(void *, size_t);

static void (*libc_free)(void *);
static void *(*libc_malloc)(size_t);

static int memory_init_state = MEMORY_INITIALISE;

/*
 * These counters are not really thread safe as they're not
 * protected by a semaphore or mutex. I'm not too fussed as
 * they're more a guide as to possible memory leaks.
 *
 * Note too that some systems will use malloc() and free()
 * during the application initialisation sequence before
 * reaching main() and that memory will probably be counted
 * as lost.
 */
static volatile unsigned long static_free_count = 0;
static volatile unsigned long static_allocation_count = 0;
static volatile unsigned long memory_free_count = 0;
static volatile unsigned long memory_allocation_count = 0;

static size_t static_used;
static char static_memory[MAX_STATIC_MEMORY];

long alignment_mask = ALIGNMENT_SIZE-1;
long alignment_size = ALIGNMENT_SIZE;

long memory_raise_signal = 1;
long memory_raise_and_exit = 1;
long memory_test_double_free = 1;
void *memory_free_chunk = NULL;
void *memory_malloc_chunk = NULL;
unsigned long memory_report_interval = 0;

/***********************************************************************
 ***
 ***********************************************************************/

static void
msgv(char *fmt, va_list args)
{
	int len;
	char buf[100];

	len = vsnprintf(buf, sizeof (buf), fmt, args);
	(void) write(2, buf, len);
}

static void
msg(char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	msgv(fmt, args);
	va_end(args);
}

static void
signal_error(char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	msgv(fmt, args);
	va_end(args);

	if (memory_raise_signal)
		raise(SIGNAL_MEMORY);

	if (memory_raise_and_exit)
		exit(EX_DATAERR);
}

void
DebugMallocReport(void)
{
	msg(
		" malloc: %14lu    free: %14lu    lost: %14ld\r\n",
		memory_allocation_count, memory_free_count,
		memory_allocation_count - memory_free_count
	);

	msg(
		"_malloc: %14lu   _free: %14lu   _lost: %14ld\r\n",
		static_allocation_count, static_free_count,
		static_allocation_count - static_free_count
	);
}

#if defined(DEBUG_MALLOC_THREAD_REPORT)
static int thread_key_ready;
static pthread_key_t thread_malloc_list;
static int (*lib_pthread_create)(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine)(void *), void *arg);

void
DebugMallocThreadReport(void *data)
{
	void *chunk;
	unsigned count = 0;
	MemoryMarker *block = data;

	if (block != NULL) {
		for ( ; block != NULL; block = block->next, count++) {
			chunk = (void *) ((char *) block + CHUNK_OFFSET);

			if (block->crc != ((unsigned long) block->base ^ block->size)) {
				signal_error("thread #%lu memory marker corruption chunk=0x%lx size=%lu\r\n", (unsigned long) pthread_self(), (long) chunk,  block->size);
				break;
			}

			msg("thread #%lu chunk=0x%lx size=%lu\n", (unsigned long) pthread_self(), (long) chunk, block->size);
		}

		msg("thread #%lu allocated blocks=%u\n", (unsigned long) pthread_self(), count);
	}
}

int
pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine)(void *), void *arg)
{
	/* Must wait until the pthread library has been initialised
	 * before we can initialise a thread key. If we attempt to
	 * use pthread_key_create() in init(), then FreeBSD seg. faults.
	 */
	if (thread_key_ready == 0) {
		thread_key_ready++;
		if (!pthread_key_create(&thread_malloc_list, DebugMallocThreadReport))
			thread_key_ready++;
	}
	return (*lib_pthread_create)(thread, attr, start_routine, arg);
}

#endif

/*
 * During program initialisation, we have to assign memory from
 * a static block. Its assumed that the program at this stage
 * is still a single thread. Any memory freed at this stage
 * must have been assigned by _malloc() and is abandoned.
 */
static int
_free(void *chunk)
{
	if (chunk == NULL)
		return 1;

	if (static_memory <= (char *) chunk && (char *) chunk < static_memory + static_used) {
		static_free_count++;
		return 1;
	}

	return 0;
}

/*
 * During program initialisation, we have to assign memory from
 * a static block. Its assumed that the program at this stage
 * is still a single thread.
 */
static void *
_malloc(size_t size)
{
	size_t aligned_used = static_used;

	/* Advance to next aligned chunk. */
	if ((aligned_used & alignment_mask) != 0)
		aligned_used = ALIGNMENT_OVERHEAD(aligned_used);

	if (sizeof (static_memory) <= aligned_used + size) {
		errno = ENOMEM;
		return NULL;
	}

	static_used = aligned_used + size;
	static_allocation_count++;

	return (void *) (static_memory + aligned_used);
}

static void
init_common(void)
{
	char *value;

	if ((value = getenv("MEMORY_ALIGNMENT_SIZE")) != NULL) {
		alignment_size = strtol(value, NULL, 0);

		/* If the number is too small or NOT a power of two, then use the default. */
		if (alignment_size < 8 || (alignment_size & -alignment_size) != alignment_size)
			alignment_size = ALIGNMENT_SIZE;
		alignment_mask = alignment_size - 1;
	}
	if ((value = getenv("MEMORY_FREE_CHUNK")) != NULL)
		memory_free_chunk = (void *) strtol(value, NULL, 0);
	if ((value = getenv("MEMORY_MALLOC_CHUNK")) != NULL)
		memory_malloc_chunk = (void *) strtol(value, NULL, 0);
	if ((value = getenv("MEMORY_RAISE_SIGNAL")) != NULL)
		memory_raise_signal = strtol(value, NULL, 0);
	if ((value = getenv("MEMORY_RAISE_AND_EXIT")) != NULL)
		memory_raise_and_exit = strtol(value, NULL, 0);
	if ((value = getenv("MEMORY_REPORT_INTERVAL")) != NULL)
		memory_report_interval = (unsigned long) strtol(value, NULL, 0);

	if (atexit(DebugMallocReport)) {
		msg("atexit() error\r\n");
		exit(EX_OSERR);
	}
}

#if defined(__WIN32__)
# include <windows.h>

# ifdef USE_LOCAL_FAMILY
void *
memory_alloc(size_t size)
{
	return LocalAlloc(LPTR, size);
}

void
memory_free(void *chunk)
{
	if (LocalFree(chunk) != NULL) {
		msg("free failed chunk=%lx", (long) chunk);
		raise(SIGNAL_MEMORY);
		exit(EX_DATAERR);
	}
}

# else

void *
memory_alloc(size_t size)
{
	return HeapAlloc(GetProcessHeap(), 0, size);
}

void
memory_free(void *chunk)
{
	if (!HeapFree(GetProcessHeap(), 0, chunk)) {
		msg("free failed chunk=%lx", (long) chunk);
		raise(SIGNAL_MEMORY);
		exit(EX_DATAERR);
	}
}
# endif /* USE_LOCAL_FAMILY */

/*
 * MingW uses a version that doesn't call malloc(), but
 * one of Windows other memory management functions and
 * returns a short pointer or its corrupting the pointer.
 * Regardless, it gets in the way of debugging.
 */
char *
strdup(const char *orig)
{
	char *copy;
	size_t size;

	size = strlen(orig) + 1;
	if ((copy = malloc(size)) != NULL)
		TextCopy(copy, size, orig);

	return copy;
}

static void
init(void)
{
	memory_init_state = MEMORY_INITIALISING;

	libc_malloc = memory_alloc;
	libc_free = memory_free;

	init_common();

	memory_init_state = MEMORY_INITIALISED;
}

#elif defined(HAVE_DLFCN_H) && defined(LIBC_PATH)
# include <dlfcn.h>

static void
init(void)
{
	void *handle;
	const char *err;

	memory_init_state = MEMORY_INITIALISING;

	init_common();

	handle = dlopen(LIBC_PATH, RTLD_NOW);
	if ((err = dlerror()) != NULL) {
		msg("libc.so load error\r\n");
		exit(EX_OSERR);
	}

  	libc_malloc = (void *(*)(size_t)) dlsym(handle, "malloc");
	if ((err = dlerror()) != NULL) {
		msg("failed to find libc malloc()\r\n");
		exit(EX_OSERR);
	}

  	libc_free = (void (*)(void *)) dlsym(handle, "free");
	if ((err = dlerror()) != NULL) {
		msg("failed to find libc free()\r\n");
		exit(EX_OSERR);
	}

#if defined(DEBUG_MALLOC_THREAD_REPORT)
	handle = dlopen("libpthread.so", RTLD_NOW);
	if ((err = dlerror()) != NULL) {
		msg("libpthread.so load error\r\n");
		exit(EX_OSERR);
	}

  	lib_pthread_create = dlsym(handle, "pthread_create");
	if ((err = dlerror()) != NULL) {
		msg("failed to find pthread_create()\r\n");
		exit(EX_OSERR);
	}
#endif
	memory_init_state = MEMORY_INITIALISED;
}

#elif !defined(LIBC_PATH)
# error "LIBC_PATH is undefined. The aclocal.m4 macro"
# error "SNERT_FIND_LIBC failed to find the C library"
# error "for your OS. For Windows, try:"
# error
# error "  ./configure --enable-mingw"
# error
#else
# error "Unable to debug memory on this system."
#endif

void
free(void *chunk)
{
	switch (memory_init_state) {
	case MEMORY_INITIALISE:
		init();
		/*@fallthrough@*/

	case MEMORY_INITIALISING:
		if (!_free(chunk)) {
			raise(SIGNAL_MEMORY);
			exit(EX_DATAERR);
		}
		break;

	case MEMORY_INITIALISED:
		if (!_free(chunk))
			DebugFree(chunk);
	}

	if (chunk != NULL) {
		memory_free_count++;

		if (chunk == memory_free_chunk)
			signal_error("memory stop free on chunk=0x%lx\r\n", (long) chunk);

		if (0 < memory_report_interval && memory_free_count % memory_report_interval == 0)
			DebugMallocReport();
	}
}

void *
malloc(size_t size)
{
	void *chunk = NULL;

	switch (memory_init_state) {
	case MEMORY_INITIALISE:
		init();
		/*@fallthrough@*/

	case MEMORY_INITIALISING:
		chunk = _malloc(size);
		break;

	case MEMORY_INITIALISED:
		chunk = DebugMalloc(size);
	}

	if (chunk != NULL) {
		memory_allocation_count++;

		if (chunk == memory_malloc_chunk)
			signal_error("memory stop malloc on chunk=0x%lx\r\n", chunk);
	}

	return chunk;
}

void *
calloc(size_t m, size_t n)
{
	return DebugCalloc(m, n);
}

void *
realloc(void *chunk, size_t size)
{
	return DebugRealloc(chunk, size);
}

void
DebugFree(void *chunk)
{
	char *base;
	size_t size;
	MemoryMarker *block;
	unsigned char *p, *q;

	if (chunk == NULL)
		return;

	/* Correctly aligned pointer? */
	if ((unsigned long) chunk & alignment_mask) {
		signal_error("memory alignment error chunk=0x%lx\r\n", (long) chunk);
		return;
	}

	/* Compute location of the marker. */
	block = (MemoryMarker *)((char *) chunk - CHUNK_OFFSET);

	/* Remember this for later for after the libc_free call, when
	 * the marker structure may have been overwritten.
	 */
	size = block->size;
	base = block->base;

	if (block->crc != ((unsigned long) base ^ size)) {
		signal_error("memory marker corruption chunk=0x%lx size=%lu\r\n", (long) chunk, size);
		return;
	}

	/* Is the size too big to be true? */
	if (size & ~((~ (size_t) 0) >> 1))
		memory_test_double_free = 0;

#if defined(DEBUG_MALLOC_THREAD_REPORT)
	if (1 < thread_key_ready) {
		if (block->prev != NULL) {
			block->prev->next = block->next;
		} else {
			/* Update thread's pointer to the head of it's allocation list. */
			(void) pthread_setspecific(thread_malloc_list, block->next);
			if (block->next != NULL)
				block->next->prev = NULL;
		}

		if (block->next != NULL)
			block->next->prev = block->prev;
		else if (block->prev != NULL)
			block->prev->next = NULL;
	}
#endif

	if (memory_test_double_free) {
		/* Look for double-free first, since the marker structure
		 * may have been overwritten by a previous free() causing
		 * a false positive for a memory underrun.
		 */
		q = (unsigned char *) chunk + size;
		for (p = (unsigned char *) chunk; p < q; p++) {
			if (*p != FREED_BYTE)
				break;
		}

		if (0 < size && q <= p) {
			signal_error("double-free chunk=0x%lx size=%lu\r\n", (long) chunk, size);
			return;
		}
	}

	/* Check the boundary marker between block and chunk. */
	for (p = (unsigned char *) (block + 1); p < (unsigned char *) chunk; p++) {
		if (*p != DOH_BYTE) {
			signal_error("memory underun (a) chunk=0x%lx size=%lu\r\n", (long) chunk, size);
			return;
		}
	}

	/* Check the boundary marker between base and block, might be zero length. */
	for (p = (unsigned char *) block->base; p < (unsigned char *) block; p++) {
		if (*p != DAH_BYTE) {
			signal_error("memory underun (b) chunk=0x%lx size=%lu\r\n", (long) chunk, size);
			return;
		}
	}

	/* Check the boundary marker between end of chunk and end of block. */
	q = (unsigned char *) chunk + ALIGNMENT_OVERHEAD(size);
	for (p = (unsigned char *) chunk + size; p < q; p++) {
		if (*p != DOH_BYTE) {
			signal_error("memory overrun chunk=0x%lx size=%lu\r\n", (long) chunk, size);
			return;
		}
	}

	if (memory_test_double_free)
		/* Wipe from the WHOLE memory area including the space
		 * for the lower and upper boundaries.
		 */
		memset(base, FREED_BYTE, alignment_size + CHUNK_OFFSET + ALIGNMENT_OVERHEAD(size));

	(*libc_free)(base);

#ifdef __WIN32__
/* On some systems like OpenBSD with its own debugging malloc()/free(),
 * once freed you cannot access the memory without causing a segmentation
 * fault. Windows is less strict.
 */
	/* Windows appears to do its own double-free detection after a free(). */
	if (0 < size && ((unsigned char *) chunk)[size - 1] != FREED_BYTE)
		memory_test_double_free = 0;
#endif
}

void *
DebugMalloc(size_t size)
{
	char *base;
	void *chunk;
	size_t adjust;
	MemoryMarker *block;

	/* Allocate enough space to include some boundary markers. */
	base = (*libc_malloc)(alignment_size + CHUNK_OFFSET + ALIGNMENT_OVERHEAD(size));
	if (base == NULL)
		return NULL;

	/* The returned ``base'' may be on a different alignment from ours. */
	if ((unsigned long) base & alignment_mask)
		adjust = alignment_size - ((unsigned long) base & alignment_mask);
	else
		adjust = 0;

	/* Compute the location of our aligned marker. */
	block = (MemoryMarker *)(base + adjust);
	block->crc = (unsigned long) base ^ size;
	block->base = base;
	block->size = size;

#if defined(DEBUG_MALLOC_THREAD_REPORT)
	if (1 < thread_key_ready) {
		MemoryMarker *head = pthread_getspecific(thread_malloc_list);
		if (head == NULL) {
			block->prev = block->next = NULL;
		} else {
			block->prev = NULL;
			block->next = head;
			head->prev = block;
		}

		(void) pthread_setspecific(thread_malloc_list, block);
	}
#endif

	/* Compute the alignment of the chunk to return. */
	chunk = (void *) ((char *) block + CHUNK_OFFSET);

	/* Add the boundary markers. */
	memset(base, DAH_BYTE, adjust);
	memset(block+1, DOH_BYTE, CHUNK_OFFSET - sizeof (MemoryMarker));
	memset((char *) chunk + size, DOH_BYTE, ALIGNMENT_OVERHEAD(size) - size);

	/* When we allocate a previously freed chunk, write a single value
	 * to the start of the chunk to prevent inadvertant double-free
	 * detection in case the caller never writes anything into the chunk.
	 */
	if (0 < size)
		*(unsigned char *) chunk = MALLOC_BYTE;

	return chunk;
}

void *
DebugRealloc(void *chunk, size_t size)
{
	void *replacement;
	MemoryMarker *block;

	if ((replacement = malloc(size)) == NULL)
		return NULL;

	if (chunk != NULL) {
		block = (MemoryMarker *)((char *) chunk - CHUNK_OFFSET);
		memcpy(replacement, chunk, block->size < size ? block->size : size);
		free(chunk);
	}

	return replacement;
}

void *
DebugCalloc(size_t m, size_t n)
{
	void *chunk;

	if ((chunk = malloc(m * n)) != NULL)
		memset(chunk, 0, m * n);

	return chunk;
}
