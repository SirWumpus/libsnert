/*
 * track.c
 *
 * Copyright 2015 by Anthony Howe.  All rights reserved.
 */

#ifndef DUMP_LINE
#define DUMP_LINE			16
#endif

#ifndef DUMP_SIZE
#define DUMP_SIZE			(2 * DUMP_LINE)
#endif

#ifndef DUMP_FILE
#define DUMP_FILE			"./track_leaks.log"
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
#include <com/snert/lib/util/Text.h>

#define HERE_FMT			"%s:%ld "
#define HERE_ARG			here, (long)lineno

#ifdef TEST
# define TRACK_STATS
# define LOGMSG(...)			(void) fprintf(stderr, __VA_ARGS__)
# define LOGTRACE()			LOGMSG("%s:%d\n", __func__, __LINE__)
# define LOGTRACK(t)			LOGMSG("%s: " TRACK_FMT "\r\n",__func__, TRACK_ARG(t))
# define LOGTRACKAT(t,f,l)		LOGMSG("%s: at " HERE_FMT TRACK_FMT "\r\n",__func__, f, l, TRACK_ARG(t))
# define LOGMALLOC(p,n,f,l)		LOGMSG("%s: ptr=%p size=%zu" HERE_FMT "\r\n", __func__, p, n, f, l)
# define LOGFREE(p,f,l)			LOGMSG("%s: ptr=%p " HERE_FMT "\r\n", __func__, p, f, l)
#else
# define LOGMSG(...)
# define LOGTRACE()
# define LOGTRACK(t)
# define LOGTRACKAT(t,f,l)
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
# define pthread_key_t			track_list *
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
	size_t size;
	unsigned long id;
	const char *here;
	long lineno;
	long crc;
} track_data;

#define TRACK_FMT		"ptr=%p id=%lu size=%zu from " HERE_FMT
#define TRACK_ARG(p)		&(p)[1], (p)->id, (p)->size, (p)->here, (p)->lineno
#define TRACK_CRC(p)		((long)((long)(p) ^ (p)->size ^ (p)->lineno ^ (long)(p)->here))

typedef struct track_list {
	struct track_list *prev;
	struct track_list *next;
	struct track_data *head;
	pthread_t thread;
} track_list;

static track_list *main_list;

static LOCK_T lock;
static unsigned long free_count;
static unsigned long malloc_count;

void  (*track_hook__exit)(int);
void  (*track_hook_free)(void *);
void *(*track_hook_malloc)(size_t);

static Marker lo_guard;
static pthread_t main_thread;
static pthread_key_t thread_key;

#ifdef SIGNAL_SAFE
/*
 * @note
 *	Signal safe.
 */
static size_t
T(dump_mem)(const unsigned char *mem, size_t count, char *buffer, size_t size)
{
	size_t i, n;
	
	if (0 < size)
		*buffer = '\0';
	if (size <= count * 4 + 4)
		return count * 4 + 4;

	*buffer++ = '\t';

	for (i = 0; i < count; i++) {
		n = ulong_format(mem[i], 16, -3, 2, 0, 0, buffer, size);
		buffer +=n;
		size -= n;
	}
	
	*buffer++ = ' ';
	
	for (i = 0; i < count; i++) {
		*buffer++ = isprint(mem[i]) ? mem[i] : '.';
	}
	
	*buffer++ = '\r';
	*buffer++ = '\n';
	*buffer   = '\0';
	
	return count * 4 + 4;
}

/*
 * @note
 *	Signal safe.
 */
