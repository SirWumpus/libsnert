/*
 * DebugMalloc.c
 *
 * Copyright 2003, 2015 by Anthony Howe.  All rights reserved.
 *
 * Inspired by Armin Biere's ccmalloc. This version is far less
 * complex and only detects five types of memory errors:
 *
 *   -	double-free
 *   -	memory over runs
 *   -	memory under runs
 *   -	releasing an invalid pointer
 *   -	memory leaks
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
#define MAX_STATIC_MEMORY	(64 * 1024)
#endif

#ifndef MAX_DUMP_LENGTH
#define MAX_DUMP_LENGTH		16
#endif

#ifndef DOH_BYTE
#define DOH_BYTE		'>'
#endif

#ifndef DAH_BYTE
#define DAH_BYTE		'<'
#endif

#ifndef FREED_BYTE
#define FREED_BYTE		'#'
#endif

#ifndef MALLOC_BYTE
#define MALLOC_BYTE		0xFF
#endif

/* This alignment known to work for Linux, OpenBSD, and Windows.
 * Too small and OpenBSD barfs on thread context switching. Windows
 * appears to have a smaller alignment of 8.
 */
#ifndef ALIGNMENT_SIZE
#define ALIGNMENT_SIZE 		16	/* Must be a power of 2. */
#endif

#define ALIGNMENT_MASK		(ALIGNMENT_SIZE-1)

#ifndef PAD_SIZE
#define PAD_SIZE		8	/* Must be even. */
#endif

#ifndef SIGNAL_MEMORY
#define SIGNAL_MEMORY		SIGUSR2
#endif

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#if HAVE_INTTYPES_H
# include <inttypes.h>
#else
# if HAVE_STDINT_H
# include <stdint.h>
# endif
#endif

#include <com/snert/lib/sys/sysexits.h>

#define DEBUG_MALLOC
#include <com/snert/lib/util/DebugMalloc.h>

extern ssize_t write(int, const void *, size_t);

#ifndef LIBC_PATH
#define LIBC_PATH		"libc.so"
#endif

#ifndef LIBPTHREAD_PATH
#define LIBPTHREAD_PATH		"libpthread.so"
#endif

/***********************************************************************
 *** Constants & Globals
 ***********************************************************************/

#if defined(_THREAD_SAFE) && ! defined(__WIN32__)
# include <com/snert/lib/sys/pthread.h>

# define LOCK_T				pthread_mutex_t
# define LOCK_INIT(L)			pthread_mutex_init(L, NULL)
# define LOCK_FREE(L)			pthread_mutex_destroy(L)
# define LOCK_LOCK(L)			pthread_mutex_lock(L)
# define LOCK_TRYLOCK(L)		pthread_mutex_trylock(L)
# define LOCK_UNLOCK(L)			pthread_mutex_unlock(L)

#else
/* Disable pthread support. */
# define LOCK_T				unsigned
# define LOCK_INIT(L)			(0)
# define LOCK_FREE(L)			(0)
# define LOCK_LOCK(L)			(0)
# define LOCK_TRYLOCK(L)		(0)
# define LOCK_UNLOCK(L)			(0)

# define PTHREAD_MUTEX_INITIALIZER	0
# define PTHREAD_ONCE_INIT		0
# define pthread_t			unsigned
# define pthread_self()			(0)
# define pthread_once_t			unsigned
# define pthread_once(o, f)		(0)
# define pthread_key_t			unsigned
# define pthread_key_create(k, f)	(0)
# define pthread_getspecific(k)		main_head
# define pthread_setspecific(k, v)	(main_head = (v), 0)
# define pthread_cleanup_push(f, d)
# define pthread_cleanup_pop(b)

#endif

#define HERE_FMT		"%s:%u "
#define HERE_ARG		here, line

#define BLOCK_FMT		"chunk=0x%lx size=%lu " HERE_FMT
#define BLOCK_ARG(b)		(unsigned long)&(b)[1], (unsigned long)(b)->size, (b)->here, (b)->line
#define BASIC_CRC(b)		((unsigned long)(b) ^ (b)->size ^ (unsigned long)(b)->here ^ (b)->line)

typedef enum {
	MEMORY_INITIALISE,
	MEMORY_INITIALISING,
	MEMORY_INITIALISED
} MemoryInit;

