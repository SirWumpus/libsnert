/*
 * pthread.c
 *
 * Copyright 2004, 2009 by Anthony Howe.  All rights reserved.
 */

#include <com/snert/lib/version.h>

#include <errno.h>
#include <com/snert/lib/sys/pthread.h>
#include <com/snert/lib/type/queue.h>
#include <com/snert/lib/util/timer.h>

#if defined(__WIN32__)

/***********************************************************************
 *** pthread create functions
 ***********************************************************************/

int
pthread_attr_init(pthread_attr_t *attr)
{
	attr->stack_size = PTHREAD_STACK_MIN;
	return 0;
}

int
pthread_attr_destroy(pthread_attr_t *attr)
{
	return 0;
}

int
pthread_attr_getstacksize(const pthread_attr_t *attr, size_t *stacksize)
{
	if (attr == NULL || stacksize == NULL) {
		errno = EFAULT;
		return -1;
	}

	*stacksize = attr->stack_size;

	return 0;
}

int
pthread_attr_setstacksize(pthread_attr_t *attr, size_t stacksize)
{
	if (attr == NULL) {
		errno = EFAULT;
		return -1;
	}

	attr->stack_size = stacksize;

	return 0;
}

int
pthread_create(pthread_t *pthread, const pthread_attr_t *attr, void *(*start_routine)(void *), void *arg)
{
	size_t stack_size;

	stack_size = attr == NULL ? PTHREAD_STACK_MIN : attr->stack_size;
	if ((*pthread = CreateThread(NULL, stack_size, (LPTHREAD_START_ROUTINE) start_routine, arg, 0, NULL)) == 0)
		return GetLastError();

	return 0;
}

/***********************************************************************
 *** pthread clean-up, cancel, and exit functions
 ***********************************************************************/

struct _pthread_thread {
	long thread_id;
#ifdef NOT_YET
	int cancel_enable;
#endif
	struct _pthread_cleanup_buffer *cleanup_list;
};

static List thread_list;
static CRITICAL_SECTION thread_mutex;

static void
thread_free(void *_thread)
{
	struct _pthread_thread *thread = _thread;

	if (thread != NULL) {
		free(thread);
	}
}

static int
thread_find(ListItem *item, long key)
{
	return ((struct _pthread_thread *) item->data)->thread_id == key;
}

void
_pthread_cleanup_push(struct _pthread_cleanup_buffer *buffer, void (*fn)(void *), void *arg)
{
	ListItem *item;
	struct _pthread_thread *thread;

	EnterCriticalSection(&thread_mutex);

	item = listFind(&thread_list, (ListFindFn) thread_find, (void *) GetCurrentThreadId());

	if (item == NULL) {
		/* Keep track of threads with cleanup routines. */
		if ((item = calloc(1, sizeof (*item))) == NULL)
			goto error0;

		if ((thread = malloc(sizeof (*thread))) == NULL) {
			free(item);
			goto error0;
		}

		item->data = thread;
		item->free = thread_free;
		thread->cleanup_list = NULL;
		thread->thread_id = GetCurrentThreadId();

		listInsertAfter(&thread_list, NULL, item);
	} else {
		thread = item->data;
	}

	buffer->next = thread->cleanup_list;
	thread->cleanup_list = buffer;
	buffer->cleanup_arg = arg;
	buffer->cleanup_fn = fn;
error0:
	LeaveCriticalSection(&thread_mutex);
}

void
_pthread_cleanup_pop(struct _pthread_cleanup_buffer *buffer, int execute)
{
	ListItem *item;
	struct _pthread_thread *thread;

	EnterCriticalSection(&thread_mutex);

	item = listFind(&thread_list, (ListFindFn) thread_find, (void *) GetCurrentThreadId());

	if (item != NULL) {
		thread = item->data;
		thread->cleanup_list = buffer->next;

		if (execute)
			(*buffer->cleanup_fn)(buffer->cleanup_arg);
	}

	LeaveCriticalSection(&thread_mutex);
}

