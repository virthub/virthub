#ifndef _PRIO_H
#define _PRIO_H

#include "file.h"
#include "wait.h"
#include "trace.h"
#include "member.h"
#include "rwlock.h"
#include "resource.h"

#define VRES_PRIO_SYNC_TIME
#define VRES_PRIO_LOCK_GROUP_SIZE 256
#define VRES_PRIO_LOCK_ENTRY_SIZE 4
#define VRES_PRIO_NR_REPEATS      64
#define VRES_PRIO_NR_INTERVALS    256
#define VRES_PRIO_PERIOD          20000    // usec
#define VRES_PRIO_WAITTIME        5000     // usec
#define VRES_PRIO_SYNC_INTV       5000000  // usec
#define VRES_PRIO_MAX             256

#ifdef SHOW_PRIO
#define LOG_PRIO_SELECT

#ifdef SHOW_MORE
#define LOG_PRIO_CHECK
#define LOG_PRIO_CREATE
#define LOG_PRIO_SET_BUSY
#define LOG_PRIO_SET_IDLE
#define LOG_PRIO_SYNC_TIME
#endif
#endif

#include "log_prio.h"

#define vres_prio_time(prio, time) ((time) + (prio)->t_off)
#define vres_prio_interval(time) (((time) / VRES_PRIO_PERIOD / VRES_PRIO_NR_REPEATS) % VRES_PRIO_NR_INTERVALS)

typedef struct vres_prio {
    vres_id_t id;
    vres_time_t t_off;
    vres_time_t t_sync;
    vres_time_t t_update;
} vres_prio_t;

typedef unsigned long vres_prio_lock_entry_t;

typedef struct {
    vres_prio_lock_entry_t entry[VRES_PRIO_LOCK_ENTRY_SIZE];
} vres_prio_lock_desc_t;

typedef struct {
    pthread_mutex_t mutex;
    rbtree_t tree;
} vres_prio_lock_group_t;

typedef struct {
    vres_prio_lock_desc_t desc;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    rbtree_node_t node;
    int count;
} vres_prio_lock_t;

void vres_prio_init();
int vres_prio_poll(vres_req_t *req);
int vres_prio_lock(vres_t *resource);
void vres_prio_unlock(vres_t *resource);
int vres_prio_set_idle(vres_t *resource);
int vres_prio_set_busy(vres_t *resource);
int vres_prio_sync_time(vres_t *resource);
int vres_prio_check(vres_req_t *req, int flags);
int vres_prio_create(vres_t *resource, bool sync);

#endif