typedef struct block_data {
#ifdef OVER_KILL
/* In theory we don't need the leading boundary, if the CRC
 * includes all the sensitive fields, but not the links.
 */
	unsigned char before[PAD_SIZE];
#endif
	size_t size;
	unsigned line;
	const char *here;
	pthread_t thread;
	unsigned long crc;
	struct block_data *prev;
	struct block_data *next;
	unsigned char after[PAD_SIZE];
} BlockData;

#define ALIGN_SIZE(sz, pow2)	((((sz)-1) & ~(pow2-1)) + pow2)
#define ALIGN_PAD(sz, pow2)	(ALIGN_SIZE(sz, pow2) - sz)

static const char unknown[] = "(unknown)";
static void signal_error(char *fmt, ...);

static void (*libc__exit)(int);
static void (*libc_free)(void *);
static void *(*libc_malloc)(size_t);

static MemoryInit memory_init_state = MEMORY_INITIALISE;

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

int memory_exit = 1;
int memory_signal = SIGNAL_MEMORY;
int memory_show_free = 0;
int memory_show_malloc = 0;
int memory_dump_length = MAX_DUMP_LENGTH;
int memory_thread_leak = 0;
int memory_test_double_free = 1;
int memory_freed_marker = FREED_BYTE;
#ifdef OVER_KILL
int memory_lower_marker = DAH_BYTE;
#endif
int memory_marker = DOH_BYTE;
unsigned long memory_report_interval = 0;

size_t memory_free_chunk_size;
void *memory_free_chunk = NULL;

size_t memory_malloc_chunk_size;
void *memory_malloc_chunk = NULL;

/***********************************************************************
 ***
 ***********************************************************************/

#if defined(_THREAD_SAFE) && ! defined(__WIN32__)
static LOCK_T lock;
static pthread_key_t thread_key;
#else
static BlockData *main_head;
#endif

static pthread_t main_thread;

/*
 * Dump the list of allocated memory at thread exit.
 */
static void
thread_exit_report(void *data)
{
	size_t size;
	unsigned count;
	BlockData *block = data, *next;

	if (block != NULL) {
		/* Clear thread data to avoid thread key cleanup loop. */
		(void) pthread_setspecific(thread_key, NULL);

		if (main_thread == block->thread)
			(void) fprintf(stderr, "main:\r\n");
		else
			(void) fprintf(stderr, "thread 0x%lx:\r\n", (unsigned long) block->thread);

		size = 0;
		for (count = 0; block != NULL; block = next, count++) {
			/* The thread data cleanup will be called before
			 * the thread releases its pthread_t data, which
			 * can result in a spurious leak being reported.
			 */
			next = block->next;
			size += block->size;
			DebugMallocDump(&block[1], memory_dump_length);
		}

		(void) fprintf(stderr, "\tleaked blocks=%u size=%lu\r\n", count, (unsigned long) size);
	}
}

void
(DebugMallocReport)(void)
{
	thread_exit_report(pthread_getspecific(thread_key));
}

static void
signal_error(char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	(void) vfprintf(stderr, fmt, args);
	va_end(args);

	if (0 < memory_signal) {
#ifdef NOT_NEEDED
		(void) LOCK_TRYLOCK(&lock);
		(void) LOCK_UNLOCK(&lock);
#endif
		raise(memory_signal);
	}

	if (memory_exit)
		exit(EX_DATAERR);
}

void
(DebugMallocSummary)(void)
{
	if (main_thread == pthread_self())
		(void) fprintf(stderr, "main_thread:\r\n");
	else
		(void) fprintf(stderr, "thread #%lx:\r\n", (unsigned long) pthread_self());

	(void) fprintf(stderr,
		"\t malloc: %-14lu  free: %-14lu  lost: %-14ld\r\n",
		memory_allocation_count, memory_free_count,
		memory_allocation_count - memory_free_count
	);

	(void) fprintf(stderr,
		"\t_malloc: %-14lu _free: %-14lu _lost: %-14ld _used: %-14ld\r\n",
		static_allocation_count, static_free_count,
		static_allocation_count - static_free_count,
		(unsigned long) static_used
	);
}

