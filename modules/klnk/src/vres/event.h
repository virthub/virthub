#ifndef _EVENT_H
#define _EVENT_H

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include "resource.h"

#define VRES_EVENT_BUSY       0x0001
#define VRES_EVENT_CANCEL     0x0002
#define VRES_EVENT_GROUP_MAX  256
#define VRES_EVENT_ENTRY_SIZE 3

#ifdef SHOW_EVENT
#define LOG_EVENT_SET
#define LOG_EVENT_WAIT
#define LOG_EVENT_CANCEL
#endif

#include "log_event.h"

typedef unsigned long vres_event_entry_t;

typedef struct vres_event_desc {
    unsigned long entry[VRES_EVENT_ENTRY_SIZE];
} vres_event_desc_t;

typedef struct vres_event {
    vres_event_desc_t desc;
    pthread_cond_t cond;
    rbtree_node_t node;
    size_t length;
    int flags;
    char *buf;
} vres_event_t;

typedef struct vres_event_group {
    pthread_mutex_t mutex;
    rbtree_t tree;
} vres_event_group_t;

typedef int (*vres_event_callback_t)(vres_t *resource, vres_event_t *event);

void vres_event_init();
int vres_event_cancel(vres_t *resource);
bool vres_event_exists(vres_t *resource);
int vres_event_get(vres_t *resource, vres_index_t *index);
int vres_event_set(vres_t *resource, char *buf, size_t length);
int vres_event_wait(vres_t *resource, char *buf, size_t length, struct timespec *timeout);

#endif
