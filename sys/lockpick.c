/*
 * lockpick.c
 *
 * Copyright 2010 by Anthony Howe.  All rights reserved.
 *
 * Inspired by http://linuxgazette.net/150/melinte.html
 */

/*
 * Must be a power of two.
 */
#ifndef HASH_TABLE_SIZE
#define HASH_TABLE_SIZE		(4 * 1024)
#endif

#ifndef MAX_LINEAR_PROBE
#define MAX_LINEAR_PROBE	64
#endif

#ifndef MAX_STACKTRACE_DEPTH
#define MAX_STACKTRACE_DEPTH	128
#endif

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#if defined(DEBUG_MUTEX)

#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

# if defined(HAVE_SYSLOG_H)
#  include <syslog.h>
# endif

#include <com/snert/lib/io/Log.h>
#include <com/snert/lib/sys/pthread.h>
#include <com/snert/lib/sys/sysexits.h>
#include <com/snert/lib/sys/lockpick.h>

#undef pthread_mutex_init
#undef pthread_mutex_destroy
#undef pthread_mutex_lock
#undef pthread_mutex_unlock
#undef pthread_mutex_trylock
#undef pthread_cond_wait
#undef pthread_cond_timedwait

#ifndef LIBPTHREAD_PATH
#define LIBPTHREAD_PATH		"libpthread.so"
#endif

/***********************************************************************
 *** Constants & Globals
 ***********************************************************************/

typedef struct {
	pthread_mutex_t *mutex;
	pthread_t thread;
	const char *file;
	unsigned lineno;
	unsigned index;
	unsigned count;
} lp_mutex_data;

typedef struct  {
	pthread_t thread;
	unsigned max_index;
} lp_thread_data;

static lp_mutex_data lp_mutexes[HASH_TABLE_SIZE];
static lp_thread_data lp_threads[HASH_TABLE_SIZE];
static pthread_mutex_t lp_mutex = PTHREAD_MUTEX_INITIALIZER;

static int (*lp_mutex_init_fn)(pthread_mutex_t *, const pthread_mutexattr_t *);
static int (*lp_mutex_destroy_fn)(pthread_mutex_t *);
static int (*lp_mutex_lock_fn)(pthread_mutex_t *);
static int (*lp_mutex_unlock_fn)(pthread_mutex_t *);
static int (*lp_mutex_trylock_fn)(pthread_mutex_t *);
static int (*lp_cond_wait_fn)(pthread_cond_t *, pthread_mutex_t *);
static int (*lp_cond_timedwait_fn)(pthread_cond_t *, pthread_mutex_t *,  const struct timespec *);

#undef PTHREAD_MUTEX_LOCK
#undef PTHREAD_MUTEX_UNLOCK

#if defined(HAD_PTHREAD_CLEANUP_PUSH)
# define PTHREAD_MUTEX_LOCK(m)		if (!(*lp_mutex_lock_fn)(m)) { \
						pthread_cleanup_push((void (*)(void*)) lp_mutex_unlock_fn, (m));

# define PTHREAD_MUTEX_UNLOCK(m)		; \
						pthread_cleanup_pop(1); \
					}

#else
# define PTHREAD_MUTEX_LOCK(m)		if (!(*lp_mutex_lock_fn)(m)) { \

# define PTHREAD_MUTEX_UNLOCK(m)		; \
						(void) (*lp_mutex_unlock_fn)(m); \
					}
#endif

/***********************************************************************
 *** Internal
 ***********************************************************************/

#if defined(__WIN32__)
# include <windows.h>


#elif defined(HAVE_DLFCN_H) && defined(LIBC_PATH)
# include <dlfcn.h>