void
pthread_exit(void *valuep)
{
	ListItem *item;
	struct _pthread_thread *thread;
	struct _pthread_cleanup_buffer *node;

	EnterCriticalSection(&thread_mutex);

	item = listFind(&thread_list, (ListFindFn) thread_find, (void *) GetCurrentThreadId());

	if (item != NULL) {
		thread = item->data;
		for (node = thread->cleanup_list; node != NULL; node = node->next) {
			(*node->cleanup_fn)(node->cleanup_arg);
		}
	}

	listDelete(&thread_list, item);
	LeaveCriticalSection(&thread_mutex);

	/* Note that we are casting a pointer to a DWORD. This is fine
	 * for 32 bit systems, but will likely be a problem problem for
	 * 64-Bit CPU.
	 */
	ExitThread((DWORD) valuep);
}

#ifdef NOT_YET
int
pthread_setcancelstate(int new_state, int *old_state)
{
	ListItem *item;
	long self_thread;
	struct _pthread_thread *thread;
	struct _pthread_cleanup_buffer *node;

	item = queueFind(&thread_list, (ListFindFn) thread_find, (void *) GetCurrentThreadId());

	if (item == NULL)
		return -1;

	thread = item->data;

	if (old_state != NULL)
		*old_state = thread->cancel_enable;

	thread->cancel_enable = new_state;

	return 0;
}
#endif

/* Windows documentation claims that TerminateThread is unsafe:
 *
 *	"This is because TerminateThread causes the thread to exit
 *	unexpectedly. The thread then has no chance to execute any
 *	user-mode code, and its initial stack is not deallocated.
 *	Furthermore, any DLLs attached to the thread are not notified
 *	that the thread is terminating.
 *
 * Instead we "signal" the thread handle which will terminate the
 * thread when it hits the next call pthread_testcancel. Windows
 * has no cancellation points, therefore the application must make
 * diligent use of pthread_testcancel to control termination.
 */
int
pthread_cancel(pthread_t target_thread)
{
	int rc;
	HANDLE self_thread;

	if (target_thread == NULL)
		return errno = ESRCH;

	self_thread = GetCurrentThread();
	rc = SignalObjectAndWait(target_thread, self_thread, 0, 0);
	CloseHandle(self_thread);

	return rc == WAIT_FAILED ? GetLastError() : 0;

}

void
pthread_testcancel(void)
{
	int rc;
	HANDLE self_thread;

	self_thread = GetCurrentThread();
	rc = WaitForSingleObject(self_thread, 0);
	CloseHandle(self_thread);

	if (rc == WAIT_OBJECT_0)
		pthread_exit(NULL);
}

int
pthread_detach(pthread_t pthread)
{
	/* Close either a real or pseudo handle to a thread.
	 * For pseudo handles, CloseHandle is effectively a
	 * no-op.
	 */
	CloseHandle(pthread);
	return 0;
}

pthread_t
pthread_self(void)
{
	return GetCurrentThread();
}

int
pthread_join(pthread_t pthread, void **value)
{
	DWORD exit_code;

	switch (WaitForSingleObject(pthread, INFINITE)) {
	case WAIT_TIMEOUT:
		errno = EBUSY;
		return -1;
	case WAIT_FAILED:
		errno = EINVAL;
		return -1;
	case WAIT_OBJECT_0:
	case WAIT_ABANDONED:
		if (value != NULL) {
			GetExitCodeThread(pthread, &exit_code);
			CloseHandle(pthread);
			*value = (void *) exit_code;
		}
		errno = 0;
		return 0;
	}

	return -1;
}

void
pthread_yield(void)
{
# if (_WIN32_WINNT >= 0x0400)
	(void) SwitchToThread();
# else
	SleepEx(0, 1);
# endif
}

