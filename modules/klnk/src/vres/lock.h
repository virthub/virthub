#ifndef _LOCK_H
#define _LOCK_H

#include <list.h>
#include <errno.h>
#include <pthread.h>
#include "resource.h"
#include "trace.h"

#define VRES_LOCK_TIMEOUT    1000000 // usec
#define VRES_LOCK_GROUP_SIZE 1024
#define VRES_LOCK_ENTRY_SIZE 4

#ifdef SHOW_LOCK
#define LOG_LOCK
#define LOG_UNLOCK
#define LOG_LOCK_TIMEOUT

#ifdef SHOW_MORE
#define LOG_LOCK_TOP
#endif
#endif

#include "log_lock.h"

typedef unsigned long vres_lock_entry_t;

typedef struct vres_lock_desc {
    vres_lock_entry_t entry[VRES_LOCK_ENTRY_SIZE];
} vres_lock_desc_t;

typedef struct vres_lock_group {
    pthread_mutex_t mutex;
    rbtree_t tree;
} vres_lock_group_t;

typedef struct vres_lock {
    vres_lock_group_t *grp;
    vres_lock_desc_t desc;
    pthread_mutex_t mutex;
    struct list_head list;
    pthread_cond_t cond;
    rbtree_node_t node;
    int count;
} vres_lock_t;

typedef struct vres_lock_list {
    struct list_head list;
    pthread_t tid;
} vres_lock_list_t;

void vres_lock_init();
int vres_lock(vres_t *resource);
int vres_lock_buttom(vres_lock_t *lock);
void vres_unlock_top(vres_lock_t *lock);
vres_lock_t *vres_lock_top(vres_t *resource);
void vres_unlock(vres_t *resource, vres_lock_t *lock);
int vres_lock_timeout(vres_t *resource, vres_time_t timeout);

#endif
