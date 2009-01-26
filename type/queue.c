/*
 * queue.c
 *
 * Copyright 2009 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#ifndef __WIN32__
/* Needs porting to windows for conditional variables. */

#include <stdlib.h>

#include <com/snert/lib/type/queue.h>
#include <com/snert/lib/util/Text.h>

/***********************************************************************
 ***
 ***********************************************************************/

void
queueItemFree(QueueItem *item)
{
	if (item->free != NULL)
		(*item->free)(item);
}

void
queueFini(void *_queue)
{
	QueueItem *item, *next;
	Queue *queue = (Queue *) _queue;

	for (item = queue->head; item != NULL; item = next) {
		next = item->next;
		queueItemFree(item);
	}

	(void) pthread_mutex_destroy(&queue->mutex);
	(void) pthread_cond_destroy(&queue->cv);
}

int
queueInit(Queue *queue)
{
	if (pthread_cond_init(&queue->cv, NULL))
		return -1;

	if (pthread_mutex_init(&queue->mutex, NULL))
		return -1;

	queue->free = queueFini;
	queue->tail = NULL;
	queue->head = NULL;

	return 0;
}

static void
queueEnqueue(Queue *queue, QueueItem *item)
{
	item->next = NULL;

	if (queue->head == NULL) {
		queue->head = item;
		item->prev = NULL;
	} else {
		item->prev = queue->tail;
		queue->tail->next = item;
	}

	queue->tail = item;
}

static void
queueDequeue(Queue *queue, QueueItem *item)
{
	QueueItem *prev, *next;

	if (item != NULL) {
		prev = item->prev;
		next = item->next;

		if (prev == NULL)
			queue->head = next;
		else
			prev->next = next;

		if (next == NULL)
			queue->tail = prev;
		else
			next->prev = prev;
	}
}

int
queueAppend(Queue *queue, QueueItem *item)
{
	if (pthread_mutex_lock(&queue->mutex))
		return -1;

	queueEnqueue(queue, item);
	(void) pthread_mutex_unlock(&queue->mutex);
	pthread_cond_signal(&queue->cv);

	return 0;
}

QueueItem *
queueRemove(Queue *queue)
{
	QueueItem *item = NULL;

	if (!pthread_mutex_lock(&queue->mutex)) {
		/* Wait until the queue has an item to process. */
		while (queue->head == NULL) {
			if (pthread_cond_wait(&queue->cv, &queue->mutex))
				break;
		}

		if (queue->head != NULL) {
			item = queue->head;
			queueDequeue(queue, item);
		}

		(void) pthread_mutex_unlock(&queue->mutex);
	}

	return item;
}

#endif /* __WIN32__ */
