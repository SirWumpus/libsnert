/*
 * track.c
 *
 * Copyright 2015 by Anthony Howe.  All rights reserved.
 */

#ifndef DUMP_SIZE
#define DUMP_SIZE			32
#endif

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <com/snert/lib/sys/sysexits.h>

#define HERE_FMT			"%s:%ld "
#define HERE_ARG			here, (long)lineno

#ifdef TEST
# define LOGMSG(...)			(void) fprintf(stderr, __VA_ARGS__)
# define LOGTRACE()			LOGMSG("%s:%d\n", __func__, __LINE__)
# define LOGMALLOC(p,n,f,l)		LOGMSG("%s: ptr=%p size=%zu " HERE_FMT "\r\n", __func__, p, n, f, l)
# define LOGFREE(p,f,l)			LOGMSG("%s: ptr=%p " HERE_FMT "\r\n", __func__, p, f, l)
# define TRACK_STATS
#else
# define LOGMSG(...)
# define LOGTRACE()
# define LOGMALLOC(p,n,f,l)
# define LOGFREE(p,f,l)
#endif

#if defined(_REENTRANT)
# include <com/snert/lib/sys/pthread.h>

// Not sure is I should have two compiled versions of libsnert;
// normally or pthread ready.
//# define T(name)			ptrack_ ## name

# define LOCK_INITIALISER		PTHREAD_MUTEX_INITIALIZER
# define LOCK_T				pthread_mutex_t
# define LOCK_INIT(L)			pthread_mutex_init(L, NULL)
# define LOCK_FREE(L)			pthread_mutex_destroy(L)
# define LOCK_LOCK(L)			pthread_mutex_lock(L)
# define LOCK_TRYLOCK(L)		pthread_mutex_trylock(L)
# define LOCK_UNLOCK(L)			pthread_mutex_unlock(L)

# if !defined(HAVE_PTHREAD_YIELD) && !defined(pthread_yield)
#  define pthread_yield()		(0)
# endif
#else
/* No pthread support. */

/* Mimick a mutex (mostly to silence compiler errors). */
# define LOCK_INITIALISER		0
# define LOCK_T				int
# define LOCK_INIT(L)			(*(L) = 1)
# define LOCK_FREE(L)			(*(L) = 0, 0)
# define LOCK_LOCK(L)			((*(L))--, 0)
# define LOCK_TRYLOCK(L)		((*(L))--, 0)
# define LOCK_UNLOCK(L)			((*(L))++, 0)

# define PTHREAD_ONCE_INIT		0
# define pthread_t			unsigned
# define pthread_self()			(0)
# define pthread_once_t			unsigned
# define pthread_once(o, f)		(0)

/* Mimick per thread data object.  thread_key will be the list head. */
# define pthread_key_t			track_data *
# define pthread_key_create(k, f)	(*(k) = NULL)
# define pthread_key_delete(k)		(0)
# define pthread_getspecific(k)		(k)
# define pthread_setspecific(k, v)	(k = (v), 0)

# define pthread_create(t,a,f,d)	(*(t) = 0, 0)
# define pthread_yield()		(0)
# define pthread_join(t,r)		(0)
# define pthread_cleanup_push(f, d)
# define pthread_cleanup_pop(b)
#endif

#ifndef T
# define T(name)			track_ ## name
#endif

typedef void *Marker;

typedef struct track_data {
	Marker lo_guard;	
	struct track_data *prev;
	struct track_data *next;
	pthread_t thread;
	size_t size;
	const char *here;
	long lineno;
	long crc;
} track_data;

#define TRACK_FMT		"chunk=%p size=%zu " HERE_FMT
#define TRACK_ARG(p)		&(p)[1], (p)->size, (p)->here, (p)->lineno
#define TRACK_CRC(p)		((long)((long)(p) ^ (p)->size ^ (p)->lineno ^ (long)(p)->here))

#ifdef TRACK_STATS
static LOCK_T lock;
static unsigned long free_count;
static unsigned long malloc_count;
#endif

void  (*track_hook__exit)(int);
void  (*track_hook_free)(void *);
void *(*track_hook_malloc)(size_t);

static Marker lo_guard;
static pthread_t main_thread;
static pthread_key_t thread_key;

void
T(dump)(void *chunk, size_t length)
{
	size_t i;
	track_data *track;
	
	track = &((track_data *) chunk)[-1];
	if (track->size < length)
		length = track->size;

	(void) fprintf(stderr, "\t" TRACK_FMT " dump=%zu:\r\n\t", TRACK_ARG(track), length);

	for (i = 0; i < length; i++, chunk++) {
		if (isprint(*(char *)chunk))
			(void) fprintf(stderr, "%c", *(char *)chunk);
		else
			(void) fprintf(stderr, "\\x%02X", *(char *)chunk);
	}

	(void) fprintf(stderr, "\r\n");
}

/*
 * Dump the list of leaked memory at thread exit.
 */
