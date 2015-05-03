/*
 * malloc.c
 *
 * Copyright 2015 by Anthony Howe.  All rights reserved.
 *
 * Inspired by Armin Biere's ccmalloc. This version is far less
 * complex and only detects five types of memory errors:
 *
 *   -	double-free
 *   -	memory leaks
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
 * See man 3 malloc or man jemalloc about MALLOC_OPTIONS.
 */

#ifndef MAX_STATIC_MEMORY
#define MAX_STATIC_MEMORY		(16 * 1024)
#endif

#if !defined(TEST) && defined(NDEBUG)
# define MALLOC_OPTIONS_DEFAULT		""
#else
# define MALLOC_OPTIONS_DEFAULT		"AL"
#endif

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>
#include <com/snert/lib/sys/sysexits.h>

#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#ifndef LIBC_PATH
#define LIBC_PATH		"libc.so"
#endif

/* Boundary bytes. */
#define LO_BYTE			'<'
#define HI_BYTE			'>'

/* See jemalloc J options. */
#define J_ALLOC_BYTE		0xA5
#define J_FREED_BYTE		0x5A	/* ASCII 'Z' */

#ifdef TEST
# define LOGMSG(...)		(void) fprintf(stderr, __VA_ARGS__)
# define LOGTRACE()		LOGMSG("%s:%d\n", __func__, __LINE__)
# define LOGMALLOC(p,n)		LOGMSG("%s: ptr=%p size=%zu\n", __func__, p, n)
# define LOGFREE(p)		LOGMSG("%s: ptr=%p\n", __func__, p)
#else
# define LOGMSG(...)
# define LOGTRACE()
# define LOGMALLOC(p,n)
# define LOGFREE(p)
#endif

/* This alignment known to work for Linux, OpenBSD, and Windows.  Too
 * small and OpenBSD barfs on thread context switching.  Windows XP
 * appears to have a smaller alignment of 8.
 */
#ifndef ALIGNMENT_SIZE
#define ALIGNMENT_SIZE 		16	/* Must be a power of 2. */
#endif

#define ALIGN_SIZE(sz, pow2)	((((sz)-1) & ~(pow2-1)) + pow2)

typedef void *Marker;

typedef struct {
	size_t crc;
	size_t size;
	Marker lo_guard;
} block_prefix;

/* This CRC is basic, cheap, fast; pick any two. */
#define BLOCK_CRC(p)		((size_t)((size_t)(p) ^ (size_t)(p)->lo_guard) ^ (p)->size)

static void _free(void *chunk);
static void *_malloc(size_t size);

void (*hook__exit)(int);
void (*hook_free)(void *);
void *(*hook_malloc)(size_t);

static __dead void
fatal(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	(void) vfprintf(stderr, fmt, args);
	(void) fputs("\r\n", stderr);
	va_end(args);

	/* You cannot catch SIGABRT from abort(); it always dumps
	 * core and terminates regardless. 
	 */
	raise(SIGABRT);
}

static void
stub_fatal(const char *fmt, ...)
{
	/* Do nothing. */
}

const char *_alt_malloc_options = MALLOC_OPTIONS_DEFAULT;

static int opt_leaks;
static int opt_zero_size;	
static void (*opt_abort)(const char *, ...) = stub_fatal;
static void (*opt_xalloc)(const char *, ...) = stub_fatal;

static Marker lo_guard;
static Marker freed;

static void
init_common(void)
{
	char *var;
	
	if ((var = getenv("ALT_MALLOC_OPTIONS")) != NULL)
		_alt_malloc_options = var;
	
	/* Based on *BSD jemalloc debug options. */
	opt_abort = (strchr(_alt_malloc_options, 'A') != NULL) ? fatal : stub_fatal;
	opt_xalloc = (strchr(_alt_malloc_options, 'X') != NULL) ? fatal : stub_fatal;
	opt_zero_size = (strchr(_alt_malloc_options, 'V') != NULL);
	opt_leaks = (strchr(_alt_malloc_options, 'L') != NULL);
	
	(void) memset(&lo_guard, LO_BYTE, sizeof (lo_guard));
	(void) memset(&freed, J_FREED_BYTE, sizeof (freed));
}

#if defined(__WIN32__)
# include <windows.h>

void *
alt_malloc(size_t size)
{
	LOGMALLOC(size);
# ifdef USE_LOCAL_FAMILY
	return LocalAlloc(LPTR, size);
# else
	return HeapAlloc(GetProcessHeap(), 0, size);
# endif
}

