/*
 * queue.h
 *
 * Copyright 2009 by Anthony Howe. All rights reserved.
 */

#ifndef __com_snert_lib_type_queue_h__
#define __com_snert_lib_type_queue_h__	1

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************************
 ***
 ***********************************************************************/

#include <com/snert/lib/sys/pthread.h>

typedef void (*FreeFn)(void *);
typedef struct queue_list Queue;
typedef struct queue_item QueueItem;

struct queue_item {
	FreeFn free;
	QueueItem *prev;
	QueueItem *next;
	void *data;
};

struct queue_list {
	FreeFn free;
	QueueItem *tail;
	QueueItem *head;
	pthread_cond_t cv;
	pthread_mutex_t mutex;
};

extern int queueInit(Queue *queue);
extern void queueFini(void *_queue);
extern void queueItemFree(QueueItem *item);
extern int queueAppend(Queue *queue, QueueItem *item);
extern QueueItem *queueRemove(Queue *queue);

/***********************************************************************
 ***
 ***********************************************************************/

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_type_queue_h__ */

