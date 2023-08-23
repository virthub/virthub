#ifndef _BARRIER_H
#define _BARRIER_H

#include <list.h>
#include <errno.h>
#include <pthread.h>
#include "resource.h"
#include "trace.h"

#define KLNK_BARRIER_TIMEOUT    5000000 // usec
#define KLNK_BARRIER_CLEAR      1
#define KLNK_BARRIER_GROUP_SIZE 256
#define KLNK_BARRIER_ENTRY_SIZE 4

typedef unsigned long klnk_barrier_entry_t;

typedef struct klnk_barrier_desc {
    klnk_barrier_entry_t entry[KLNK_BARRIER_ENTRY_SIZE];
} klnk_barrier_desc_t;

typedef struct klnk_barrier_group {
    pthread_rwlock_t lock;
    rbtree_t tree;
} klnk_barrier_group_t;

typedef struct {
    klnk_barrier_desc_t desc;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    rbtree_node_t node;
    int count;
    int flags;
} klnk_barrier_t;

void klnk_barrier_init();
int klnk_barrier_set(vres_t *resource);
int klnk_barrier_wait(vres_t *resource);
int klnk_barrier_clear(vres_t *resource);
int klnk_barrier_wait_timeout(vres_t *resource, vres_time_t timeout);

#endif