void
(DebugMallocDump)(void *chunk, size_t length)
{
	if (memory_init_state == MEMORY_INITIALISED) {
		BlockData *block = &((BlockData *) chunk)[-1];
		(void) fprintf(stderr, "\t" BLOCK_FMT " dump=", BLOCK_ARG(block));

		if (block->size < length)
			length = block->size;

		for ( ; 0 < length; length--, chunk++) {
			if (isprint(*(char *)chunk))
				(void) fprintf(stderr, "%c", *(char *)chunk);
			else
				(void) fprintf(stderr, "\\x%02X", *(char *)chunk);
		}

		(void) fprintf(stderr, "\r\n");
	}
}

static void
DebugMallocHere0(void *chunk, const char *here, unsigned line, const char *tag)
{
	if (memory_init_state == MEMORY_INITIALISED) {
		BlockData *block = &((BlockData *) chunk)[-1];
		(void) fprintf(stderr, HERE_FMT "%s " BLOCK_FMT "\r\n", HERE_ARG, tag, BLOCK_ARG(block));
	}
}

void
(DebugMallocHere)(void *chunk, const char *here, unsigned line)
{
	DebugMallocHere0(chunk, here, line, "info");
}

static void
init_common(void)
{
	char *value, *stop;

	if ((value = getenv("MEMORY")) != NULL) {
		memory_show_free = (strchr(value, 'F') != NULL);
		memory_show_malloc = (strchr(value, 'M') != NULL);
		memory_thread_leak = (strchr(value, 'T') != NULL);
		memory_signal = (strchr(value, 'S') == NULL) ? SIGSEGV : 0;
		memory_exit = (strchr(value, 'E') == NULL);
	}

	if ((value = getenv("MEMORY_SIGNUM")) != NULL)
		memory_signal = (int) strtol(value, NULL, 0);
	if ((value = getenv("MEMORY_DUMP_LENGTH")) != NULL)
		memory_dump_length = (int) strtol(value, NULL, 0);
	if ((value = getenv("MEMORY_FREE_CHUNK")) != NULL) {
		memory_free_chunk = (void *) strtoul(value, &stop, 0);
		if (*stop == ':')
			memory_free_chunk_size = strtoul(stop+1, NULL, 0);
	}
	if ((value = getenv("MEMORY_MALLOC_CHUNK")) != NULL) {
		memory_malloc_chunk = (void *) strtoul(value, &stop, 0);
		if (*stop == ':')
			memory_malloc_chunk_size = strtoul(stop+1, NULL, 0);
	}
	if ((value = getenv("MEMORY_REPORT_INTERVAL")) != NULL)
		memory_report_interval = (unsigned long) strtol(value, NULL, 0);
#ifdef __WIN32__
	if (atexit(DebugMallocReport)) {
		(void) fprintf(stderr, HERE_FMT "atexit() error\r\n", __FILE__, __LINE__);
		exit(EX_OSERR);
	}
	if (atexit(DebugMallocSummary)) {
		(void) fprintf(stderr, HERE_FMT "atexit() error\r\n", __FILE__, __LINE__);
		exit(EX_OSERR);
	}
#endif
	memory_init_state = MEMORY_INITIALISED;
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
		(void) fprintf(stderr, "free failed chunk=%lx", (long) chunk);
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
		(void) fprintf(stderr, "free failed chunk=%lx", (long) chunk);
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
}

#elif defined(HAVE_DLFCN_H) && defined(LIBC_PATH)
# include <dlfcn.h>

static void *libc;

