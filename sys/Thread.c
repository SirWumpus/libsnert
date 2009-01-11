/**
 * Thread.c
 *
 * Copyright 2001, 2004 by Anthony Howe. All rights reserved.
 *
 * *** NOT FINISHED ***
 */

#include <stdlib.h>

#include <com/snert/lib/sys/Thread.h>

#ifdef __WIN32__

/* Assume Windows NT 4.0 functions or better.
 * See ThreadYield() concerning SwitchToThread().
 */
#define _WIN32_WINNT	0x0400

#include <windows.h>

static int
convertReturnCode(int rc)
{
	switch (rc) {
	case WAIT_OBJECT_0:
	case WAIT_ABANDONED:
		return THREAD_WAIT_OK;
	case WAIT_FAILED:
		return THREAD_WAIT_ERROR;
	case WAIT_TIMEOUT:
		return THREAD_WAIT_TIMEOUT;
	}

	return THREAD_WAIT_OK;
}

Thread
ThreadCreate(ThreadFunction fn, void *data)
{
	DWORD id;
	HANDLE h;

	h = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) fn, data, 0, &id);

	return (Thread) h;
}

void
ThreadDestroy(Thread thread)
{
	if (thread != (Thread) 0) {
		TerminateThread((HANDLE) thread, 1);
		CloseHandle((HANDLE) thread);
	}
}

void
ThreadExit(int status)
{
	ExitThread((DWORD) status);
}

int
ThreadWaitOn(Thread target, int milliseconds)
{
	if (milliseconds == 0)
		milliseconds = INFINITE;

	return convertReturnCode(WaitForSingleObject((HANDLE) target, (DWORD) milliseconds));
}

void
ThreadYield()
{
#if (_WIN32_WINNT >= 0x0400)
	(void) SwitchToThread();
#else
	SleepEx(0, 1);
#endif
}

int
ThreadJoin(Thread target, int *status)
{
	int rc;
	DWORD exit_code;

	rc = WaitForSingleObject((HANDLE) target, INFINITE);
	(void) GetExitCodeThread((HANDLE) target, &exit_code);
	if (status != (int *) 0)
		*status = exit_code;

	return convertReturnCode(rc);
}

#else /* POSIX */

#include <pthread.h>
#include <sched.h>

Thread
ThreadCreate(ThreadFunction func, void *data)
{
	pthread_t *thread;

	if ((thread = calloc(1, sizeof *thread)) == NULL)
		return NULL;

	if (pthread_create(thread, (pthread_attr_t *) 0, (void *(*)(void *)) func, data) != 0) {
		free(thread);
		return NULL;
	}

	return (Thread) thread;
}

void
ThreadDestroy(Thread thread)
{
	free(thread);
}

void
ThreadExit(int code)
{
	int *result;

	if ((result = malloc(sizeof (*result))) != NULL)
		*result = code;

	pthread_exit(result);
}

int
ThreadWaitOn(Thread target, int ms)
{
	/*** TODO ***/
	return THREAD_WAIT_ERROR;
}

void
ThreadYield()
{
	(void) sched_yield();
}

int
ThreadJoin(Thread target, int *status)
{
	return pthread_join(*(pthread_t *)target, (void **) status);
}

#endif /* __WIN32__ */
