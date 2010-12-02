/*
 * queue.c
 *
 * Copyright 2009, 2010 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <stdlib.h>

#include <com/snert/lib/sys/pthread.h>
#include <com/snert/lib/type/queue.h>
#include <com/snert/lib/util/Text.h>
#include <com/snert/lib/util/timer.h>

/***********************************************************************
 *** Low-level list manipulation; not mutex protected.
 ***********************************************************************/

/*** TODO place in new class. ***/

void
listItemFree(ListItem *item)
{
	if (item->free != NULL)
		(*item->free)(item);
}

void
listFini(void *_list)
{
	ListItem *item, *next;
	List *list = (List *) _list;

	if (list != NULL) {
		for (item = list->head; item != NULL; item = next) {
			next = item->next;
			listItemFree(item);
		}

		if (list->free != NULL)
			(*list->free)(list);
	}
}

void
listInit(List *list)
{
	memset(list, 0, sizeof (*list));
}

void
listInsertAfter(List *list, ListItem *node, ListItem *new_node)
{
	if (node != NULL) {
		new_node->prev = node;
		new_node->next = node->next;
		if (node->next == NULL)
			list->tail = new_node;
		else
			node->next->prev = new_node;
		node->next = new_node;
		list->length++;
	} else if (list->tail == NULL) {
		new_node->prev = new_node->next = NULL;
		list->head = list->tail = new_node;
		list->length++;
	} else {
		listInsertAfter(list, list->tail, new_node);
	}
}

void
listInsertBefore(List *list, ListItem *node, ListItem *new_node)
{
	if (node != NULL) {
		new_node->prev = node->prev;
		new_node->next = node;
		if (node->prev == NULL)
			list->head = new_node;
		else
			node->prev->next = new_node;
		node->prev = new_node;
		list->length++;
	} else if (list->head == NULL) {
		new_node->prev = new_node->next = NULL;
		list->head = list->tail = new_node;
		list->length++;
	} else {
		listInsertBefore(list, list->head, new_node);
	}

}

void
listDelete(List *list, ListItem *item)
{
	ListItem *prev, *next;

	if (item != NULL) {
		prev = item->prev;
		next = item->next;

		if (prev == NULL)
			list->head = next;
		else
			prev->next = next;

		if (next == NULL)
			list->tail = prev;
		else
			next->prev = prev;

		item->prev = item->next = NULL;
		list->length--;
	}
}

ListItem *
listFind(List *list, ListFindFn find_fn, void *key)
{
	ListItem *node, *next;

	for (node = list->head; node != NULL; node = next) {
		next = node->next;
		if ((*find_fn)(list, node, key))
			break;
	}

	return node;
}

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
queueRemoveFn(List *list, ListItem *node, void *queue)
{
	listDelete(list, node);
	if (node->free != NULL)
		(*node->free)(node->data);
	return 0;
}

void
queueRemove(Queue *queue, ListItem *node)
{
	PTHREAD_MUTEX_LOCK(&queue->mutex);
	(void) queueRemoveFn(&queue->list, node, queue);
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

	TIMER_SET_MS(&delay, ms);
	timespecSetAbstime(&timeout, &delay);

	PTHREAD_MUTEX_LOCK(&queue->mutex);
	while (queue->list.head != NULL) {
		if (pthread_cond_timedwait(&queue->cv_less, &queue->mutex, &timeout))
			break;
	}
	PTHREAD_MUTEX_UNLOCK(&queue->mutex);
#endif
}