/***********************************************************************
 *** pthread mutex functions
 ***********************************************************************/

int
pthread_mutex_init(pthread_mutex_t *handle, const pthread_mutexattr_t *attr)
{
	if ((*(volatile pthread_mutex_t *) handle = CreateMutex(NULL, FALSE, NULL)) == (HANDLE) 0)
		return -1;

	return 0;
}

static int
pthread_mutex_timeout_lock(pthread_mutex_t *handle, long timeout)
{
	if (*(volatile pthread_mutex_t *)handle == PTHREAD_MUTEX_INITIALIZER) {
		if (pthread_mutex_init(handle, NULL))
			return -1;
	}

	switch (WaitForSingleObject(*(volatile pthread_mutex_t *) handle, timeout)) {
	case WAIT_TIMEOUT:
		errno = EBUSY;
		return -1;
	case WAIT_FAILED:
		errno = EINVAL;
		return -1;
	case WAIT_OBJECT_0:
	case WAIT_ABANDONED:
		errno = 0;
		return 0;
	}

	return -1;
}

int
pthread_mutex_lock(pthread_mutex_t *handle)
{
	return pthread_mutex_timeout_lock(handle, INFINITE);
}

int
pthread_mutex_trylock(pthread_mutex_t *handle)
{
	return pthread_mutex_timeout_lock(handle, 0);
}

int
pthread_mutex_unlock(pthread_mutex_t *handle)
{
	if (ReleaseMutex(*(volatile pthread_mutex_t *) handle) == 0)
		return -1;

	return 0;
}

int
pthread_mutex_destroy(pthread_mutex_t *handle)
{
	if (*(volatile pthread_mutex_t *)handle != (HANDLE) 0)
		CloseHandle(*(volatile pthread_mutex_t *) handle);

	return 0;
}

/***********************************************************************
 *** pthread conditional variables functions
 ***
 *** Implementation based on the article:
 ***
 ***	Strategies for Implementing POSIX Condition Variables on Win32
 ***
 ***	Douglas C. Schmidt and Irfan Pyarali
 ***	Department of Computer Science
 ***	Washington University, St. Louis, Missouri
 ***
 ***	http://www.cs.wustl.edu/~schmidt/win32-cv-1.html
 ***
 ***********************************************************************/

int
pthread_cond_init(pthread_cond_t *cv, const pthread_condattr_t *attr)
{
	cv->waiters_count_ = 0;
	cv->was_broadcast_ = 0;
	InitializeCriticalSection(&cv->waiters_count_lock_);
	cv->sema_ = CreateSemaphore(NULL, 0, 0x7fffffff, NULL);
	cv->waiters_done_ = CreateEvent(NULL, FALSE, FALSE, NULL);

	return 0;
}

int
pthread_cond_destroy(pthread_cond_t *cv)
{
	DeleteCriticalSection(&cv->waiters_count_lock_);
	CloseHandle(cv->waiters_done_);
	CloseHandle(cv->sema_ );

	return 0;
}

int
pthread_cond_signal(pthread_cond_t *cv)
{
	int have_waiters;

	EnterCriticalSection(&cv->waiters_count_lock_);
	have_waiters = cv->waiters_count_ > 0;
	LeaveCriticalSection(&cv->waiters_count_lock_);

	// If there aren't any waiters, then this is a no-op.
	if (have_waiters)
		ReleaseSemaphore(cv->sema_, 1, 0);

	return 0;
}

