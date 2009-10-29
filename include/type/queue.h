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
 *** Low-level Double Link List (not mutex protected).
 ***********************************************************************/

#include <com/snert/lib/sys/pthread.h>

typedef void (*FreeFn)(void *);
typedef struct list List;
typedef struct list_item ListItem;
typedef int (*ListFindFn)(ListItem *, void *);

struct list_item {
	FreeFn free;
	ListItem *prev;
	ListItem *next;
	void *data;
};

struct list {
	FreeFn free;
	size_t length;
	ListItem *tail;
	ListItem *head;
};

extern void listInit(List *list);
extern void listFini(void *_list);
extern void listDelete(List *list, ListItem *item);
extern void listInsertAfter(List *list, ListItem *node, ListItem *new_node);
extern void listInsertBefore(List *list, ListItem *node, ListItem *new_node);
extern ListItem *listFind(List *list, ListFindFn find_fn, void *key);

/***********************************************************************
 *** Message Queue (mutex protected)
 ***********************************************************************/

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
extern void queueRemoveAll(Queue *Queue);

/***********************************************************************
 ***
 ***********************************************************************/

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_type_queue_h__ */

