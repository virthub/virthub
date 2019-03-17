#ifndef _BARRIER_H
#define _BARRIER_H

#include <list.h>
#include <errno.h>
#include <pthread.h>
#include "resource.h"
#include "rbtree.h"
#include "trace.h"

#define VRES_BARRIER_TIMEOUT    5000000 // usec
#define VRES_BARRIER_GROUP_SIZE 1024
#define VRES_BARRIER_CLEAR      1

#ifdef SHOW_BARRIER
#define barrier_log log_resource_ln
#else
#define barrier_log(...) do {} while (0)
#endif

typedef struct vres_barrier_desc {
    unsigned long entry[4];
} vres_barrier_desc_t;

typedef struct vres_barrier_group {
    pthread_rwlock_t lock;
    rbtree head;
} vres_barrier_group_t;

typedef struct vres_barrier {
    vres_barrier_desc_t desc;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int count;
    int flags;
} vres_barrier_t;

void vres_barrier_init();
int vres_barrier_set(vres_t *resource);
int vres_barrier_wait(vres_t *resource);
int vres_barrier_clear(vres_t *resource);
int vres_barrier_wait_timeout(vres_t *resource, vres_time_t timeout);

#endif