int
pthread_cond_broadcast(pthread_cond_t *cv)
{
	int have_waiters = 0;

	// This is needed to ensure that <waiters_count_> and <was_broadcast_> are
	// consistent relative to each other.
	EnterCriticalSection(&cv->waiters_count_lock_);

	if (cv->waiters_count_ > 0) {
		// We are broadcasting, even if there is just one waiter...
		// Record that we are broadcasting, which helps optimize
		// <pthread_cond_wait> for the non-broadcast case.
		cv->was_broadcast_ = 1;
		have_waiters = 1;
	}

	if (have_waiters) {
		// Wake up all the waiters atomically.
		ReleaseSemaphore(cv->sema_, cv->waiters_count_, 0);

		LeaveCriticalSection(&cv->waiters_count_lock_);

		// Wait for all the awakened threads to acquire the counting
		// semaphore.
		WaitForSingleObject(cv->waiters_done_, INFINITE);

		// This assignment is okay, even without the <waiters_count_lock_> held
		// because no other waiter threads can wake up to access it.
		cv->was_broadcast_ = 0;
	} else {
		LeaveCriticalSection(&cv->waiters_count_lock_);
	}

	return 0;
}

static int
pthread_cond_wait_to(pthread_cond_t *cv, pthread_mutex_t *external_mutex, unsigned long timeout)
{
	int rc, last_waiter;

	// Avoid race conditions.
	EnterCriticalSection(&cv->waiters_count_lock_);
	cv->waiters_count_++;
	LeaveCriticalSection(&cv->waiters_count_lock_);

	// This call atomically releases the mutex and waits on the
	// semaphore until <pthread_cond_signal> or <pthread_cond_broadcast>
	// are called by another thread.
	SignalObjectAndWait(*external_mutex, cv->sema_, INFINITE, FALSE);

	// Reacquire lock to avoid race conditions.
	EnterCriticalSection(&cv->waiters_count_lock_);

	// We're no longer waiting...
	cv->waiters_count_--;

	// Check to see if we're the last waiter after <pthread_cond_broadcast>.
	last_waiter = cv->was_broadcast_ && cv->waiters_count_ == 0;

	LeaveCriticalSection(&cv->waiters_count_lock_);

	// If we're the last waiter thread during this particular broadcast
	// then let all the other threads proceed.
	if (last_waiter) {
		// This call atomically signals the <waiters_done_> event and waits until
		// it can acquire the <external_mutex>.  This is required to ensure fairness.
		rc = SignalObjectAndWait(cv->waiters_done_, *external_mutex, timeout, FALSE);
	} else {
		// Always regain the external mutex since that's the guarantee we
		// give to our callers.
		rc = WaitForSingleObject(*external_mutex, timeout);
	}

	switch (rc) {
	case WAIT_FAILED:
		return GetLastError();

	case WAIT_TIMEOUT:
		return ETIMEDOUT;
	}

	return 0;
}

int
pthread_cond_wait(pthread_cond_t *cv, pthread_mutex_t *mutex)
{
	return pthread_cond_wait_to(cv, mutex, INFINITE);
}

int
pthread_cond_timedwait(pthread_cond_t *cv, pthread_mutex_t *mutex, const struct timespec *abstime)
{
	CLOCK now, then;

	then = *(CLOCK *) abstime;

	CLOCK_GET(&now);
	CLOCK_SUB(&then, &now);

	return pthread_cond_wait_to(cv, mutex, TIMER_GET_MS(&then));
}

/***********************************************************************
 *** pthread setup
 ***********************************************************************/

int
pthreadInit(void)
{
	listInit(&thread_list);
	InitializeCriticalSection(&thread_mutex);

	return 0;
}

void
pthreadFini(void)
{
	DeleteCriticalSection(&thread_mutex);
	listFini(&thread_list);
}

/***********************************************************************
 ***
 ***********************************************************************/

#endif /* defined(__WIN32__) */

/*
 * POSIX states that calling pthread_mutex_destroy() with a mutex in
 * a lock state will have undefined results. This function attempts
 * to lock then unlock the mutex so that it is in suitable state for
 * pthread_mutex_destroy().
 */
int
pthreadMutexDestroy(pthread_mutex_t *mutexp)
{
	(void) pthread_mutex_trylock(mutexp);
	(void) pthread_mutex_unlock(mutexp);
	return pthread_mutex_destroy(mutexp);
}