void
alt_free(void *chunk)
{
	LOGFREE(chunk);
# ifdef USE_LOCAL_FAMILY
	if (LocalFree(chunk) != NULL) {
# else
	if (!HeapFree(GetProcessHeap(), 0, chunk)) {
# endif
		(void) fprintf(stderr, "free failed chunk=%lx", (long) chunk);
		abort();
	}
}

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

	LOGMSG("orig=%p", orig);
	size = strlen(orig) + 1;
	if ((copy = malloc(size)) != NULL)
		(void) memcpy(copy, orig, size);

	return copy;
}

static void
hook_init(void)
{
	hook_malloc = alt_malloc;
	hook_free = alt_free;
	hook__exit = _exit;
	init_common();
	LOGTRACE();
}

#elif defined(HAVE_DLFCN_H) && defined(LIBC_PATH)
# include <dlfcn.h>

static void
hook_init(void)
{
	void *libc;
	const char *err;
	void (*libc__exit)(int);
	void (*libc_free)(void *);
	void *(*libc_malloc)(size_t);

	/* Setup boot strap memory allocation during first malloc()
	 * while we fetch libc's malloc() and free() adfresses.
	 */
	hook_free = _free;
	hook_malloc = _malloc;

	LOGTRACE();

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

	(void) dlclose(libc);
	init_common();

	/* Switch from boot strap memory allocation to libc's.  The
	 * program can later replace the hooks with its own memory
	 * manager.
	 */
	hook__exit = libc__exit;
	hook_malloc = libc_malloc;
	hook_free = libc_free;
}

#elif !defined(LIBC_PATH)
# error "LIBC_PATH is undefined."
#else
# error "Unable to hook memory management on this system."
#endif

/*
 * These counters are not really thread safe as they're not
 * protected by a semaphore or mutex.  I'm not too fussed as
 * they're more a guide as to possible memory leaks.
 *
 * Note too that some systems will use malloc() and free()
 * during the application initialisation sequence before
 * reaching main() and that memory will probably be counted
 * as lost.
 */
static volatile unsigned long free_count = 0;
static volatile unsigned long malloc_count = 0;
static volatile unsigned long static_free_count = 0;
static volatile unsigned long static_malloc_count = 0;

static char static_memory[MAX_STATIC_MEMORY];
static size_t static_used;

/*
 * During program initialisation, we have to assign memory from
 * a static block.  It is assumed that the program at this stage
 * is still a single thread.  Any memory freed at this stage
 * must have been assigned by _malloc() and is abandoned.
 */
static void
_free(void *chunk)
{
	if (static_memory <= (char *) chunk && (char *) chunk < static_memory + static_used) {
		static_free_count++;
		LOGFREE(chunk);
	}
}

/*
 * During program initialisation, we have to assign memory from
 * a static block.  It is assumed that the program at this stage
 * is still a single thread.
 */
static void *
_malloc(size_t size)
{
	void *chunk;
	size_t aligned_size;

	/* Allocate static memory in aligned chunks. */
	aligned_size = ALIGN_SIZE(size, ALIGNMENT_SIZE);

	/* Do we have enough static memory? */
	if (sizeof (static_memory) <= static_used + aligned_size) {
		(*opt_xalloc)("out of static memory");
		LOGMALLOC(NULL, size);
		errno = ENOMEM;
		return NULL;
	}

	chunk = (void *) (static_memory + static_used);	
	static_used += aligned_size;
	static_malloc_count++;
	
	LOGMALLOC(chunk, size);

	return chunk;
}

/*
 * @note
 *	free() before main() can happen with OpenBSD. 
 */
void
(free)(void *chunk)
{
	char *chunky;
	Marker *markers;
	block_prefix *block;
	size_t aligned_size, n;
	
	LOGFREE(chunk);
	if (chunk == NULL)
		return;
	
	if (hook_free == NULL) {
		(*opt_abort)("free() before any malloc().");
		return;
	}

	block = &((block_prefix *) chunk)[-1];	

	/* Underflow control check. */
	if (block->crc != BLOCK_CRC(block)) {
		(*opt_abort)("buffer under run or bad pointer!");
		return;
	}

	/* Double-free check.  Smallest allocated chunk is a size
	 * aligned to sizeof Marker plus the hi_guard, essentially
	 * at least two Markers.
	 */
	markers = chunk;
	if (markers[0] == freed && markers[1] == freed) {
		(*opt_abort)("doube-free!");
		return;
	}
	
	/* Overflow control check (sizeof Marker plus delta). */
	n = block->size;
	aligned_size = ALIGN_SIZE(n, sizeof (Marker)) + sizeof (Marker);	
	for (chunky = chunk + n; n < aligned_size; n++) {
		if (*chunky++ != HI_BYTE) {
			(*opt_abort)("buffer over run!");
			return;
		}
	}

	/* Mark the start of the old chunk with the freed bytes
	 * for future double free checks.
	 */
	markers[0] = freed;
	markers[1] = freed;

	(*hook_free)(block);
	free_count++;
}

