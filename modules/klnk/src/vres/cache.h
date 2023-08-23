#ifndef _CACHE_H
#define _CACHE_H

#include <list.h>
#include <errno.h>
#include <stdlib.h>
#include <pthread.h>
#include "resource.h"

#define VRES_CACHE_GROUP_SIZE 256
#define VRES_CACHE_QUEUE_SIZE 256
#define VRES_CACHE_ENTRY_SIZE 2

typedef unsigned long vres_cache_entry_t;

typedef struct vres_cache_desc {
    vres_cache_entry_t entry[VRES_CACHE_ENTRY_SIZE];
} vres_cache_desc_t;

typedef struct vres_cache {
    vres_cache_desc_t desc;
    struct list_head list;
    rbtree_node_t node;
    size_t len;
    char *buf;
} vres_cache_t;

typedef struct vres_cache_group {
    pthread_mutex_t mutex;
    struct list_head list;
    unsigned long count;
    rbtree_t tree;
} vres_cache_group_t;

void vres_cache_init();
void vres_cache_release();
void vres_cache_flush(vres_t *resource);
int vres_cache_read(vres_t *resource, char *buf, size_t len);
int vres_cache_write(vres_t *resource, char *buf, size_t len);

#endif