static void
lp_init(void)
{
	void *handle;
	const char *err;

	handle = dlopen(LIBPTHREAD_PATH, RTLD_NOW);
	if ((err = dlerror()) != NULL) {
		fprintf(stderr, "%s load error\r\n", LIBPTHREAD_PATH);
		exit(EX_OSERR);
	}

  	lp_mutex_init_fn = (int (*)(pthread_mutex_t *, const pthread_mutexattr_t *)) dlsym(handle, "pthread_mutex_init");
	if ((err = dlerror()) != NULL) {
		fprintf(stderr, "pthread_mutex_init not found\n");
		exit(EX_OSERR);
	}

  	lp_mutex_destroy_fn = (int (*)(pthread_mutex_t *)) dlsym(handle, "pthread_mutex_destroy");
	if ((err = dlerror()) != NULL) {
		fprintf(stderr, "pthread_mutex_destroy not found\n");
		exit(EX_OSERR);
	}

  	lp_mutex_lock_fn = (int (*)(pthread_mutex_t *)) dlsym(handle, "pthread_mutex_lock");
	if ((err = dlerror()) != NULL) {
		fprintf(stderr, "pthread_mutex_lock not found\n");
		exit(EX_OSERR);
	}

  	lp_mutex_unlock_fn = (int (*)(pthread_mutex_t *)) dlsym(handle, "pthread_mutex_unlock");
	if ((err = dlerror()) != NULL) {
		fprintf(stderr, "pthread_mutex_unlock not found\n");
		exit(EX_OSERR);
	}

  	lp_mutex_trylock_fn = (int (*)(pthread_mutex_t *)) dlsym(handle, "pthread_mutex_trylock");
	if ((err = dlerror()) != NULL) {
		fprintf(stderr, "pthread_mutex_trylock not found\n");
		exit(EX_OSERR);
	}

  	lp_cond_wait_fn = (int (*)(pthread_cond_t *, pthread_mutex_t *)) dlsym(handle, "pthread_cond_wait");
	if ((err = dlerror()) != NULL) {
		fprintf(stderr, "pthread_cond_wait not found\n");
		exit(EX_OSERR);
	}

  	lp_cond_timedwait_fn = (int (*)(pthread_cond_t *, pthread_mutex_t *, const struct timespec *)) dlsym(handle, "pthread_cond_timedwait");
	if ((err = dlerror()) != NULL) {
		fprintf(stderr, "pthread_cond_timedwait not found\n");
		exit(EX_OSERR);
	}

	(void) (*lp_mutex_init_fn)(&lp_mutex, NULL);
}

#elif !defined(LIBPTHREAD_PATH)
# error "LIBPTHREAD_PATH is undefined. "
#endif

static lp_mutex_data *
lp_find_mutex(pthread_mutex_t *m)
{
	int i;
	unsigned long hash;
	lp_mutex_data *entry, *available;

	hash = (unsigned long) m & (HASH_TABLE_SIZE-1);
	available = &lp_mutexes[hash];

	for (i = 0; i < MAX_LINEAR_PROBE; i++) {
		entry = &lp_mutexes[(hash + i) & (HASH_TABLE_SIZE-1)];
		if (entry->mutex == NULL)
			available = entry;
		else if (m == entry->mutex)
			return entry;
	}

	/* If we didn't find the entry within the linear probe
	 * distance, then overwrite the oldest hash entry. Note
	 * that we take the risk of two or more IPs repeatedly
	 * cancelling out each other's entry. Shit happens.
	 */
	return available;
}

static lp_thread_data *
lp_find_thread(pthread_t t)
{
	int i;
	unsigned long hash;
	lp_thread_data *entry, *available;

	hash = (unsigned long) t & (HASH_TABLE_SIZE-1);
	available = &lp_threads[hash];

	for (i = 0; i < MAX_LINEAR_PROBE; i++) {
		entry = &lp_threads[(hash + i) & (HASH_TABLE_SIZE-1)];
		if (entry->max_index == 0)
			available = entry;
		else if (pthread_equal(t, entry->thread))
			return entry;
	}

	/* If we didn't find the entry within the linear probe
	 * distance, then overwrite the oldest hash entry. Note
	 * that we take the risk of two or more IPs repeatedly
	 * cancelling out each other's entry. Shit happens.
	 */
	available->thread = t;
	return available;
}

static void
lp_reset(pthread_mutex_t *mutex)
{
	pthread_t me;
	lp_mutex_data *md;
	lp_thread_data *td;

	PTHREAD_MUTEX_LOCK(&lp_mutex);

	me = pthread_self();
	td = lp_find_thread(me);
	md = lp_find_mutex(mutex);

	memset(md, 0, sizeof (*md));

	if (td->max_index == 0)
		fprintf(stderr, "%s:%d internal error\n", __FILE__, __LINE__);
	else
		td->max_index--;

	PTHREAD_MUTEX_UNLOCK(&lp_mutex);
}