static void
init(void)
{
	const char *err;

	memory_init_state = MEMORY_INITIALISING;

	libc = dlopen(LIBC_PATH, RTLD_NOW);
	if ((err = dlerror()) != NULL) {
		(void) fprintf(stderr, "libc.so load error: %s\r\n", err);
		exit(EX_OSERR);
	}

  	libc_malloc = (void *(*)(size_t)) dlsym(libc, "malloc");
	if ((err = dlerror()) != NULL) {
		(void) fprintf(stderr, "libc malloc() not found: %s\r\n", err);
		exit(EX_OSERR);
	}

  	libc_free = (void (*)(void *)) dlsym(libc, "free");
	if ((err = dlerror()) != NULL) {
		(void) fprintf(stderr, "libc free() not found: %s\r\n", err);
		exit(EX_OSERR);
	}

  	libc__exit = dlsym(libc, "_exit");
	if ((err = dlerror()) != NULL) {
		(void) fprintf(stderr, "libc _exit() not found: %s\r\n", err);
		exit(EX_OSERR);
	}

	init_common();
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
	if ((aligned_used & ALIGNMENT_MASK) != 0)
		aligned_used = ALIGN_SIZE(aligned_used, ALIGNMENT_SIZE);

	if (sizeof (static_memory) <= aligned_used + size) {
		errno = ENOMEM;
		return NULL;
	}

	static_used = aligned_used + size;
	static_allocation_count++;

	return (void *) (static_memory + aligned_used);
}

__dead void
(_exit)(int status)
{
	char buffer[128];

	/* Force any previous stdio buffers to be freed. */
	(void) setvbuf(stdin, NULL, _IONBF, 0);
	(void) setvbuf(stdout, NULL, _IONBF, 0);

	/* Use a temporary "static" output buffer. */
	(void) setvbuf(stderr, buffer, _IOLBF, sizeof (buffer));

#ifdef HAVE_SYS_RESOURCE_H
# define _STANDALONE
# include <sys/resource.h>
{
	struct rusage rusage;
	(void) getrusage(RUSAGE_SELF, &rusage);
	(void) fprintf(stderr, "RSS: %-14ld\r\n", rusage.ru_maxrss);
}
#endif

	DebugMallocSummary();
	DebugMallocReport();

	(*libc__exit)(status);
}

void
(free)(void *chunk)
{
	(DebugFree)(chunk, unknown, 0);
}

void *
(malloc)(size_t size)
{
	return (DebugMalloc)(size, unknown, 0);
}

void *
(calloc)(size_t elements, size_t size)
{
	return (DebugCalloc)(elements, size, unknown, 0);
}

void *
(realloc)(void *chunk, size_t size)
{
	return (DebugRealloc)(chunk, size, unknown, 0);
}

/**
 * Force the transition from MEMORY_INITIALISING to MEMORY_INITIALISED
 * in case not all object files were built using DebugMalloc.h and
 * DEBUG_MALLOC macro.  Place at top of main().
 */
void
(DebugMallocStart)(void)
{
	void *chunk;

	chunk = (DebugMalloc)(1, __func__, __LINE__);
	(DebugFree)(chunk, __func__, __LINE__);
}

void
(DebugMallocAssert)(void *chunk, const char *here, unsigned line)
{
	BlockData *block;
	unsigned char *p, *q;

	block = &((BlockData *) chunk)[-1];

	if (memory_test_double_free) {
		/* Look for double-free first, since the block structure
		 * may have been overwritten by a previous free() causing
		 * a false positive for a memory underrun.
		 */
		p = (unsigned char *) chunk;
		q = p + block->size;
		for ( ; p < q; p++) {
			if (*p != memory_freed_marker)
				break;
		}

		if (0 < block->size && q <= p) {
			signal_error(HERE_FMT "double-free " BLOCK_FMT "\r\n", BLOCK_ARG(block));
			return;
		}
	}

	/* Check the boundary marker between block and chunk. */
	p = block->after;
	q = chunk;
	for ( ; p < q; p++) {
		if (*p != memory_marker) {
			signal_error(HERE_FMT "memory underun (a) " BLOCK_FMT "\r\n", HERE_ARG, BLOCK_ARG(block));
			return;
		}
	}

#ifdef OVER_KILL
	/* Check the leading boundary marker */
	p = block->before;
	q = p + sizeof (block->before);
	for ( ; p < q; p++) {
		if (*p != memory_lower_marker) {
			signal_error(HERE_FMT "memory underun (b) " BLOCK_FMT "\r\n", HERE_ARG, BLOCK_ARG(block));
			return;
		}
	}
#endif

	/* Check the boundary marker between end of chunk and end of block. */
	p = (unsigned char *) &block[1] + block->size;
	q = p + ALIGN_PAD(block->size, ALIGNMENT_SIZE) + ALIGNMENT_SIZE;
	for ( ; p < q; p++) {
		if (*p != memory_marker) {
			signal_error(HERE_FMT "memory overrun " BLOCK_FMT "\r\n", HERE_ARG, BLOCK_ARG(block));
			return;
		}
	}
}