/*
 * @note
 *	malloc() before main() can happen with OpenBSD. 
 */
void *
(malloc)(size_t size)
{
	void *chunk;
	char *chunky;
	block_prefix *block;
	size_t aligned_size, n;

	if (hook_malloc == NULL)
		hook_init();
		
	if (opt_zero_size && size == 0)
		return NULL;
	if (size == 0)
		size = 1;
				
	/* Allocate memory in aligned chunks plus upper boundary. */
	aligned_size = ALIGN_SIZE(size, sizeof (Marker)) + sizeof (Marker);
	
	/* We have to replace calloc() and realloc() too since they may
	 * use internal entry points.  In which case, we have to track
	 * the object size so we can implement realloc() properly.  
	 */
	if ((block = (*hook_malloc)(sizeof (*block) + aligned_size)) != NULL) {
		malloc_count++;

		/* Basic underflow control. */
		block->size = size;
		block->lo_guard = lo_guard;
		block->crc = BLOCK_CRC(block);
		
		chunk = &block[1];

		/* Basic overflow control.  Fill in sizeof Marker plus delta. */
		for (n = size, chunky = chunk + n; n < aligned_size; n++)
			*chunky++ = HI_BYTE;		

		/* Disrupt double-free markers in case the allocated
		 * memory is not written over before being freed.  
		 */
		*(char *)chunk = 0x0;		
	} else {
		(*opt_xalloc)("out of memory!");
	}
	LOGMALLOC(chunk, size);
			
	return (void *)chunk;
}

void *
(calloc)(size_t n_objects, size_t object_size)
{
	void *chunk;
	size_t size;
	
	size = n_objects * object_size;
	if ((chunk = (malloc)(size)) != NULL) {
		(void) memset(chunk, 0, size);
	}		
		
	return chunk;
}

void *
(realloc)(void *orig, size_t size)
{
	void *chunk;
	size_t osize = 0;
	
	if ((chunk = (malloc)(size)) != NULL && orig != NULL) {
		osize = ((size_t *)orig)[-1];
		(void) memcpy(chunk, orig, osize < size ? osize : size);
		(free)(orig);
	}
		
	return chunk;
}

#define is_power_two(x)		(((x) != 0) && !((x) & ((x) - 1)))

void *
(aligned_alloc)(size_t alignment, size_t size)
{
	if (is_power_two(alignment) && sizeof (void *) <= alignment && (size / alignment) * alignment == size)
		return (malloc)(size);
	return NULL;
}

/*
 * We hook _exit() so that an alternative malloc can do any cleanup
 * and/or reporting.
 */
__dead void
(_exit)(int ex_code)
{
	char buffer[128];

	/* Have to initialise hook__exit before we can call it. */
	if (hook__exit == NULL)
		hook_init();

	/* Force any previous stdio buffers to be freed. */
	(void) setvbuf(stdin, NULL, _IONBF, 0);
	(void) setvbuf(stdout, NULL, _IONBF, 0);

	/* Use a temporary "static" output buffer. */
	(void) setvbuf(stderr, buffer, _IOLBF, sizeof (buffer));

	if (opt_leaks) {
		if (static_malloc_count != static_free_count)
			(void) fprintf(stderr, "_malloc=%lu _free=%lu\n", static_malloc_count, static_free_count);
		if (malloc_count != free_count)
			(void) fprintf(stderr, "malloc=%lu free=%lu\n", malloc_count, free_count);
	}
	LOGMSG("ex_code=%d\n", ex_code);		
	(*hook__exit)(ex_code);
}

/***********************************************************************
 *** Test Suite
 ***********************************************************************/

#ifdef TEST
#include <signal.h>

#if HAVE_INTTYPES_H
# include <inttypes.h>
#else
# if HAVE_STDINT_H
# include <stdint.h>
# endif
#endif

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

typedef enum { PASS, FAIL, SIGNAL } TestRc;

struct diagnostic {
	TestRc (*test)(void);
	TestRc expectedStatus;
	const char *explanation;
};

static char *allocation;
static volatile int fault_found = 0;
static const char *status_names[] = { "-OK-", "FAIL", "SIGNAL" };

static void
fault_handler(int signum)
{
	fault_found = 1;
}