static void
lp_assign(pthread_mutex_t *mutex, const char *file, unsigned line)
{
	pthread_t me;
	lp_mutex_data *md;
	lp_thread_data *td;

	PTHREAD_MUTEX_LOCK(&lp_mutex);

	me = pthread_self();
	td = lp_find_thread(me);
	md = lp_find_mutex(mutex);

	td->thread = me;
	md->thread = me;
	md->file = file;
	md->lineno = line;
	md->mutex = mutex;
	md->index = ++td->max_index;

	PTHREAD_MUTEX_UNLOCK(&lp_mutex);
}

static int
lp_check_lock(pthread_mutex_t *m, int blocking, const char *file, unsigned line)
{
	int rc;
	lp_mutex_data *md;
	lp_thread_data *td;

	if (m == NULL)
		return EINVAL;

#ifdef LP_MUTEX_INIT
	if (lp_mutex_lock_fn == NULL)
		lp_init();
#endif
	rc = 0;
	pthread_testcancel();
	PTHREAD_MUTEX_LOCK(&lp_mutex);

	md = lp_find_mutex(m);
	td = lp_find_thread(pthread_self());

	if (md->mutex != NULL) {
		/* Already locked. */
		if (pthread_equal(md->thread, td->thread)) {
			fprintf(stderr, "%s:%d mutex 0x%lx locked by same thread\n", file, line, (unsigned long) m);
			rc = EDEADLK;
		} else if (!blocking) {
			fprintf(stderr, "%s:%d mutex 0x%lx locked by other thread\n", file, line, (unsigned long) m);
			rc = EBUSY;
		} else {
			rc = 0;
		}
	}

	PTHREAD_MUTEX_UNLOCK(&lp_mutex);

	return rc;
}

static int
lp_check_unlock(pthread_mutex_t *m, const char *file, unsigned line)
{
	int rc;
	lp_mutex_data *md;
	lp_thread_data *td;

	if (m == NULL)
		return EINVAL;

#ifdef LP_MUTEX_INIT
	if (lp_mutex_unlock_fn == NULL)
		lp_init();
#endif
	rc = 0;
	pthread_testcancel();
	PTHREAD_MUTEX_LOCK(&lp_mutex);

	md = lp_find_mutex(m);
	td = lp_find_thread(pthread_self());

	if (md->mutex == NULL) {
		fprintf(stderr, "%s:%d mutex 0x%lx not locked\n", file, line, (unsigned long) m);
		rc = EPERM;
	} else if (!pthread_equal(md->thread, td->thread)) {
		fprintf(stderr, "%s:%d mutex 0x%lx locked by other thread\n", file, line, (unsigned long) m);
		rc = EPERM;
	} else {
		if (md->index != td->max_index) {
			/* If the mutexes a thread holds are unlocked in
			 * reverse order to the locking order, then the
			 * thread's max_index will always equal the index
			 * of the mutex being released.
			 */
			fprintf(stderr, "%s:%d mutex 0x%lx unlocked out of order (id=%u expected=%u), possible deadlock\n", file, line, (unsigned long) m, md->index, td->max_index);
		}

		rc = 0;
	}

	PTHREAD_MUTEX_UNLOCK(&lp_mutex);

	return rc;
}

/***********************************************************************
 *** debug wrapped functions
 ***********************************************************************/

int
lp_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *a)
{
	if (lp_mutex_init_fn == NULL)
		lp_init();

	return (*lp_mutex_init_fn)(m, a);
}

int
lp_mutex_destroy(pthread_mutex_t *m)
{
	lp_reset(m);
	return (*lp_mutex_destroy_fn)(m);
}

int
lp_mutex_lock(pthread_mutex_t *m, const char *file, unsigned line)
{
	int rc;

	if ((rc = lp_check_lock(m, 1, file, line)) == 0) {
		if ((rc = (*lp_mutex_lock_fn)(m)) == 0) {
			/* Lock acquired. */
			lp_assign(m, file, line);
		}
	}

	return rc;
}

int
lp_mutex_unlock(pthread_mutex_t *m, const char *file, unsigned line)
{
	int rc;

	if ((rc = lp_check_unlock(m, file, line)) == 0) {
		lp_reset(m);
		rc = (*lp_mutex_unlock_fn)(m);
	}

	return rc;
}

