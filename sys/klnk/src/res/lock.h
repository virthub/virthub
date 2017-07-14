#ifndef _LOCK_H
#define _LOCK_H

#include <list.h>
#include <errno.h>
#include <pthread.h>
#include "resource.h"
#include "rbtree.h"
#include "trace.h"

#define VRES_LOCK_TIMEOUT			1000000 // usec
#define VRES_LOCK_GROUP_SIZE	1024

#ifdef SHOW_LOCK
#define lock_log vres_log
#else
#define lock_log(...) do {} while (0)
#endif

typedef struct vres_lock_desc {
	unsigned long entry[4];
} vres_lock_desc_t;

typedef struct vres_lock_group {
	pthread_mutex_t mutex;
	rbtree head;
} vres_lock_group_t;

typedef struct vres_lock {
	vres_lock_desc_t desc;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	struct list_head list;
	int count;
} vres_lock_t;

typedef struct vres_lock_list {
	pthread_t tid;
	struct list_head list;
} vres_lock_list_t;

void vres_lock_init();
void vres_unlock(vres_t *resource);
void vres_unlock_top(vres_lock_t *lock);
vres_lock_t *vres_lock_top(vres_t *resource);

int vres_lock(vres_t *resource);
int vres_lock_buttom(vres_lock_t *lock);
int vres_lock_timeout(vres_t *resource, vres_time_t timeout);

#endif
