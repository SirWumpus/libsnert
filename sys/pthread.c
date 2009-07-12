/*
 * pthread.c
 *
 * Copyright 2004, 2009 by Anthony Howe.  All rights reserved.
 */

#include <com/snert/lib/version.h>
#include <errno.h>
#include <com/snert/lib/sys/pthread.h>

#if defined(__WIN32__)

int
pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine)(void *), void *arg)
{
	if ((*thread = CreateThread(NULL, 16 * 1024, (LPTHREAD_START_ROUTINE) start_routine, arg, 0, NULL)) == 0)
		return GetLastError();

	return 0;
}

int
pthread_join(pthread_t thread, void **value)
{
	DWORD exit_code;

	switch (WaitForSingleObject(thread, INFINITE)) {
	case WAIT_TIMEOUT:
		errno = EBUSY;
		return -1;
	case WAIT_FAILED:
		errno = EINVAL;
		return -1;
	case WAIT_OBJECT_0:
	case WAIT_ABANDONED:
		if (value != NULL) {
			GetExitCodeThread(thread, &exit_code);
			CloseHandle(thread);
			*value = (void *) exit_code;
		}
		errno = 0;
		return 0;
	}

	return -1;
}

void
pthread_exit(void *valuep)
{
	/* Note that we are casting a pointer to a DWORD. This is fine
	 * for 32 bit systems, but will likely be a problem problem for
	 * 64-Bit CPU.
	 */
	ExitThread((DWORD) valuep);
}

/* Windows documentation claims that TerminateThread() is unsafe:
 *
 *	"This is because TerminateThread causes the thread to exit
 *	unexpectedly. The thread then has no chance to execute any
 *	user-mode code, and its initial stack is not deallocated.
 *	Furthermore, any DLLs attached to the thread are not notified
 *	that the thread is terminating.
 */
int
pthread_cancel(pthread_t thread)
{
	int rc = TerminateThread(thread, -1) ? 0 : GetLastError();
	CloseHandle(thread);
	return rc;
}

int
pthread_detach(pthread_t thread)
{
	/* Close either a real or pseudo handle to a thread.
	 * For pseudo handles, CloseHandle is effectively a
	 * no-op.
	 */
	CloseHandle(thread);
	return 0;
}

pthread_t
pthread_self(void)
{
	return GetCurrentThread();
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