int
lp_mutex_trylock(pthread_mutex_t *m, const char *file, unsigned line)
{
	int rc;

	if ((rc = lp_check_lock(m, 0, file, line)) == 0) {
		if ((rc = (*lp_mutex_trylock_fn)(m)) == 0) {
			/* Lock acquired. */
			lp_assign(m, file, line);
		}
	}

	return rc;
}

int
lp_cond_wait(pthread_cond_t *cv, pthread_mutex_t *m, const char *file, unsigned line)
{
	int rc;

	if ((rc = lp_check_unlock(m, file, line)) == 0) {
		/* Release the current thread's control of the mutex.
		 * Another thread may subsequently lock it directly
		 * or indirectly via conditional variable.
		 */
		lp_reset(m);

		rc = (*lp_cond_wait_fn)(cv, m);

		/* The conditional variable has been signalled.
		 * Assign the mutex to the active thread.
		 */
		lp_assign(m, file, line);
	}

	return rc;
}

int
lp_cond_timedwait(pthread_cond_t *cv, pthread_mutex_t *m, const struct timespec *abstime, const char *file, unsigned line)
{
	int rc;

	if ((rc = lp_check_unlock(m, file, line)) == 0) {
		/* Release the current thread's control of the mutex.
		 * Another thread may subsequently lock it directly
		 * or indirectly via conditional variable.
		 */
		lp_reset(m);
		rc = (*lp_cond_timedwait_fn)(cv, m, abstime);

		/* The conditional variable has been signalled.
		 * Assign the mutex to the active thread.
		 */
		lp_assign(m, file, line);
	}

	return rc;
}

/***********************************************************************
 *** pthread mutex hooked functions for libraries
 ***********************************************************************/

int
pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *a)
{
	return lp_mutex_init(m, a);
}

#ifdef HAVE_BACKTRACE
void
lp_stacktrace(int err_no)
{
	int i, length;
	char **strings;
	void *trace[MAX_STACKTRACE_DEPTH];

	length = backtrace(trace, MAX_STACKTRACE_DEPTH);
	strings = backtrace_symbols(trace, length);
	fprintf(stderr, "-- %s (%d)\n", strerror(err_no), err_no);
	for (i = 0; i < length; i++)
		fprintf(stderr, "frame #%d %s\n", i, strings[i]);
	free(strings);
}
#endif

# if DEBUG_MUTEX == 2
/* The reason for disabling these hooked functions is that some
 * libraries, like SQLite use recursive mutexes, which currently
 * report lots of errors/warnings. Support for recursive mutexes
 * has to be added before we can always hook these functions.
 */

int
pthread_mutex_destroy(pthread_mutex_t *m)
{
	return lp_mutex_destroy(m);
}

int
pthread_mutex_lock(pthread_mutex_t *m)
{
	int rc = lp_mutex_lock(m, __FILE__, __LINE__);

#ifdef HAVE_BACKTRACE
	if (rc != 0)
		lp_stacktrace(rc);
#endif
	return rc;
}

int
pthread_mutex_unlock(pthread_mutex_t *m)
{
	int rc = lp_mutex_unlock(m, __FILE__, __LINE__);

#ifdef HAVE_BACKTRACE
	if (rc != 0)
		lp_stacktrace(rc);
#endif
	return rc;
}

int
pthread_mutex_trylock(pthread_mutex_t *m)
{
	int rc = lp_mutex_trylock(m, __FILE__, __LINE__);

#ifdef HAVE_BACKTRACE
	if (rc != 0)
		lp_stacktrace(rc);
#endif
	return rc;
}

int
pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m)
{
	int rc = lp_cond_wait(c, m, __FILE__, __LINE__);

#ifdef HAVE_BACKTRACE
	if (rc != 0)
		lp_stacktrace(rc);
#endif
	return rc;
}

int
pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m, const struct timespec *abstime)
{
	int rc = lp_cond_timedwait(c, m, abstime, __FILE__, __LINE__);

#ifdef HAVE_BACKTRACE
	if (rc != 0)
		lp_stacktrace(rc);
#endif
	return rc;
}
# endif /* DEBUG_MUTEX == 2 */

#endif /* DEBUG_MUTEX */