void
T(report)(void *data)
{
	size_t size;
	unsigned count;
	track_data *track;

	LOGTRACE();
	if (data == NULL)
		return;
		
	/* Clear thread data to avoid thread key cleanup loop. */
	(void) pthread_setspecific(thread_key, NULL);

	track = data;	
	if (main_thread == track->thread)
		(void) fprintf(stderr, "main:\r\n");
	else
		(void) fprintf(stderr, "thread 0x%lx:\r\n", (unsigned long) track->thread);

	size = 0;
	for (count = 0; track != NULL; track = track->next, count++) {
		/* The thread data cleanup will be called before
		 * the thread releases its pthread_t data, which
		 * can result in a spurious leak being reported.
		 */
		T(dump)(track+1, DUMP_SIZE);
		size += track->size;
	}

	if (0 < count)
		(void) fprintf(stderr, "\tleaked blocks=%u size=%zu\r\n", count, size);
#ifdef TRACK_STATS
	if (malloc_count != free_count)
		(void) fprintf(stderr, "malloc=%lu free=%lu\r\n", malloc_count, free_count);	
#endif
}

static void
track_init_common(void)
{
#ifdef TRACK_STATS
	(void) LOCK_INIT(&lock);
	(void) LOCK_LOCK(&lock);
	(void) LOCK_UNLOCK(&lock);
#endif
	(void) pthread_key_create(&thread_key, T(report));
	(void) pthread_setspecific(thread_key, NULL);
	main_thread = pthread_self();

	(void) memset(&lo_guard, '[', sizeof (lo_guard));
}

#if defined(__WIN32__)
# error "Windows support not implemented."

#elif defined(HAVE_DLFCN_H) && defined(LIBC_PATH)
# include <dlfcn.h>

static void
track_init(void)
{
	void *libc;
	const char *err;
	void (*libc__exit)(int);
	void (*libc_free)(void *);
	void *(*libc_malloc)(size_t);

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
	track_init_common();
	
	/* We've completed initialisation. */
	track_hook__exit = libc__exit;
	track_hook_malloc = libc_malloc;
	track_hook_free = libc_free;
}
#endif

#ifdef TRACK
/* Only hook _exit() when we build with -DTRACK (see --enable-track). */

void
(_exit)(int ex_code)
{
	LOGTRACE();
	
	if (track_hook__exit == NULL)
		track_init();
	
	/* We need to hook _exit() so that we can report leaks after
	 * all the atexit() functions, which might release dynamic 
	 * memory.  We cannot simply use atexit() for reporting since
	 * there is no way to garantee we are executed last.
	 *
	 * NOTE sys/track.c and sys/malloc.c both hook _exit(), so
	 * cannot be used together (yet).
	 */
	T(report)(pthread_getspecific(thread_key));
	(void) pthread_key_delete(thread_key);
#ifdef TRACK_STATS
//	(void) LOCK_TRYLOCK(&lock);
//	(void) LOCK_UNLOCK(&lock);	
	(void) LOCK_FREE(&lock);
#endif
	(*track_hook__exit)(ex_code);
}
#endif /* TRACK */

void
T(free)(void *chunk, const char *here, long lineno)
{
	track_data *track, *head;

	LOGFREE(chunk, here, lineno);
	if (chunk == NULL)
		return;

	track = &((track_data *) chunk)[-1];

	/* Check the pointer and core data are valid. */	
	if (track->crc != TRACK_CRC(track)) {
		/* The rest of the data is suspect. */
		(void) fprintf(stderr, HERE_FMT "buffer under run or bad pointer!\n", HERE_ARG);
		abort();
	}

	/* Did something run into us from below? */
	if (track->lo_guard != lo_guard) {
		(void) fprintf(
			stderr, HERE_FMT "buffer guard corrupted! size=%zu %s:%ld\n", HERE_ARG,
			track->size, track->here, track->lineno
		);
		abort();
	}
	
	/* Maintain list of allocated memory per thread, including main() */
	head = pthread_getspecific(thread_key);
	if (track->prev == NULL)
		head = track->next;
	else
		track->prev->next = track->next;		
	if (track->next != NULL)
		track->next->prev = track->prev;
	(void) pthread_setspecific(thread_key, head);	

#ifdef TEST
	(void) fprintf(stderr, "freed: " HERE_FMT "size=%zu\r\n", track->here, track->lineno, track->size);
#endif
	(free)(track);

#ifdef TRACK_STATS
	(void) LOCK_LOCK(&lock);
	free_count++;
	(void) LOCK_UNLOCK(&lock);			
#endif
}

