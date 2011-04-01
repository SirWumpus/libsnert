/*
 * queue.c
 *
 * Copyright 2009, 2011 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <stdlib.h>

#include <com/snert/lib/sys/pthread.h>
#include <com/snert/lib/type/queue.h>
#include <com/snert/lib/util/timer.h>

/***********************************************************************
 *** Thread-safe message queue.
 ***********************************************************************/

void
queueFini(void *_queue)
{
	Queue *queue = (Queue *) _queue;

	(void) pthread_cond_destroy(&queue->cv_less);
	(void) pthread_cond_destroy(&queue->cv_more);
	(void) pthreadMutexDestroy(&queue->mutex);

	listFini(&queue->list);
}

int
queueInit(Queue *queue)
{
	listInit(&queue->list);

	if (pthread_cond_init(&queue->cv_more, NULL))
		goto error0;

	if (pthread_cond_init(&queue->cv_less, NULL))
		goto error1;

	if (pthread_mutex_init(&queue->mutex, NULL))
		goto error2;

	return 0;
error2:
	(void) pthread_cond_destroy(&queue->cv_less);
error1:
	(void) pthread_cond_destroy(&queue->cv_more);
error0:
	return -1;
}

size_t
queueLength(Queue *queue)
{
	size_t length = 0;

	PTHREAD_MUTEX_LOCK(&queue->mutex);
	length = queue->list.length;
	PTHREAD_MUTEX_UNLOCK(&queue->mutex);

	return length;
}

int
queueIsEmpty(Queue *queue)
{
	return queueLength(queue) == 0;
}

int
queueEnqueue(Queue *queue, ListItem *item)
{
	int rc = -1;

	PTHREAD_MUTEX_LOCK(&queue->mutex);
	listInsertAfter(&queue->list, queue->list.tail, item);
	rc = pthread_cond_signal(&queue->cv_more);
	PTHREAD_MUTEX_UNLOCK(&queue->mutex);

	return rc;
}

ListItem *
queueDequeue(Queue *queue)
{
	ListItem *item = NULL;

	PTHREAD_MUTEX_LOCK(&queue->mutex);

	/* Wait until the queue has an item to process. */
	while (queue->list.head == NULL) {
		if (pthread_cond_wait(&queue->cv_more, &queue->mutex))
			break;
	}

	item = queue->list.head;
	listDelete(&queue->list, item);

	if (queue->list.head == NULL)
		(void) pthread_cond_signal(&queue->cv_less);

	PTHREAD_MUTEX_UNLOCK(&queue->mutex);

	return item;
}

static int
queueRemoveFn(List *list, ListItem *item, void *queue)
{
	listDelete(list, item);
	if (item->free != NULL)
		(*item->free)(item);
	return 0;
}

void
queueRemove(Queue *queue, ListItem *item)
{
	PTHREAD_MUTEX_LOCK(&queue->mutex);
	(void) queueRemoveFn(&queue->list, item, queue);
	if (queue->list.head == NULL)
		(void) pthread_cond_signal(&queue->cv_less);
	PTHREAD_MUTEX_UNLOCK(&queue->mutex);
}

void
queueRemoveAll(Queue *queue)
{
	PTHREAD_MUTEX_LOCK(&queue->mutex);
	(void) listFind(&queue->list, queueRemoveFn, queue);
	(void) pthread_cond_signal(&queue->cv_less);
	PTHREAD_MUTEX_UNLOCK(&queue->mutex);
}

/***
 *** Do NOT use a find_fn that releases the queue's mutex; this includes
 *** pthread_cond_wait and pthread_cond_timedwait, otherwise the local
 *** state of listFind can become invalid, as other threads control the
 *** queue mutex and modify the list.
 ***/
ListItem *
queueWalk(Queue *queue, ListFindFn find_fn, void *data)
{
	ListItem *item = NULL;
	PTHREAD_MUTEX_LOCK(&queue->mutex);
	item = listFind(&queue->list, find_fn, data);
	if (queue->list.head == NULL)
		(void) pthread_cond_signal(&queue->cv_less);
	PTHREAD_MUTEX_UNLOCK(&queue->mutex);
	return item;
}

void
queueWaitEmpty(Queue *queue)
{
	PTHREAD_MUTEX_LOCK(&queue->mutex);
	while (queue->list.head != NULL) {
		if (pthread_cond_wait(&queue->cv_less, &queue->mutex))
			break;
	}
	PTHREAD_MUTEX_UNLOCK(&queue->mutex);
}

void
queueTimedWaitEmpty(Queue *queue, unsigned long ms)
{
#ifdef HAVE_PTHREAD_COND_TIMEDWAIT
	struct timespec timeout, delay;

	timespecSetMs(&delay, ms);
	timespecSetAbstime(&timeout, &delay);

	PTHREAD_MUTEX_LOCK(&queue->mutex);
	while (queue->list.head != NULL) {
		if (pthread_cond_timedwait(&queue->cv_less, &queue->mutex, &timeout))
			break;
	}
	PTHREAD_MUTEX_UNLOCK(&queue->mutex);
#endif
}
