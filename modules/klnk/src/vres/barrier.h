#ifndef _BARRIER_H
#define _BARRIER_H

#include <list.h>
#include <errno.h>
#include <common.h>
#include <pthread.h>
#include "resource.h"
#include "trace.h"

#define VRES_BARRIER_TIMEOUT    5000000 // usec
#define VRES_BARRIER_CLEAR      1
#define VRES_BARRIER_GROUP_SIZE 256
#define VRES_BARRIER_ENTRY_SIZE 4

#ifdef SHOW_BARRIER
#define LOG_BARRIER_SET
#define LOG_BARRIER_WAIT
#define LOG_BARRIER_CLEAR
#define LOG_BARRIER_WAIT_TIMEOUT
#endif

#include "log_barrier.h"

typedef unsigned long vres_barrier_entry_t;

typedef struct vres_barrier_desc {
    vres_barrier_entry_t entry[VRES_BARRIER_ENTRY_SIZE];
} vres_barrier_desc_t;

typedef struct vres_barrier_group {
    pthread_rwlock_t lock;
    rbtree_t tree;
} vres_barrier_group_t;

typedef struct vres_barrier {
    vres_barrier_desc_t desc;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    rbtree_node_t node;
    int count;
    int flags;
} vres_barrier_t;

void vres_barrier_init();
int vres_barrier_set(vres_t *resource);
int vres_barrier_wait(vres_t *resource);
int vres_barrier_clear(vres_t *resource);
int vres_barrier_wait_timeout(vres_t *resource, vres_time_t timeout);

#endif