void
(DebugFree)(void *chunk, const char *here, unsigned line)
{
	size_t size, block_size;
	BlockData *block, *head;

	if (chunk == NULL)
		return;

	/* free() before main() can happen with OpenBSD. */
	switch (memory_init_state) {
	case MEMORY_INITIALISED:
		if (_free(chunk))
			return;
		break;
	case MEMORY_INITIALISING:
		if (_free(chunk))
			return;
		/* Wasn't allocated by _malloc(). */
		/*@fallthrough@*/
	case MEMORY_INITIALISE:
		/* free() before malloc() */
		raise(memory_signal);
		exit(EX_DATAERR);
	}

	/* Get the location of the block data. */
	block = &((BlockData *) chunk)[-1];

	/* Remember this for later for after the libc_free call, when
	 * the block structure may have been overwritten.
	 */
	size = block->size;

	block_size =
		  ALIGN_SIZE(sizeof (*block), ALIGNMENT_SIZE)
		+ ALIGN_SIZE(size, ALIGNMENT_SIZE)
		+ ALIGNMENT_SIZE;

	if (block->crc != BASIC_CRC(block)) {
		/* Can't report size as it is probably corrupted. */
		if (*(unsigned char *) chunk == memory_freed_marker) {
			signal_error(HERE_FMT "double free chunk=0x%lx\r\n", HERE_ARG, (long) chunk);
			return;
		}

		signal_error(HERE_FMT "memory corruption chunk=0x%lx\r\n", HERE_ARG, (long) chunk);
		return;
	}

	/* Is the size too big to be true? */
	if (size & ~((~ (size_t) 0) >> 1))
		memory_test_double_free = 0;

	/* Maintain list of allocated memory per thread, including main() */
	head = pthread_getspecific(thread_key);

	if (block->prev == NULL)
		head = block->next;
	else
		block->prev->next = block->next;

	if (block->next != NULL)
		block->next->prev = block->prev;

	(void) pthread_setspecific(thread_key, head);

	if (memory_show_free)
		DebugMallocHere0(chunk, here, line, "free");

	DebugMallocAssert(chunk, HERE_ARG);

	if (memory_test_double_free) {
		/* Wipe the WHOLE memory area including the space
		 * for the lower and upper boundaries.
		 */
		size_t block_size =
			  ALIGN_SIZE(sizeof (*block), ALIGNMENT_SIZE)
			+ ALIGN_SIZE(block->size, ALIGNMENT_SIZE)
			+ ALIGNMENT_SIZE;
		(void) memset(block, memory_freed_marker, block_size);
	}

	/* The libc free has its own mutex locking. */
	(*libc_free)(block);

	(void) LOCK_LOCK(&lock);
	memory_free_count++;
	(void) LOCK_UNLOCK(&lock);

#ifdef __WIN32__
/* On some systems like OpenBSD with its own debugging malloc()/free(),
 * once freed you cannot access the memory without causing a segmentation
 * fault. Windows is less strict.
 */
	/* Windows appears to do its own double-free detection after a free(). */
	if (0 < size && ((unsigned char *) chunk)[size - 1] != memory_freed_marker)
		memory_test_double_free = 0;
#endif
	if (chunk == memory_free_chunk
	&& (memory_free_chunk_size == 0 || size == memory_free_chunk_size)) {
		(void) fprintf(stderr, HERE_FMT "memory stop free on chunk=0x%lx\r\n", HERE_ARG, (long) chunk);
		raise(SIGSTOP);
	}

	if (0 < memory_report_interval && memory_free_count % memory_report_interval == 0)
		DebugMallocSummary();
}

