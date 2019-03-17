#ifndef _MUTEX_H
#define _MUTEX_H

#include "debug.h"
#include "rbtree.h"
#include "resource.h"

#define KLNK_MUTEX_GROUP_SIZE 1024

typedef struct klnk_mutex_desc {
    unsigned long entry[4];
} klnk_mutex_desc_t;

typedef struct klnk_mutex_group {
    pthread_mutex_t mutex;
    rbtree head;
} klnk_mutex_group_t;

typedef struct klnk_mutex {
    klnk_mutex_desc_t desc;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int count;
} klnk_mutex_t;

void klnk_mutex_init();
void klnk_mutex_unlock(vres_t *resource);
int klnk_mutex_lock(vres_t *resource);

#endif
