/*
 * list.c
 *
 * Copyright 2009, 2011 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <stdlib.h>
#include <string.h>

#include <com/snert/lib/type/list.h>

/***********************************************************************
 *** Low-level list manipulation; not mutex protected.
 ***********************************************************************/

/**
 * @param list
 *	A pointer to a static or dynamic list structure to be finalised.
 *	The list items are freed if necessary. The list structure itself
 *	is left unaltered and the responsibility of the caller to free
 *	or re-initialise.
 */
void
listFini(void *_list)
{
	ListItem *item, *next;
	List *list = (List *) _list;

	if (list != NULL) {
		for (item = list->head; item != NULL; item = next) {
			next = item->next;
			if (item->free != NULL)
				(*item->free)(item);
		}
	}
}

/**
 * @param list
 *	A pointer to a static or dynamic list structure to be initialised.
 */
void
listInit(List *list)
{
	memset(list, 0, sizeof (*list));
}

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

/**
 * @param list
 *	A pointer to a list.
 *
 * @param item
 *	A pointer to an item in the list that is to be removed
 *	(separated) from the list. The item itself is not freed.
 */
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
ListItem *
listFind(List *list, ListFindFn find_fn, void *data)
{
	ListItem *node, *next;

	for (node = list->head; node != NULL; node = next) {
		next = node->next;
		if ((*find_fn)(list, node, data))
			break;
	}

	return node;
}
