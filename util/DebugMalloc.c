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
#define MAX_STATIC_MEMORY	(64 * 1024)
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
#define SIGNAL_MEMORY		SIGSEGV
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
#include <sys/types.h>

#include <com/snert/lib/sys/pthread.h>
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

#if !defined(__unix__) && !defined(HAVE_PTHREAD_KEY_CREATE)
# undef DEBUG_MALLOC_THREAD_REPORT
#endif

#ifdef DEBUG_MALLOC_THREAD_REPORT
# define DEBUG_MALLOC_MUTEX
#endif

typedef enum {
	MEMORY_INITIALISE, MEMORY_INITIALISING, MEMORY_INITIALISED
} MemoryInit;

typedef struct block_data {
	unsigned char before[PAD_SIZE];
	size_t size;
	unsigned line;
	const char *file;
#if defined(DEBUG_MALLOC_THREAD_REPORT)
	struct block_data *prev;
	struct block_data *next;
#endif
	unsigned long crc;
	unsigned char after[PAD_SIZE];
} BlockData;

#define ALIGN_SIZE(sz, pow2)	((((sz)-1) & ~(pow2-1)) + pow2)
#define ALIGN_PAD(sz, pow2)	(ALIGN_SIZE(sz, pow2) - sz)

static const char *unknown = "(unknown)";
static void msg(char *fmt, ...);
static void msgv(char *fmt, va_list args);
static void dump(unsigned char *chunk, size_t size);
static void signal_error(char *fmt, ...);

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
#ifdef DEBUG_MALLOC_MUTEX
static pthread_mutex_t memory_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

int memory_show_free = 0;
int memory_show_malloc = 0;
int memory_thread_leak = 0;
int memory_raise_signal = 1;
int memory_raise_and_exit = 1;
int memory_test_double_free = 1;
void *memory_free_chunk = NULL;
void *memory_malloc_chunk = NULL;
unsigned long memory_report_interval = 0;

/***********************************************************************
 ***
 ***********************************************************************/

#if defined(DEBUG_MALLOC_THREAD_REPORT)
/*** THIS CODE NEEDS TO BE REVIEWED. NOT STABLE IN THREADED CODE. ***/

#define IS_NULL(p)	((p) == NULL || ((unsigned long)(p) & 0xFF) == FREED_BYTE)

enum { THREAD_KEY_INITIALISE, THREAD_KEY_INITIALISING, THREAD_KEY_READY, THREAD_KEY_BUSY };

static pthread_key_t thread_malloc_list;
/* static pthread_mutex_t msg_mutex = PTHREAD_MUTEX_INITIALIZER; */
static volatile int thread_key_state = THREAD_KEY_INITIALISE;
static int (*lib_pthread_create)(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine)(void *), void *arg);

void
DebugMallocThreadReport(void *data)
{
	void *chunk;
	unsigned count = 0;
	BlockData *block = data;

	if (block != NULL) {
		for ( ; block != NULL; block = block->next, count++) {
			chunk = &block[1];

			if (block->crc != ((unsigned long) block ^ block->size)) {
				signal_error("thread #%lu memory marker corruption chunk=0x%lx size=%lu\r\n", (unsigned long) pthread_self(), (long) chunk,  block->size);
				return;
			}

			msg("thread #%lu chunk=0x%lx size=%lu file=%s:%u dump=", (unsigned long) pthread_self(), (long) chunk, block->size, block->file, block->line);
			dump(chunk, block->size < 40 ? block->size : 40);
			msg("\r\n");
		}

		thread_key_state = THREAD_KEY_BUSY;
		(void) pthread_setspecific(thread_malloc_list, NULL);
		thread_key_state = THREAD_KEY_READY;

		msg("thread #%lu allocated blocks=%u\r\n", (unsigned long) pthread_self(), count);

		if (memory_thread_leak && 0 < count)
			signal_error("thread report abort\r\n");
	}
}

int
pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine)(void *), void *arg)
{
	/* Must wait until the pthread library has been initialised
	 * before we can initialise the thread key. If we attempt to
	 * use pthread_key_create() in init(), then we may get a
	 * segmentation fault.
	 */
	if (thread_key_state == THREAD_KEY_INITIALISE) {
		thread_key_state = THREAD_KEY_INITIALISING;
		if (!pthread_key_create(&thread_malloc_list, DebugMallocThreadReport))
			thread_key_state = THREAD_KEY_READY;
	}

	return (*lib_pthread_create)(thread, attr, start_routine, arg);
}

#endif

static void
msgv(char *fmt, va_list args)
{
	int len;
	char buf[255];

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

			(void) write(2, hex, sizeof (hex));
		}
	}
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

