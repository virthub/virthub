#ifndef _MUTEX_H
#define _MUTEX_H

#include "resource.h"

#define KLNK_MUTEX_GROUP_SIZE 256
#define KLNK_MUTEX_ENTRY_SIZE 4

typedef unsigned long klnk_mutex_entry_t;

typedef struct klnk_mutex_desc {
    klnk_mutex_entry_t entry[KLNK_MUTEX_ENTRY_SIZE];
} klnk_mutex_desc_t;

typedef struct klnk_mutex_group {
    pthread_mutex_t mutex;
    rbtree_t tree;
} klnk_mutex_group_t;

typedef struct klnk_mutex {
    klnk_mutex_desc_t desc;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    rbtree_node_t node;
    int count;
} klnk_mutex_t;

void klnk_mutex_init();
int klnk_mutex_lock(vres_t *resource);
void klnk_mutex_unlock(vres_t *resource);

#endif
