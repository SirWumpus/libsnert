/**
 * Thread.h
 *
 * Copyright 2001, 2004 by Anthony Howe. All rights reserved.
 *
 * *** NOT FINISHED ***
 */

#ifndef __com_snert_lib_sys_Thread_h__
#define __com_snert_lib_sys_Thread_h__	1

typedef void *Thread;
typedef void (*ThreadFunction)(void *);

#define THREAD_WAIT_OK		0
#define THREAD_WAIT_ERROR	-1
#define	THREAD_WAIT_TIMEOUT	-2

#ifdef __cplusplus
extern "C" {
#endif

extern Thread ThreadCreate(ThreadFunction, void *);
extern void ThreadDestroy(Thread);
extern int ThreadWaitOn(Thread, int);
extern int ThreadJoin(Thread, int *);
extern void ThreadYield(void);
extern void ThreadExit(int);

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_sys_Thread_h__ */
