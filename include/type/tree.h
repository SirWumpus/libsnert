/*
 * tree.h
 *
 * Copyright 2013 by Anthony Howe. All rights reserved.
 */

#ifndef __com_snert_lib_type_tree_h__
#define __com_snert_lib_type_tree_h__	1

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************************
 *** Low-level Binary Tree (not mutex protected).
 ***********************************************************************/

typedef struct tree Tree;

struct tree {
	void *data;
	Tree *left;
	Tree *right;
	Tree *parent;
};

#ifndef HAVE_CMPFN_T
/**
 * @param a
 *	A pointer to object A.
 *
 * @param b
 *	A pointer to object B.
 *
 * @return
 *	Zero if A == B, negative if A < B, or positive if A > B.
 */
typedef int (*CmpFn)(void *a, void *b);
#endif

typedef void (*TreeWalkFn)(Tree *node, void *data);

extern Tree *tree_node(void *data);

/**
 * @param node
 *	Pointer to binary tree node. Free all sub-tree nodes.
 */
extern void tree_free(Tree *node, TreeWalkFn free_data);

/**
 */
extern Tree *tree_insert(Tree *root, CmpFn cmp, void *data);

/**
 * @param root
 *	A pointer to a tree to search.
 *
 * @param cmp_fn
 *	A compare function that returns -1 (<), 0 (=), or 1 (>) when
 *	comparing data and node->data.
 *
 * @return
 *	A pointer to a tree node found or NULL.
 */
extern Tree *tree_find(Tree *root, CmpFn cmp, void *data);

/**
 * @param root
 *	A pointer to a tree to walk.
 *
 * @param action
 *	A function applied to each node traversed.
 */
extern void tree_pre_order(Tree *root, TreeWalkFn action, void *data);
extern void tree_in_order(Tree *root, TreeWalkFn action, void *data);
extern void tree_post_order(Tree *root, TreeWalkFn action, void *data);
extern void tree_walk(Tree *node, TreeWalkFn pre, TreeWalkFn in, TreeWalkFn post, void *data);

extern size_t tree_size(Tree *node);
extern Tree *tree_successor(Tree *node);
extern Tree *tree_predecessor(Tree *node);

/***********************************************************************
 ***
 ***********************************************************************/

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_type_tree_h__ */

