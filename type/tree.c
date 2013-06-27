/*
 * tree_.c
 *
 * Copyright 2013 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>
#include <stdlib.h>
#include <com/snert/lib/type/tree.h>

/***********************************************************************
 *** Low-level binary tree_ manipulation; not mutex protected.
 ***********************************************************************/

/**
 * @param node
 *	Pointer to binary tree_ node. Free all sub-tree_ nodes.
 */
void
tree_free(Tree *node, TreeWalkFn free_data)
{
	if (node != NULL) {
		if (free_data != NULL)
			(*free_data)(node->data, NULL);
		tree_free(node->right, free_data);
		tree_free(node->left, free_data);
		free(node);
	}
}

Tree *
tree_node(void *data)
{
	Tree *node;

	if ((node = calloc(1, sizeof (*node))) != NULL)
		node->data = data;

	return node;
}

/**
 */
Tree *
tree_insert(Tree *node, CmpFn cmp, void *data)
{
	if (node == NULL) {
		node = tree_node(data);
	} else if ((*cmp)(data, node->data) < 0) {
		node->left = tree_insert(node->left, cmp, data);
		node->left->parent = node;
	} else {
		node->right = tree_insert(node->right, cmp, data);
		node->right->parent = node;
	}

	return node;
}

Tree *
tree_find(Tree *node, CmpFn cmp, void *data)
{
	int diff;

	while (node != NULL) {
		if ((diff = (*cmp)(data, node->data)) == 0)
			break;
		if (diff < 0)
			node = node->left;
		else
			node = node->right;
	}

	return node;
}

void
tree_walk(Tree *node, TreeWalkFn pre, TreeWalkFn in, TreeWalkFn post, void *data)
{
	if (node != NULL) {
		if (pre != NULL)
			(*pre)(node, data);
		tree_walk(node->left, pre, in, post, data);
		if (in != NULL)
			(*in)(node, data);
		tree_walk(node->right, pre, in, post, data);
		if (post != NULL)
			(*post)(node, data);
	}
}

void
tree_pre_order(Tree *node, TreeWalkFn action, void *data)
{
	tree_walk(node, action, NULL, NULL, data);
}

void
tree_in_order(Tree *node, TreeWalkFn action, void *data)
{
	tree_walk(node, NULL, action, NULL, data);
}

void
tree_post_order(Tree *node, TreeWalkFn action, void *data)
{
	tree_walk(node, NULL, NULL, action, data);
}

size_t
tree_size(Tree *node)
{
	if (node == NULL)
		return 0;
	return tree_size(node->left) + tree_size(node->right) + 1;
}

Tree *
tree_successor(Tree *node)
{
	if (node == NULL || node->right == NULL)
		return NULL;

	for (node = node->right; node->left != NULL; node = node->left)
		;

	return node;
}

Tree *
tree_predecessor(Tree *node)
{
	if (node == NULL || node->left == NULL)
		return NULL;

	for (node = node->left; node->right != NULL; node = node->right)
		;

	return node;
}

static void *
tree_remove_node(Tree *node, Tree *child)
{
	void *node_data;

	if (node->parent != NULL) {
		if (node == node->parent->left)
			node->parent->left = child;
		else
			node->parent->right = child;
	}
	if (child != NULL)
		child->parent = node->parent;

	node->left = node->right = NULL;
	node_data = node->data;
	tree_free(node, NULL);

	return node->data;
}

/**
 * @see
 *	http://en.wikipedia.org/wiki/Binary_search_tree_#Deletion
 */
void *
tree_delete(Tree *node, CmpFn cmp, void *data)
{
	void *node_data;
	Tree *successor;
	int diff = (*cmp)(data, node->data);

	if (node == NULL) {
		node_data = NULL;
	} else if (diff < 0) {
		node_data = tree_delete(node->left, cmp, data);
	} else if (0 < diff) {
		node_data = tree_delete(node->right, cmp, data);
	} else {
		if (node->left != NULL && node->right != NULL) {
			successor = tree_successor(node);
			node_data = node->data;
			node->data = successor->data;
			(void) tree_delete(successor, cmp, data);
		} else if (node->right != NULL) {
			node_data = tree_remove_node(node, node->left);
		} else if (node->left != NULL) {
			node_data = tree_remove_node(node, node->right);
		} else {
			node_data = tree_remove_node(node, NULL);
		}
	}

	return node_data;
}

#ifdef TEST
#include <stdio.h>

static int
cmp_long(void *_a, void *_b)
{
	return (long) _a - (long) _b;
}

static void
dump_long(Tree *node, void *ignore)
{
	printf("%ld\n", (long) node->data);
}

int
main(int argc, char **argv)
{
	int argi;
	long number;
	Tree *root = NULL;

	for (argi = 1; argi < argc; argi++) {
		number = strtol(argv[argi], NULL, 10);
		root = tree_insert(root, cmp_long, (void *)number);
	}

	printf("size=%lu\n", (unsigned long) tree_size(root));

	printf("\nPreorder\n");
	tree_pre_order(root, dump_long, NULL);
	printf("\nInorder\n");
	tree_in_order(root, dump_long, NULL);
	printf("\nPostorder\n");
	tree_post_order(root, dump_long, NULL);

	tree_free(root, NULL);

	return 0;
}
#endif