static size_t
T(tostring)(track_data *track, size_t dump_len, char *buffer, size_t size)
{
	int i;
	size_t n;
	char *buf;
	
	buf = buffer;
	n = TextCopy(buf, size, "    ptr=0x");
	buf += n; if (buf-buffer < size) size -= n; else size = 0;	 

	n = ulong_format((unsigned long)(track+1), 16, 0, 0, 0, 0, buf, size);
	buf += n; if (buf-buffer < size) size -= n; else size = 0;	 
		
	n = TextCopy(buf, size, " id=");
	buf += n; if (buf-buffer < size) size -= n; else size = 0;	 
	
	n = ulong_format(track->id, 10, 0, 0, 0, 0, buf, size);
	buf += n; if (buf-buffer < size) size -= n; else size = 0;	 

	n = TextCopy(buf, size, " size=");
	buf += n; if (buf-buffer < size) size -= n; else size = 0;	 

	n = ulong_format(track->size, 10, 0, 0, 0, 0, buf, size);
	buf += n; if (buf-buffer < size) size -= n; else size = 0;	 

	n = TextCopy(buf, size, " from ");
	buf += n; if (buf-buffer < size) size -= n; else size = 0;	 

	n = TextCopy(buf, size, track->here);
	buf += n; if (buf-buffer < size) size -= n; else size = 0;	 

	if (0 < size) {
		*buf++ = ':';
		size--;
	}

	n = ulong_format(track->lineno, 10, 0, 0, 0, 0, buf, size);
	buf += n; if (buf-buffer < size) size -= n; else size = 0;	 
		
	n = TextCopy(buf, size, "\r\n");
	buf += n; if (buf-buffer < size) size -= n; else size = 0;	 

	/* Round up to multiple of 16. */
	dump_len = ((dump_len + 15) / 16) * 16;
	for (i = 0; i < dump_len; i += 16) {
		n = T(dump_mem)((unsigned char *)(track+1)+i, 16, buf, size);
		buf += n; if (buf-buffer < size) size -= n; else size = 0;
	}
	
	return buf - buffer;
}

void
T(report_all_fd)(int fd)
{
	track_list *list;
	
	list = main_list;
	do {
		T(report_fd)(fd, list);
		list = list->next;
	} while (list != main_list);

#ifdef TRACK_STATS
	if (malloc_count != free_count)
		(void) fprintf(stderr, "malloc=%lu free=%lu\r\n", malloc_count, free_count);	
#endif
}

void
sig_dump(int signum)
{
	int fd;
	
	if (0 <= (fd = open(DUMP_FILE, O_CREAT|O_WRONLY|O_APPEND, 0))) {
		T(report_all_fd)(fd);
		(void) close(fd);
	}
}
#else
static void
T(dump)(FILE *fp, track_data *track, size_t dump_len)
{
	size_t i, j;
	unsigned char *chunk;
	
	if (track->size < dump_len)
		dump_len = track->size;
	dump_len = ((dump_len + (DUMP_LINE-1)) / DUMP_LINE) * DUMP_LINE;
	chunk = (unsigned char *)(track+1);

	(void) fprintf(fp, "    " TRACK_FMT "\r\n", TRACK_ARG(track));

	for (i = 0; i < dump_len; i += DUMP_LINE) {
		(void) fputc('\t', fp);
		for (j = 0; j < DUMP_LINE; j++)
			(void) fprintf(fp, "%-3.2X", chunk[i+j]);			
		(void) fputc(' ', fp);
		for (j = 0; j < DUMP_LINE; j++)
			(void) fputc(isprint(chunk[j]) ? chunk[i+j] : '.', fp);
		(void) fputs("\r\n", fp);
	}
	
	(void) fflush(fp);
}
#endif

/*
 * Dump the list of leaked memory at thread exit.
 */
static void
T(report)(track_list *list)
{
	size_t size;
	unsigned count;
	track_data *track;

	if (list == NULL)
		return;

	if (main_thread == list->thread)
		(void) fprintf(stderr, "main:\r\n");
	else
		(void) fprintf(stderr, "thread 0x%lx:\r\n", (unsigned long) list->thread);
		
	size = 0;
	for (count = 0, track = list->head; track != NULL; track = track-> next, count++) {
		/* The thread data cleanup will be called before
		 * the thread releases its pthread_t data, which
		 * can result in a spurious leak being reported.
		 */
#ifdef SIGNAL_SAFE		 
		char buffer[512];
		(void) T(tostring)(track, DUMP_SIZE, buffer, sizeof (buffer));
		fputs(buffer, stderr);
#else
		(void) T(dump)(stderr, track, DUMP_SIZE);
#endif
		(void) fputs("\r\n", stderr);
		size += track->size;
	}

	if (0 < count)
		(void) fprintf(stderr, "    leaked blocks=%u size=%zu\r\n", count, size);
}

void
T(report_all_unsafe)(void)
{
	track_list *list;
	
	list = main_list;
	do {
		T(report)(list);
		list = list->next;
	} while (list != main_list);

#ifdef TRACK_STATS
	if (malloc_count != free_count)
		(void) fprintf(stderr, "malloc=%lu free=%lu\r\n", malloc_count, free_count);	
#endif
}

void
T(report_all)(void)
{
	LOCK_LOCK(&lock);
	T(report_all_unsafe)();
	LOCK_UNLOCK(&lock);
}