void *
T(malloc)(size_t size, const char *here, long lineno)
{
	track_data *track, *head;
	
	if (track_hook__exit == NULL)
		track_init();
	
	if ((track = (malloc)(sizeof (*track) + size + (size == 0))) == NULL) {
		LOGMALLOC(NULL, size, here, lineno);
		return NULL;
	}

#ifdef TRACK_STATS
	(void) LOCK_LOCK(&lock);
	malloc_count++;
	(void) LOCK_UNLOCK(&lock);
#endif
	track->size = size;
	track->here = here;
	track->lineno = lineno;
	track->thread = pthread_self();
	track->crc = TRACK_CRC(track);
	track->lo_guard = lo_guard;

	/* Maintain list of allocated memory per thread, including main() */
	head = pthread_getspecific(thread_key);
	track->prev = NULL;
	track->next = head;
	if (head != NULL)
		head->prev = track;
	(void) pthread_setspecific(thread_key, track);		
	
	LOGMALLOC(&track[1], size, here, lineno);
	
	return &track[1];
}

void *
T(calloc)(size_t m, size_t n, const char *here, long lineno)
{
	void *chunk;
	size_t size;
	
	size = m * n;
	if ((chunk = T(malloc)(size, here, lineno)) != NULL)
		(void) memset(chunk, 0, size);
	
	return chunk;
}

void *
T(realloc)(void *orig, size_t size, const char *here, long lineno)
{
	void *chunk;
	track_data *track;
	
	if ((chunk = T(malloc)(size, here, lineno)) != NULL && orig != NULL) {
		track = &((track_data *) chunk)[-1];
		(void) memcpy(chunk, orig, track->size < size ? track->size : size);
		T(free)(orig, here, lineno);
	}
		
	return chunk;
}

#define is_power_two(x)		(((x) != 0) && !((x) & ((x) - 1)))

void *
T(aligned_alloc)(size_t alignment, size_t size, const char *here, long lineno)
{
	if (is_power_two(alignment) && sizeof (void *) <= alignment && (size / alignment) * alignment == size)
		return T(malloc)(size, here, lineno);
	return NULL;
}

char *
T(strdup)(const char *orig, const char *here, long lineno)
{
	char *copy;
	size_t size;

	size = strlen(orig) + 1;
	if ((copy = T(malloc)(size, here, lineno)) != NULL)
		(void) memcpy(copy, orig, size);

	return copy;
}

#ifdef TRACK
/*
 * Use our version of strdup() so we ensure that we use matching
 * malloc/free family of functions.  Otherwise you get can get
 * false positives about buffer under runs.
 */
char *
(strdup)(const char *orig)
{
	return T(strdup)(orig, __func__, __LINE__);
}
#endif

/***********************************************************************
 *** Test Suite
 ***********************************************************************/

#ifdef TEST
#include <errno.h>

struct main_args {
	int argc;
	char **argv;
};

void *
test_thread(void *data)
{
	int argi;
	char **base;
	struct main_args *arg = data;

	if ((base = calloc(arg->argc+1, sizeof (*base))) == NULL) {
		(void) fprintf(stderr, "%s:%d memory\n", __FILE__, __LINE__);
		return (void *) EXIT_FAILURE;
	}

	/* Force a leak. */
	(void) fprintf(stderr, "create test leak 1\n");
	(void) malloc(101);
	
	for (argi = 0; argi < arg->argc; argi++) {
		base[argi] = strdup(arg->argv[argi]);
//		(void) pthread_yield();
		if (base[argi] == NULL) {
			(void) fprintf(stderr, "%s:%d memory\n", __FILE__, __LINE__);
			return (void *) EXIT_FAILURE;
		}
	}
	base[argi] = NULL;
	
	for (argi = 0; argi < arg->argc; argi++) {
		(void) printf("%d: %s\n", argi, base[argi]);
		free(base[argi]);
//		(void) pthread_yield();
	}	
	free(base);
	
	/* Force a leak. */
	(void) fprintf(stderr, "create test leak 2\n");
	(void) malloc(102);
	
	return NULL;
}

int
main(int argc, char **argv)
{
	int err_no;
	void *test_ret;
	pthread_t testy;
	struct main_args arg;
	
	if (argc <= 1) {
		(void) fprintf(stderr, "usage: %s [string ...]\n", argv[0]);
		return EXIT_FAILURE;
	}
	
	arg.argc = argc;
	arg.argv = argv;

	if (pthread_create(&testy, NULL, test_thread, &arg)) {
		(void) fprintf(stderr, "%s:%d %s (%d)\r\n", __FILE__, __LINE__, strerror(errno), errno);
		return EXIT_FAILURE;
	}
			
	(void) test_thread(&arg);

	test_ret = NULL;
	if ((err_no = pthread_join(testy, &test_ret))) {
		(void) fprintf(stderr, "%s:%d %s (%d)\r\n", __FILE__, __LINE__, strerror(err_no), err_no);
		return EXIT_FAILURE;
	}	
	if ((long) test_ret) {
		(void) fprintf(stderr, "%s:%d test_thread() failed\r\n", __FILE__, __LINE__);
		return EXIT_FAILURE;
	}		
	
	return EXIT_SUCCESS;
}
#endif /* TEST */
