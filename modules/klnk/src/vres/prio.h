#ifndef _PRIO_H
#define _PRIO_H

#include "file.h"
#include "wait.h"
#include "trace.h"
#include "member.h"
#include "rwlock.h"
#include "resource.h"

//#define VRES_PRIO_NOWAIT
#define VRES_PRIO_UPDATE_CONTROL
// #define VRES_PRIO_VIRTUAL_MEMBER

#define VRES_PRIO_NR_REPEATS      32
#define VRES_PRIO_NR_INTERVALS    256
#define VRES_PRIO_PERIOD          100000   // usec
#define VRES_PRIO_SYNC_INTERVAL   10000000 // usec
#ifdef VRES_PRIO_VIRTUAL_MEMBER
#define VRES_PRIO_MAX             VRES_MEMBER_MAX * 4
#else
#define VRES_PRIO_MAX             VRES_MEMBER_MAX
#endif

#define VRES_PRIO_LOCK_GROUP_SIZE 256
#define VRES_PRIO_LOCK_ENTRY_SIZE 3

#ifdef SHOW_PRIO
#define LOG_PRIO_CHECK

#ifdef SHOW_MORE
#define LOG_PRIO_LOCK
#define LOG_PRIO_CREATE
#define LOG_PRIO_SYNC_TIME
#endif
#endif

#include "log_prio.h"

#define vres_prio_time(prio, time) ((time) + (prio)->t_off)
#define vres_prio_interval(time) (((time) / VRES_PRIO_PERIOD / VRES_PRIO_NR_REPEATS) % VRES_PRIO_NR_INTERVALS)

typedef unsigned long vres_prio_lock_entry_t;

typedef struct vres_prio_lock_desc {
    vres_prio_lock_entry_t entry[VRES_PRIO_LOCK_ENTRY_SIZE];
} vres_prio_lock_desc_t;

typedef struct vres_prio_lock_group {
    pthread_mutex_t mutex;
    rbtree_t tree;
} vres_prio_lock_group_t;

typedef struct vres_prio_lock {
    vres_prio_lock_desc_t desc;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    rbtree_node_t node;
    int count;
} vres_prio_lock_t;

typedef struct vres_prio {
    vres_id_t id;
    vres_time_t t_off;
    vres_time_t t_sync;
    vres_time_t t_update;
#ifdef CHECK_LIVE_TIME
    vres_time_t t_live;
    int val;
#endif
} vres_prio_t;

void vres_prio_init();
int vres_prio_check(vres_req_t *req);
void vres_prio_clear(vres_req_t *req);
int vres_prio_set_idle(vres_t *resource);
int vres_prio_set_busy(vres_t *resource);
int vres_prio_sync_time(vres_t *resource);
int vres_prio_create(vres_t *resource, bool sync);
#ifdef CHECK_LIVE_TIME
int vres_prio_check_live_time(vres_t *resource, int *prio, vres_time_t *live);
#endif

#endif