void
T(clean_key)(void *data)
{
	track_list *list;

	LOGTRACE();
	if (data != NULL) {
		list = data;

		LOCK_LOCK(&lock);
		list->prev->next = list->next;
		list->next->prev = list->prev;
		LOCK_UNLOCK(&lock);

		T(report)(list);	
		(free)(list);

		(void) pthread_setspecific(thread_key, NULL);
	}	
}

static void
T(init_common)(void)
{
	(void) LOCK_INIT(&lock);
	(void) LOCK_LOCK(&lock);
	(void) LOCK_UNLOCK(&lock);

	(void) pthread_key_create(&thread_key, T(clean_key));
	(void) pthread_setspecific(thread_key, NULL);
	main_thread = pthread_self();

	(void) memset(&lo_guard, '[', sizeof (lo_guard));
}

#if defined(__WIN32__)
# error "Windows support not implemented."

#elif defined(HAVE_DLFCN_H) && defined(LIBC_PATH)
# include <dlfcn.h>

static void
T(init)(void)
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
	T(init_common)();
	
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
	track_list *list;

	LOGTRACE();	
	if (track_hook__exit == NULL)
		T(init)();
	
	/* We need to hook _exit() so that we can report leaks after
	 * all the atexit() functions, which might release dynamic 
	 * memory.  We cannot simply use atexit() for reporting since
	 * there is no way to garantee we are executed last.
	 *
	 * NOTE sys/track.c and sys/malloc.c both hook _exit(), so
	 * cannot be used together (yet).
	 */
	list = (track_list *)pthread_getspecific(thread_key);
	T(report)(list);	
	(void) pthread_key_delete(thread_key);
	(void) LOCK_FREE(&lock);

	(*track_hook__exit)(ex_code);
}
#endif /* TRACK */

void
T(free)(void *chunk, const char *here, long lineno)
{
	track_list *list;
	track_data *track;

	if (chunk == NULL) {
		LOGFREE(chunk, here, lineno);
		return;
	}

	track = &((track_data *) chunk)[-1];
	LOGTRACKAT(track, here, lineno);

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
	list = pthread_getspecific(thread_key);
	if (track->prev == NULL)
		list->head = track->next;
	else
		track->prev->next = track->next;		
	if (track->next != NULL)
		track->next->prev = track->prev;
	(void) pthread_setspecific(thread_key, list);	

	(free)(track);

	(void) LOCK_LOCK(&lock);
	free_count++;
	(void) LOCK_UNLOCK(&lock);			
}

void *
T(malloc)(size_t size, const char *here, long lineno)
{
	track_list *list;
	track_data *track;
	
	if (track_hook__exit == NULL)
		T(init)();
	
	if ((track = (malloc)(sizeof (*track) + size + (size == 0))) == NULL) {
		LOGMALLOC(NULL, size, here, lineno);
		return NULL;
	}

	(void) LOCK_LOCK(&lock);
	malloc_count++;
	track->id = malloc_count;
	(void) LOCK_UNLOCK(&lock);

	track->size = size;
	track->here = here;
	track->lineno = lineno;
	track->lo_guard = lo_guard;
	track->crc = TRACK_CRC(track);

	/* Maintain list of allocated memory per thread, including main() */
	list = (track_list *)pthread_getspecific(thread_key);

	if (list == NULL) {
		if ((list = (malloc)(sizeof (*list))) == NULL) {
			(free)(track);
			return NULL;
		}
		
		list->thread = pthread_self();
		list->head = NULL;
	
		/* Each thread maintains an allocation list and each
		 * allocation list is a member of a circular double
		 * link list of lists.  This allows us to dump all 
		 * the current allocations for all threads any time.
		 */
		LOCK_LOCK(&lock);
		if (main_list == NULL) {
			list->prev = list;
			list->next = list;
			main_list = list;		
		} else {
			list->prev = main_list->prev;
			list->next = main_list;
			main_list->prev->next = list;
			main_list->prev = list;		
		}		
		LOCK_UNLOCK(&lock);		
	}
	
	/* Push allocation to head of list. */
	track->prev = NULL;
	track->next = list->head;
	if (list->head != NULL)
		list->head->prev = track;
	list->head = track;
	
	(void) pthread_setspecific(thread_key, list);		
	
	LOGTRACKAT(track, here, lineno);
	
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
	(void) TextCopy(malloc(101), 101, "leak 1 abcdefghijklmnopqrstuvwxyz");
	
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
	(void) TextCopy(malloc(102), 102, "leak 2 abcdefghijklmnopqrstuvwxyz");
	
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
