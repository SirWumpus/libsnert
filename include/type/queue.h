/*
 * queue.h
 *
 * Copyright 2009, 2010 by Anthony Howe. All rights reserved.
 */

#ifndef __com_snert_lib_type_queue_h__
#define __com_snert_lib_type_queue_h__	1

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************************
 *** Message Queue (mutex protected)
 ***********************************************************************/

#include <com/snert/lib/sys/pthread.h>
#include <com/snert/lib/type/list.h>

typedef struct queue_list Queue;

struct queue_list {
	List list;
	pthread_mutex_t mutex;
	pthread_cond_t cv_more;
	pthread_cond_t cv_less;
};

extern int queueInit(Queue *queue);
extern void queueFini(void *_queue);
extern int queueIsEmpty(Queue *queue);
extern size_t queueLength(Queue *queue);
extern void queueItemFree(ListItem *item);
extern ListItem *queueDequeue(Queue *queue);
extern int queueEnqueue(Queue *queue, ListItem *item);
extern void queueRemove(Queue *queue, ListItem *item);
extern void queueRemoveAll(Queue *queue);
extern ListItem *queueWalk(Queue *queue, ListFindFn find_fn, void *data);
extern void queueWaitEmpty(Queue *queue);
extern void queueTimedWaitEmpty(Queue *queue, unsigned long ms);

/***********************************************************************
 ***
 ***********************************************************************/

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_type_queue_h__ */

