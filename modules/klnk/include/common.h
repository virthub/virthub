#ifndef _COMMON_H
#define _COMMON_H

#include "rbtree.h"

typedef struct rb_tree rbtree_t;
typedef struct rb_tree_node rbtree_node_t;

#define addr2str(addr) inet_ntoa(addr)
#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))
#define tree_entry list_entry
#define rbtree_new rb_tree_new
#define rbtree_find rb_tree_find
#define rbtree_insert rb_tree_insert
#define rbtree_remove_node rb_tree_remove
#define rbtree_remove(tree, key) do { \
    rbtree_node_t *node = NULL; \
    if (!rbtree_find(tree, key, &node)) \
        rbtree_remove_node(tree, node); \
} while (0)

#endif