static TestRc
find_fault(const struct diagnostic *diag)
{
	TestRc status;

	(void) printf("%s...", diag->explanation);

	fault_found = 0;
	status = (*diag->test)();
	if (fault_found)
		status = SIGNAL;

	if (status != diag->expectedStatus)
		status = FAIL;	
	(void) printf("%s\r\n", status_names[status]);	

	return status;
}

static TestRc
testSizes(void)
{
	return sizeof (unsigned long) < sizeof (void *);
}

static TestRc
size_intptr(void)
{
	return sizeof (intptr_t) < sizeof (void *);
}

static TestRc
allocateMemory(void)
{
	allocation = (char *) malloc(1);

	return allocation == NULL;

}

static TestRc
freeMemory(void)
{
	free(allocation);

	return PASS;
}

static TestRc
write0(void)
{
	*allocation = 1;

	return PASS;
}

static TestRc
write_over(void)
{
	allocation[1] = 0xFF;

	return PASS;
}

static TestRc
write_under(void)
{
	allocation[-1] = 0xFF;

	return PASS;
}

static TestRc
corruptPointer(void)
{
	allocation += sizeof (void *);

	return PASS;
}

static struct diagnostic diagnostics[] = {
	{
		testSizes, PASS,
		"sizeof (int) == sizeof (void *)"
	},
	{
		size_intptr, PASS,
		"sizeof (intptr_t) == sizeof (void *)"
	},
	{
		allocateMemory, PASS,
		"#1 Allocation single byte of memory to play with."
	},
	{
		write0, PASS,
		"#1 Write valid memory"
	},
	{
		freeMemory, PASS,
		"#1 Free memory"
	},
	{
		allocateMemory, PASS,
		"#2 Allocation a new single byte of memory to play with."
	},
	{
		write0, PASS,
		"#2 Write valid memory"
	},
	{
		write_over, PASS,
		"#2 Over write invalid memory."
	},
	{
		freeMemory, SIGNAL,
		"#2 Free over written memory."
	},
	{
		allocateMemory, PASS,
		"#3 Allocation a new single byte of memory to play with."
	},
	{
		write_under, PASS,
		"#3 Under write invalid memory."
	},
	{
		freeMemory, SIGNAL,
		"#3 Free under written memory."
	},
	{
		allocateMemory, PASS,
		"#4 Allocation a new single byte of memory to play with."
	},
	{
		freeMemory, PASS,
		"#4 Free memory."
	},
	{
		freeMemory, SIGNAL,
		"#4 Double free memory."
	},
	{
		allocateMemory, PASS,
		"#5 Allocation a new single byte of memory to play with."
	},
	{
		corruptPointer, PASS,
		"#5 Corrupt the allocated memory pointer."
	},
	{
		freeMemory, SIGNAL,
		"#5 Free corrupted memory pointer."
	},
	{
		NULL, PASS, NULL
	}
};

static const char failedTest[] = "Unexpected result returned for:\n";

int
test_suite(void)
{
	static const struct diagnostic *diag;

	opt_abort = fatal;
	signal(SIGABRT, fault_handler);

	for (diag = diagnostics; diag->explanation != NULL; diag++) {
		if (find_fault(diag) == FAIL)
			exit(EXIT_FAILURE);
	}

	return EXIT_SUCCESS;
}

int
main(int argc, char **argv)
{
	char **base;
	int argi, ch;
	
	if (argc <= 1) {
		(void) fprintf(stderr, "usage: %s [-t] [string ...]\n", argv[0]);
		return EXIT_FAILURE;
	}
			
	while ((ch = getopt(argc, argv, "t")) != -1) {
		switch (ch) {
		case 't':
			return test_suite();
		}			
	}
	
	/* Otherwise simple test to exercise the memory hooks. */
	
	if ((base = calloc(argc+1, sizeof (*base))) == NULL) {
		(void) fprintf(stderr, "%s:%d memory\n", __FILE__, __LINE__);
		return EXIT_FAILURE;
	}
	
	for (argi = 0; argi < argc; argi++) {
		base[argi] = strdup(argv[argi]);
		if (base[argi] == NULL) {
			(void) fprintf(stderr, "%s:%d memory\n", __FILE__, __LINE__);
			return EXIT_FAILURE;
		}
	}
	base[argi] = NULL;
	
	for (argi = 0; argi < argc; argi++) {
		(void) printf("%d: %s\n", argi, base[argi]);
		free(base[argi]);
	}	
	free(base);
	
	/*** TODO realloc() test */
	
	return EXIT_SUCCESS;
}

#endif /* TEST */
