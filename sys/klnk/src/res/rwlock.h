#ifndef _RWLOCK_H
#define _RWLOCK_H

#include <list.h>
#include <errno.h>
#include <pthread.h>
#include "resource.h"
#include "rbtree.h"
#include "trace.h"

#define VRES_RWLOCK_GROUP_SIZE		1024

#ifdef SHOW_RWLOCK
#define rwlock_log vres_log
#else
#define rwlock_log(...) do {} while (0)
#endif

typedef struct vres_rwlock_desc {
	unsigned long entry[2];
} vres_rwlock_desc_t;

typedef struct vres_rwlock_group {
	pthread_mutex_t mutex;
	rbtree head;
} vres_rwlock_group_t;

typedef struct vres_rwlock {
	vres_rwlock_desc_t desc;
	pthread_rwlock_t lock;
	struct list_head list;
} vres_rwlock_t;

void vres_rwlock_init();
void vres_rwlock_release();
void vres_rwlock_unlock(vres_t *resource);

int vres_rwlock_rdlock(vres_t *resource);
int vres_rwlock_wrlock(vres_t *resource);
int vres_rwlock_trywrlock(vres_t *resource);

#endif
