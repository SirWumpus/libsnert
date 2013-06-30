/*
 * DebugMalloc.c
 *
 * Copyright 2003, 2013 by Anthony Howe.  All rights reserved.
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

#include <com/snert/lib/sys/sysexits.h>
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

#ifdef _THREAD_SAFE
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
#define HERE_ARG		file, line

#define BLOCK_FMT		"chunk=0x%lx size=%lu " HERE_FMT
#define BLOCK_ARG(b)		&(b)[1], (b)->size, (b)->file, (b)->line

typedef enum {
	MEMORY_INITIALISE,
	MEMORY_INITIALISING,
	MEMORY_INITIALISED
} MemoryInit;

typedef struct block_data {
	unsigned char before[PAD_SIZE];
	size_t size;
	unsigned line;
	const char *file;
	unsigned long crc;
	struct block_data *prev;
	struct block_data *next;
	unsigned char after[PAD_SIZE];
} BlockData;

#define ALIGN_SIZE(sz, pow2)	((((sz)-1) & ~(pow2-1)) + pow2)
#define ALIGN_PAD(sz, pow2)	(ALIGN_SIZE(sz, pow2) - sz)

static const char *unknown = "(unknown)";
static void dump(unsigned char *chunk, size_t size);
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
int memory_thread_leak = 0;
int memory_test_double_free = 1;
int memory_freed_marker = FREED_BYTE;
int memory_lower_marker = DAH_BYTE;
int memory_upper_marker = DOH_BYTE;
void *memory_free_chunk = NULL;
void *memory_malloc_chunk = NULL;
unsigned long memory_report_interval = 0;

/***********************************************************************
 ***
 ***********************************************************************/

#ifdef _THREAD_SAFE
static LOCK_T lock;
static pthread_key_t thread_key;
#else
static BlockData *main_head;
#endif

static pthread_t main_thread;

static void
thread_exit_report(void *data)
{
	unsigned count;
	BlockData *block = data;

	/* Clear the thread key data to avoid thread key cleanup loop. */
	(void) pthread_setspecific(thread_key, NULL);

	/* Dump the list of allocated memory at thread exit. */
	if (block != NULL) {
		if (main_thread == pthread_self())
			err_msg("main_thread:\r\n");
		else
			err_msg("thread #%lx:\r\n", (unsigned long) pthread_self());

		for (count = 0; block != NULL; block = block->next, count++) {
			if (block->crc != ((unsigned long) block ^ block->size)) {
				signal_error("thread #%lu memory marker corruption " BLOCK_FMT "\r\n", (unsigned long) pthread_self(), BLOCK_ARG(block));
				return;
			}
			DebugMallocDump(&block[1]);
		}

		err_msg("\tleaked blocks=%u\r\n", count);

		if (memory_thread_leak && 0 < count)
			signal_error("thread report abort\r\n");
	}
}

void
DebugMallocReport(void)
{
	thread_exit_report(pthread_getspecific(thread_key));
}

void
err_msgv(char *fmt, va_list args)
{
	int len;
	char buf[255];

	if (fmt != NULL) {
		len = vsnprintf(buf, sizeof (buf), fmt, args);
		(void) write(STDERR_FILENO, buf, len);
	}
}

void
err_msg(char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	err_msgv(fmt, args);
	va_end(args);
}

static void
dump(unsigned char *chunk, size_t size)
{
	unsigned char hex[4];

	hex[0] = '\\';
	hex[1] = 'x';

	for ( ; 0 < size; size--, chunk++) {
		if (isprint(*chunk)) {
			(void) write(2, chunk, 1);
		} else {
			hex[3] = *chunk & 0xF;
			hex[3] += (hex[3] < 0xA) ? '0' : ('A' - 10);

			hex[2] = (*chunk >> 4) & 0xF;
			hex[2] += (hex[2] < 0xA) ? '0' : ('A' - 10);

			(void) write(STDERR_FILENO, hex, sizeof (hex));
		}
	}

	write(STDERR_FILENO, "\r\n", STRLEN("\r\n"));
}

