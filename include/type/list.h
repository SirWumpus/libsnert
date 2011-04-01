/*
 * list.h
 *
 * Copyright 2009, 2011 by Anthony Howe. All rights reserved.
 */

#ifndef __com_snert_lib_type_list_h__
#define __com_snert_lib_type_list_h__	1

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************************
 *** Low-level Double Link List (not mutex protected).
 ***********************************************************************/

typedef struct list List;
typedef struct list_item ListItem;

/**
 * @param list
 *	A pointer to a list.
 *
 * @param item
 *	A pointer to the current list item being searched.
 *
 * @param data
 *	A pointer to search data and/or additional information.
 *
 * @return
 *	True if the current item is found, else false to continue.
 */
typedef int (*ListFindFn)(List *list, ListItem *item, void *data);

struct list_item {
	FreeFn free;		/* How to free this structure and/or data, NULL if static. */
	ListItem *prev;
	ListItem *next;
	void *data;
};

struct list {
	size_t length;
	ListItem *tail;
	ListItem *head;
};

/**
 * @param list
 *	A pointer to a static or dynamic list structure to be initialised.
 */
extern void listInit(List *list);

/**
 * @param list
 *	A pointer to a static or dynamic list structure to be finalised.
 *	The list items are freed if necessary. The list structure itself
 *	is left unaltered and the responsibility of the caller to free
 *	or re-initialise.
 */
extern void listFini(void *_list);

/**
 * @param list
 *	A pointer to a list.
 *
 * @param item
 *	A pointer to an item in the list that is to be removed
 *	(separated) from the list. The item itself is not freed.
 */
extern void listDelete(List *list, ListItem *item);

/**
 * @param list
 *	A pointer to a list.
 *
 * @param node
 *	A pointer to an item already in the list.
 *
 * @param new_node
 *	A pointer to an item to be inserted after a given node in the list.
 */
extern void listInsertAfter(List *list, ListItem *node, ListItem *new_node);

/**
 * @param list
 *	A pointer to a list.
 *
 * @param node
 *	A pointer to an item already in the list.
 *
 * @param new_node
 *	A pointer to an item to be inserted before a given node in the list.
 */
extern void listInsertBefore(List *list, ListItem *node, ListItem *new_node);

/**
 * @param list
 *	A pointer to a list to search.
 *
 * @param find_fn
 *	A search function that returns true to stop the search
 *	or false to continue.
 *
 * @return
 *	A pointer to a list item found or NULL.
 */
extern ListItem *listFind(List *list, ListFindFn find_fn, void *key_data);

/***********************************************************************
 ***
 ***********************************************************************/

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_type_list_h__ */

