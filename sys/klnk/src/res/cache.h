#ifndef _CACHE_H
#define _CACHE_H

#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <list.h>
#include "resource.h"
#include "rbtree.h"

#define VRES_CACHE_GROUP_SIZE 		1024
#define VRES_CACHE_QUEUE_SIZE 		1024

typedef struct vres_cache_desc {
	unsigned long entry[2];
} vres_cache_desc_t;

typedef struct vres_cache {
	vres_cache_desc_t desc;
	size_t len;
	struct list_head list;
	char *buf;
} vres_cache_t;

typedef struct vres_cache_group {
	pthread_mutex_t mutex;
	struct list_head list;
	rbtree head;
	unsigned long count;
} vres_cache_group_t;

void vres_cache_init();
void vres_cache_release();
void vres_cache_flush(vres_t *resource);
int vres_cache_read(vres_t *resource, char *buf, size_t len);
int vres_cache_write(vres_t *resource, char *buf, size_t len);

#endif