static void
init_common(void)
{
	char *value;

	if ((value = getenv("MEMORY")) != NULL) {
		memory_show_free = (strchr(value, 'F') != NULL);
		memory_show_malloc = (strchr(value, 'M') != NULL);
		memory_raise_signal = (strchr(value, 'S') == NULL);
		memory_raise_and_exit = (strchr(value, 'E') == NULL);
		memory_thread_leak = (strchr(value, 'T') != NULL);
	}

	if ((value = getenv("MEMORY_FREE_CHUNK")) != NULL)
		memory_free_chunk = (void *) strtol(value, NULL, 0);
	if ((value = getenv("MEMORY_MALLOC_CHUNK")) != NULL)
		memory_malloc_chunk = (void *) strtol(value, NULL, 0);
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

#if defined(DEBUG_MALLOC_THREAD_REPORT) && defined(LIBPTHREAD_PATH)
	handle = dlopen(LIBPTHREAD_PATH, RTLD_NOW);
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

#undef free
#undef malloc
#undef calloc
#undef realloc

void
free(void *chunk)
{
	DebugFree(chunk);
}

void *
malloc(size_t size)
{
	return DebugMalloc(size, unknown, 0);
}

void *
calloc(size_t m, size_t n)
{
	return DebugCalloc(m, n, unknown, 0);
}

void *
realloc(void *chunk, size_t size)
{
	return DebugRealloc(chunk, size, unknown, 0);
}

static void
DebugCheckBoundaries(BlockData *block)
{
	unsigned char *p, *q;
	unsigned char *chunk = (unsigned char *) &block[1];

	/* Check the boundary marker between block and chunk. */
	p = block->after;
	q = chunk;
	for ( ; p < q; p++) {
		if (*p != DOH_BYTE) {
			signal_error("memory underun (a) chunk=0x%lx size=%lu\r\n", (long) chunk, block->size);
			return;
		}
	}

	/* Check the leading boundary marker */
	p = block->before;
	q = p + sizeof (block->before);
	for ( ; p < q; p++) {
		if (*p != DAH_BYTE) {
			signal_error("memory underun (b) chunk=0x%lx size=%lu\r\n", (long) chunk, block->size);
			return;
		}
	}


	/* Check the boundary marker between end of chunk and end of block. */
	p = (unsigned char *) &block[1] + block->size;
	q = p + ALIGN_PAD(block->size, ALIGNMENT_SIZE) + ALIGNMENT_SIZE;
	for ( ; p < q; p++) {
		if (*p != DOH_BYTE) {
			signal_error("memory overrun chunk=0x%lx size=%lu\r\n", (long) chunk, block->size);
			return;
		}
	}
}

void
DebugFree(void *chunk)
{
	BlockData *block;
	unsigned char *p, *q;
	size_t size, block_size;

	if (chunk == NULL)
		return;

	switch (memory_init_state) {
	case MEMORY_INITIALISED:
		if (_free(chunk))
			return;
		break;

	case MEMORY_INITIALISING:
		if (!_free(chunk)) {
			raise(SIGNAL_MEMORY);
			exit(EX_DATAERR);
		}
		return;

	case MEMORY_INITIALISE:
		init();
		break;
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
		/* Can't report size as it is probably bogus. */
		if (*(unsigned char *) chunk == FREED_BYTE) {
			signal_error("double free chunk=0x%lx\r\n", (long) chunk);
			return;
		}

		signal_error("memory corruption chunk=0x%lx\r\n", (long) chunk);
		return;
	}

	/* Is the size too big to be true? */
	if (size & ~((~ (size_t) 0) >> 1))
		memory_test_double_free = 0;

#if defined(DEBUG_MALLOC_THREAD_REPORT)
	if (pthread_mutex_lock(&memory_mutex))
		signal_error("free mutex lock: %s (%d)\r\n", strerror(errno), errno);

	if (!IS_NULL(block->prev)) {
		block->prev->next = block->next;
	} else {
		/* Update thread's pointer to the head of it's allocation list. */
		thread_key_state = THREAD_KEY_BUSY;
		(void) pthread_setspecific(thread_malloc_list, block->next);
		thread_key_state = THREAD_KEY_READY;

		if (!IS_NULL(block->next))
			block->next->prev = NULL;
	}

	if (!IS_NULL(block->next))
		block->next->prev = block->prev;
	else if (!IS_NULL(block->prev))
		block->prev->next = NULL;

	if (pthread_mutex_unlock(&memory_mutex))
		signal_error("free mutex unlock: %s (%d)\r\n", strerror(errno), errno);
#endif
	if (memory_test_double_free) {
		/* Look for double-free first, since the block structure
		 * may have been overwritten by a previous free() causing
		 * a false positive for a memory underrun.
		 */
		p = (unsigned char *) chunk;
		q = p + size;
		for ( ; p < q; p++) {
			if (*p != FREED_BYTE)
				break;
		}

		if (0 < size && q <= p) {
			signal_error("double-free chunk=0x%lx size=%lu\r\n", (long) chunk, size);
			return;
		}
	}

	DebugCheckBoundaries(block);

	if (memory_test_double_free) {
		/* Wipe the WHOLE memory area including the space
		 * for the lower and upper boundaries.
		 */
		memset(block, FREED_BYTE, block_size);
	}

	/* The libc free has its own mutex locking. */
	(*libc_free)(block);

#ifdef DEBUG_MALLOC_MUTEX
	if (pthread_mutex_lock(&memory_mutex))
		signal_error("free mutex lock: %s (%d)\r\n", strerror(errno), errno);
#endif
	memory_free_count++;

#ifdef DEBUG_MALLOC_MUTEX
	if (pthread_mutex_unlock(&memory_mutex))
		signal_error("free mutex lock: %s (%d)\r\n", strerror(errno), errno);
#endif
#ifdef __WIN32__
/* On some systems like OpenBSD with its own debugging malloc()/free(),
 * once freed you cannot access the memory without causing a segmentation
 * fault. Windows is less strict.
 */
	/* Windows appears to do its own double-free detection after a free(). */
	if (0 < size && ((unsigned char *) chunk)[size - 1] != FREED_BYTE)
		memory_test_double_free = 0;
#endif
	if (memory_show_free)
		msg("free chunk=0x%lx size=%lu\r\n", (unsigned long) chunk, size);

	if (chunk == memory_free_chunk)
		signal_error("memory stop free on chunk=0x%lx\r\n", (long) chunk);

	if (0 < memory_report_interval && memory_free_count % memory_report_interval == 0)
		DebugMallocReport();
}

void *
DebugMalloc(size_t size, const char *file, unsigned line)
{
	void *chunk;
	BlockData *block;
	size_t block_size;

	switch (memory_init_state) {
	case MEMORY_INITIALISED:
		break;

	case MEMORY_INITIALISING:
		/* Ignore mutex locking, assumes single threaded. */
		return _malloc(size);

	case MEMORY_INITIALISE:
		init();
		break;
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
	block = (*libc_malloc)(block_size);

	if (block == NULL)
		return NULL;

#ifdef DEBUG_MALLOC_MUTEX
	if (pthread_mutex_lock(&memory_mutex))
		signal_error("malloc mutex lock: %s (%d)\r\n", strerror(errno), errno);
#endif
	memory_allocation_count++;

#if defined(DEBUG_MALLOC_THREAD_REPORT)
	block->prev = block->next = NULL;

	if (thread_key_state == THREAD_KEY_READY) {
		BlockData *head = pthread_getspecific(thread_malloc_list);

		if (!IS_NULL(head)) {
			block->prev = NULL;
			block->next = head;
			head->prev = block;
		}

		/* pthread_setspecific() might allocate memory first
		 * time it is called for a new thread, which might
		 * cause recursion if we don't take care to skip this
		 * section while in pthread_setspecific().
		 */
		thread_key_state = THREAD_KEY_BUSY;
		(void) pthread_setspecific(thread_malloc_list, block);
		thread_key_state = THREAD_KEY_READY;
	}
#endif
#ifdef DEBUG_MALLOC_MUTEX
	if (pthread_mutex_unlock(&memory_mutex))
		signal_error("malloc mutex unlock: %s (%d)\r\n", strerror(errno), errno);
#endif
	block->crc = (unsigned long) block ^ size;
	block->size = size;
	block->file = file;
	block->line = line;

	/* The chunk to return. */
	chunk = &block[1];

	/* Fill in the boundary markers. */
	memset(block->before, DAH_BYTE, sizeof (block->before));
	memset(block->after, DOH_BYTE, chunk - (void *) block->after);
	memset((char *) chunk + size, DOH_BYTE, ALIGN_PAD(size, ALIGNMENT_SIZE) + ALIGNMENT_SIZE);

	/* When we allocate a previously freed chunk, write a single value
	 * to the start of the chunk to prevent inadvertant double-free
	 * detection in case the caller never writes anything into this chunk.
	 */
	if (0 < size)
		*(unsigned char *) chunk = MALLOC_BYTE;

	if (memory_show_malloc)
		msg("malloc chunk=0x%lx size=%lu\r\n", (unsigned long) chunk, size);

	if (chunk == memory_malloc_chunk)
		signal_error("memory stop malloc on chunk=0x%lx\r\n", chunk);

	return chunk;
}

void *
DebugRealloc(void *chunk, size_t size, const char *file, unsigned line)
{
	void *replacement;
	BlockData *block;

	if ((replacement = DebugMalloc(size, file, line)) == NULL)
		return NULL;

	if (chunk != NULL) {
		block = &((BlockData *) chunk)[-1];
		memcpy(replacement, chunk, block->size < size ? block->size : size);
		free(chunk);
	}

	return replacement;
}

void *
DebugCalloc(size_t m, size_t n, const char *file, unsigned line)
{
	void *chunk;

	if ((chunk = DebugMalloc(m * n, file, line)) != NULL)
		memset(chunk, 0, m * n);

	return chunk;
}