void *
(DebugMalloc)(size_t size, const char *here, unsigned line)
{
	void *chunk;
	size_t block_size;
	BlockData *block, *head;
	static int init_second_stage = -1;

	/* malloc() before main() can happen with OpenBSD. */
	switch (memory_init_state) {
	case MEMORY_INITIALISED:
		break;

	case MEMORY_INITIALISE:
		init();
		/*@fallthrough@*/

	case MEMORY_INITIALISING:
		return _malloc(size);
	}

	if (init_second_stage) { 
		/* OpenBSD allocates more memory when some pthread functions
		 * are first used.  Force it here so as to allocate it from
		 * _malloc().  We can't do this in init(), since the first call
		 * happens during thread initilisation and would cause a loop.
		 */
		memory_init_state = MEMORY_INITIALISING;
		 
		(void) LOCK_INIT(&lock);
		(void) LOCK_LOCK(&lock);
		(void) LOCK_UNLOCK(&lock);
		(void) pthread_key_create(&thread_key, thread_exit_report);
		(void) pthread_setspecific(thread_key, NULL);

		memory_init_state = MEMORY_INITIALISED;
		main_thread = pthread_self();
		init_second_stage = 0;
	}

	/* Allocate enough space to include some boundary markers.
	 *
#ifdef OVER_KILL
	 * block.before	| DA DA DA DA DA DA DA DA ...
#endif
	 * block	| ...
	 * block.after	| D0 D0 D0 D0 D0 D0 D0 D0 ...
	 * chunk	| FF ?? ?? ?? ?? ?? ?? ?? ...
	 * chunk+size	| ?? ?? ?? DO D0 D0 D0 D0 ...	end padding & boundary
	 * chunk+size+8	| D0 D0 D0 DO D0 D0 D0 D0 ...
	 * chunk+size+16| D0 D0 D0 DO D0 D0 D0 D0 ...
	 */
	block_size =
		  ALIGN_SIZE(sizeof (*block), ALIGNMENT_SIZE)
		+ ALIGN_SIZE(size, ALIGNMENT_SIZE)
		+ ALIGNMENT_SIZE;

	/* The libc malloc has its own mutex locking. */
	if ((block = (*libc_malloc)(block_size)) == NULL)
		return NULL;

	(void) LOCK_LOCK(&lock);
	memory_allocation_count++;
	(void) LOCK_UNLOCK(&lock);

	/* The chunk to return. */
	chunk = &block[1];

	/* Fill in the boundary markers. */
	(void) memset(block, memory_marker, block_size);
#ifdef OVER_KILL
	(void) memset(block->before, memory_lower_marker, sizeof (block->before));
#endif
	/* Zero the chunk. */
	(void) memset((char *) chunk, 0, size);

	/* Maintain list of allocated memory per thread, including main() */
	head = pthread_getspecific(thread_key);
	block->prev = NULL;
	block->next = head;
	if (head != NULL)
		head->prev = block;
	(void) pthread_setspecific(thread_key, block);

	block->size = size;
	block->here = here;
	block->line = line;
	block->thread = pthread_self();
	block->crc = BASIC_CRC(block);

#ifdef NOPE
	/* When we allocate a previously freed chunk, write a single value
	 * to the start of the chunk to prevent inadvertant double-free
	 * detection in case the caller never writes anything into this chunk.
	 */
	if (0 < size)
		*(unsigned char *) chunk = MALLOC_BYTE;
#endif
	if (memory_show_malloc)
		DebugMallocHere0(chunk, here, line, "malloc");

	if (chunk == memory_malloc_chunk
	&& (memory_malloc_chunk_size == 0 || size == memory_malloc_chunk_size)) {
		(void) fprintf(stderr, HERE_FMT "memory stop malloc on " BLOCK_FMT "\r\n", HERE_ARG, BLOCK_ARG(block));
		raise(SIGSTOP);
	}

	return chunk;
}

void *
(DebugRealloc)(void *chunk, size_t size, const char *here, unsigned line)
{
	void *replacement;
	BlockData *block;

	if ((replacement = (DebugMalloc)(size, HERE_ARG)) == NULL)
		return NULL;

	if (chunk != NULL) {
		block = &((BlockData *) chunk)[-1];
		(void) memcpy(replacement, chunk, block->size < size ? block->size : size);
		free(chunk);
	}

	return replacement;
}

void *
(DebugCalloc)(size_t elements, size_t size, const char *here, unsigned line)
{
	void *chunk;

	if ((chunk = (DebugMalloc)(elements * size, HERE_ARG)) != NULL)
		(void) memset(chunk, 0, elements * size);

	return chunk;
}
