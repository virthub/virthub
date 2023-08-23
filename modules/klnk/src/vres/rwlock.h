#ifndef _RWLOCK_H
#define _RWLOCK_H

#include <list.h>
#include <errno.h>
#include <pthread.h>
#include "resource.h"
#include "trace.h"

#define VRES_RWLOCK_GROUP_SIZE 256
#define VRES_RWLOCK_ENTRY_SIZE 2

#ifdef SHOW_RWLOCK
#ifdef SHOW_MORE
#define LOG_RWLOCK_GET
#define LOG_RWLOCK_WRLOCK
#define LOG_RWLOCK_RDLOCK
#define LOG_RWLOCK_UNLOCK
#endif
#endif

#include "log_rwlock.h"

typedef unsigned long vres_rwlock_entry_t;

typedef struct vres_rwlock_desc {
    vres_rwlock_entry_t entry[VRES_RWLOCK_ENTRY_SIZE];
} vres_rwlock_desc_t;

typedef struct vres_rwlock_group {
    pthread_mutex_t mutex;
    rbtree_t tree;
} vres_rwlock_group_t;

typedef struct vres_rwlock {
    vres_rwlock_desc_t desc;
    pthread_rwlock_t lock;
    rbtree_node_t node;
} vres_rwlock_t;

void vres_rwlock_init();
void vres_rwlock_release();
int vres_rwlock_rdlock(vres_t *resource);
int vres_rwlock_wrlock(vres_t *resource);
void vres_rwlock_unlock(vres_t *resource);
int vres_rwlock_trywrlock(vres_t *resource);

#endif