static void
signal_error(char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	err_msgv(fmt, args);
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
DebugMallocSummary(void)
{
	if (main_thread == 0 || main_thread == pthread_self())
		err_msg("main_thread:\r\n");
	else
		err_msg("thread #%lx:\r\n", (unsigned long) pthread_self());

	err_msg(
		"\t malloc: %-14lu  free: %-14lu  lost: %-14ld\r\n",
		memory_allocation_count, memory_free_count,
		memory_allocation_count - memory_free_count
	);

	err_msg(
		"\t_malloc: %-14lu _free: %-14lu _lost: %-14ld _used: %-14ld\r\n",
		static_allocation_count, static_free_count,
		static_allocation_count - static_free_count,
		static_used
	);
}

void
DebugMallocDump(void *chunk)
{
	if (memory_init_state == MEMORY_INITIALISED) {
		BlockData *block = &((BlockData *) chunk)[-1];
		err_msg("\t" BLOCK_FMT " dump=", BLOCK_ARG(block));
		dump((unsigned char *)chunk, block->size < 40 ? block->size : 40);
	}
}

static void
DebugMallocHere0(void *chunk, const char *file, unsigned line, const char *tag)
{
	if (memory_init_state == MEMORY_INITIALISED) {
		BlockData *block = &((BlockData *) chunk)[-1];
		err_msg(HERE_FMT "%s " BLOCK_FMT "\r\n", HERE_ARG, tag, BLOCK_ARG(block));
	}
}

void
DebugMallocHere(void *chunk, const char *file, unsigned line)
{
	DebugMallocHere0(chunk, file, line, "info");
}

static void
init_common(void)
{
	char *value;

	if ((value = getenv("MEMORY")) != NULL) {
		memory_show_free = (strchr(value, 'F') != NULL);
		memory_show_malloc = (strchr(value, 'M') != NULL);
		memory_thread_leak = (strchr(value, 'T') != NULL);
		memory_signal = (strchr(value, 'S') == NULL) ? SIGSEGV : 0;
		memory_exit = (strchr(value, 'E') == NULL);
	}

	if ((value = getenv("MEMORY_SIGNUM")) != NULL)
		memory_signal = (int) strtol(value, NULL, 0);
	if ((value = getenv("MEMORY_FREE_CHUNK")) != NULL)
		memory_free_chunk = (void *) strtol(value, NULL, 0);
	if ((value = getenv("MEMORY_MALLOC_CHUNK")) != NULL)
		memory_malloc_chunk = (void *) strtol(value, NULL, 0);
	if ((value = getenv("MEMORY_REPORT_INTERVAL")) != NULL)
		memory_report_interval = (unsigned long) strtol(value, NULL, 0);
#ifdef __WIN32__
	if (atexit(DebugMallocReport)) {
		err_msg(HERE_FMT "atexit() error\r\n", __FILE__, __LINE__);
		exit(EX_OSERR);
	}
	if (atexit(DebugMallocSummary)) {
		err_msg(HERE_FMT "atexit() error\r\n", __FILE__, __LINE__);
		exit(EX_OSERR);
	}
#endif
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
		err_msg("free failed chunk=%lx", (long) chunk);
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
		err_msg("free failed chunk=%lx", (long) chunk);
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
		err_msg("libc.so load error: %s\r\n", err);
		exit(EX_OSERR);
	}

  	libc_malloc = (void *(*)(size_t)) dlsym(libc, "malloc");
	if ((err = dlerror()) != NULL) {
		err_msg("libc malloc() not found: %s\r\n", err);
		exit(EX_OSERR);
	}

  	libc_free = (void (*)(void *)) dlsym(libc, "free");
	if ((err = dlerror()) != NULL) {
		err_msg("libc free() not found: %s\r\n", err);
		exit(EX_OSERR);
	}

  	libc__exit = dlsym(libc, "_exit");
	if ((err = dlerror()) != NULL) {
		err_msg("libc _exit() not found: %s\r\n", err);
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

void
(_exit)(int status)
{
	/* Have to initialise libc__exit before we can call it. */
	if (memory_init_state == MEMORY_INITIALISE)
		init();

	DebugMallocSummary();
	DebugMallocReport();

	(*libc__exit)(status);
}

void
(free)(void *chunk)
{
	DebugFree(chunk, unknown, 0);
}

void *
(malloc)(size_t size)
{
	return DebugMalloc(size, unknown, 0);
}

void *
(calloc)(size_t m, size_t n)
{
	return DebugCalloc(m, n, unknown, 0);
}

void *
(realloc)(void *chunk, size_t size)
{
	return DebugRealloc(chunk, size, unknown, 0);
}

static void
DebugCheckBoundaries(BlockData *block, const char *file, unsigned line)
{
	unsigned char *p, *q;
	unsigned char *chunk = (unsigned char *) &block[1];

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
		if (*p != memory_upper_marker) {
			signal_error(HERE_FMT "memory underun (a) " BLOCK_FMT "\r\n", HERE_ARG, BLOCK_ARG(block));
			return;
		}
	}

	/* Check the leading boundary marker */
	p = block->before;
	q = p + sizeof (block->before);
	for ( ; p < q; p++) {
		if (*p != memory_lower_marker) {
			signal_error(HERE_FMT "memory underun (b) " BLOCK_FMT "\r\n", HERE_ARG, BLOCK_ARG(block));
			return;
		}
	}

	/* Check the boundary marker between end of chunk and end of block. */
	p = (unsigned char *) &block[1] + block->size;
	q = p + ALIGN_PAD(block->size, ALIGNMENT_SIZE) + ALIGNMENT_SIZE;
	for ( ; p < q; p++) {
		if (*p != memory_upper_marker) {
			signal_error(HERE_FMT "memory overrun " BLOCK_FMT "\r\n", HERE_ARG, BLOCK_ARG(block));
			return;
		}
	}

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
}

void
(DebugFree)(void *chunk, const char *file, unsigned line)
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

	if (block->crc != ((unsigned long) block ^ size)) {
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
		DebugMallocHere0(chunk, file, line, "free");

	DebugCheckBoundaries(block, HERE_ARG);

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
	if (chunk == memory_free_chunk)
		signal_error(HERE_FMT "memory stop free on chunk=0x%lx\r\n", HERE_ARG, (long) chunk);

	if (0 < memory_report_interval && memory_free_count % memory_report_interval == 0)
		DebugMallocSummary();
}

void *
(DebugMalloc)(size_t size, const char *file, unsigned line)
{
	void *chunk;
	size_t block_size;
	BlockData *block, *head;

	/* malloc() before main() can happen with OpenBSD. */
	switch (memory_init_state) {
	case MEMORY_INITIALISED:
		break;

	case MEMORY_INITIALISE:
		init();
		/*@fallthrough@*/

	case MEMORY_INITIALISING:
		if (line == 0)
			return _malloc(size);

		/* OpenBSD allocates more memory when some pthread functions
		 * are first used. Force it here so as to allocate it from
		 * _malloc(). We can't do this in init(), since the first call
		 * happens during thread initilisation and would cause a loop.
		 */
		(void) LOCK_INIT(&lock);
		(void) LOCK_LOCK(&lock);
		(void) LOCK_UNLOCK(&lock);
		(void) pthread_key_create(&thread_key, thread_exit_report);
		(void) pthread_setspecific(thread_key, NULL);

		memory_init_state = MEMORY_INITIALISED;
		main_thread = pthread_self();
	}

	/* Allocate enough space to include some boundary markers.
	 *
	 * block.before	| DA DA DA DA DA DA DA DA ...
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
	(void) memset(block, memory_upper_marker, block_size);
	(void) memset(block->before, memory_lower_marker, sizeof (block->before));

	/* Zero the chunk. */
	(void) memset((char *) chunk, 0, size);

	/* Maintain list of allocated memory per thread, including main() */
	head = pthread_getspecific(thread_key);
	block->prev = NULL;
	block->next = head;
	if (head != NULL)
		head->prev = block;
	(void) pthread_setspecific(thread_key, block);

	block->crc = (unsigned long) block ^ size;
	block->size = size;
	block->file = file;
	block->line = line;

#ifdef NOPE
	/* When we allocate a previously freed chunk, write a single value
	 * to the start of the chunk to prevent inadvertant double-free
	 * detection in case the caller never writes anything into this chunk.
	 */
	if (0 < size)
		*(unsigned char *) chunk = MALLOC_BYTE;
#endif
	if (memory_show_malloc)
		DebugMallocHere0(chunk, file, line, "malloc");

	if (chunk == memory_malloc_chunk)
		signal_error(HERE_FMT "memory stop malloc on " BLOCK_FMT "\r\n", HERE_ARG, BLOCK_ARG(block));

	return chunk;
}

void *
(DebugRealloc)(void *chunk, size_t size, const char *file, unsigned line)
{
	void *replacement;
	BlockData *block;

	if ((replacement = DebugMalloc(size, HERE_ARG)) == NULL)
		return NULL;

	if (chunk != NULL) {
		block = &((BlockData *) chunk)[-1];
		(void) memcpy(replacement, chunk, block->size < size ? block->size : size);
		free(chunk);
	}

	return replacement;
}

void *
(DebugCalloc)(size_t m, size_t n, const char *file, unsigned line)
{
	void *chunk;

	if ((chunk = DebugMalloc(m * n, HERE_ARG)) != NULL)
		memset(chunk, 0, m * n);

	return chunk;
}
