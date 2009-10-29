/*
 * queue.c
 *
 * Copyright 2009 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <stdlib.h>

#include <com/snert/lib/type/queue.h>
#include <com/snert/lib/util/Text.h>

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

	for (item = list->head; item != NULL; item = next) {
		next = item->next;
		listItemFree(item);
	}
}

void
listInit(List *list)
{
	memset(list, 0, sizeof (*list));
	list->free = listFini;
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
	ListItem *node;

	for (node = list->head; node != NULL; node = node->next) {
		if ((*find_fn)(node, key))
			break;
	}

	return node;
}

/***********************************************************************
 ***
 ***********************************************************************/

void
queueFini(void *_queue)
{
	Queue *queue = (Queue *) _queue;

	listFini(&queue->list);

	(void) pthread_cond_destroy(&queue->cv_less);
	(void) pthread_cond_destroy(&queue->cv_more);
	(void) pthreadMutexDestroy(&queue->mutex);
}

int
queueInit(Queue *queue)
{
	listInit(&queue->list);

	queue->list.free = queueFini;

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
	unsigned length = 0;

	if (!pthread_mutex_lock(&queue->mutex)) {
#ifdef HAVE_PTHREAD_CLEANUP_PUSH
		pthread_cleanup_push((void (*)(void*)) pthread_mutex_unlock, &queue->mutex);
#endif
		length = queue->list.length;
#ifdef HAVE_PTHREAD_CLEANUP_PUSH
		pthread_cleanup_pop(1);
#else
		(void) pthread_mutex_unlock(&queue->mutex);
#endif
	}

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
	if (pthread_mutex_lock(&queue->mutex))
		return -1;
#ifdef HAVE_PTHREAD_CLEANUP_PUSH
	pthread_cleanup_push((void (*)(void*)) pthread_mutex_unlock, &queue->mutex);
#endif
	listInsertAfter(&queue->list, queue->list.tail, item);
	pthread_cond_signal(&queue->cv_more);
#ifdef HAVE_PTHREAD_CLEANUP_PUSH
	pthread_cleanup_pop(1);
#else
	(void) pthread_mutex_unlock(&queue->mutex);
#endif
	return 0;
}

ListItem *
queueDequeue(Queue *queue)
{
	ListItem *item = NULL;

	if (!pthread_mutex_lock(&queue->mutex)) {
#ifdef HAVE_PTHREAD_CLEANUP_PUSH
		pthread_cleanup_push((void (*)(void*)) pthread_mutex_unlock, &queue->mutex);
#endif
		/* Wait until the queue has an item to process. */
		while (queue->list.head == NULL) {
			if (pthread_cond_wait(&queue->cv_more, &queue->mutex))
				break;
		}

		item = queue->list.head;
		listDelete(&queue->list, item);
		pthread_cond_signal(&queue->cv_less);
#ifdef HAVE_PTHREAD_CLEANUP_PUSH
		pthread_cleanup_pop(1);
#else
		(void) pthread_mutex_unlock(&queue->mutex);
#endif
	}

	return item;
}

void
queueRemove(Queue *queue, ListItem *node)
{
	if (!pthread_mutex_lock(&queue->mutex)) {
#ifdef HAVE_PTHREAD_CLEANUP_PUSH
		pthread_cleanup_push((void (*)(void*)) pthread_mutex_unlock, &queue->mutex);
#endif
		listDelete(&queue->list, node);
		if (node->free != NULL)
			(*node->free)(node->data);
		(void) pthread_cond_signal(&queue->cv_less);
#ifdef HAVE_PTHREAD_CLEANUP_PUSH
		pthread_cleanup_pop(1);
#else
		(void) pthread_mutex_unlock(&queue->mutex);
#endif
	}
}

void
queueRemoveAll(Queue *queue)
{
	ListItem *node, *next;

	if (!pthread_mutex_lock(&queue->mutex)) {
#ifdef HAVE_PTHREAD_CLEANUP_PUSH
		pthread_cleanup_push((void (*)(void*)) pthread_mutex_unlock, &queue->mutex);
#endif
		for (node = queue->list.head; node != NULL; node = next) {
			next = node->next;
			listDelete(&queue->list, node);
			if (node->free != NULL)
				(*node->free)(node->data);
			(void) pthread_cond_signal(&queue->cv_less);
		}
#ifdef HAVE_PTHREAD_CLEANUP_PUSH
		pthread_cleanup_pop(1);
#else
		(void) pthread_mutex_unlock(&queue->mutex);
#endif
	}
}

