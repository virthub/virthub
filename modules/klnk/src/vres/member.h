#ifndef _MEMBER_H
#define _MEMBER_H

#include "resource.h"
#include "trace.h"

#ifdef SHOW_MEMBER
#define LOG_MEMBER_ADD
#define LOG_MEMBER_SEND_REQ

#ifdef SHOW_MORE
#define LOG_MEMBER_GET
#define LOG_MEMBER_SAVE
#define LOG_MEMBER_UPDATE
#define LOG_MEMBER_CREATE
#endif
#endif

#include "log_member.h"

#define VRES_MEMBER_MAX     256
#define VRES_MEMBER_REQ_MAX 65536

typedef struct vres_members {
    int total;
    vres_member_t list[0];
} vres_members_t;

typedef struct vres_member_req {
    vres_req_t req;
    struct list_head list;
} vres_member_req_t;

typedef struct vres_member_req_queue {
    int cnt;
    pthread_cond_t cond;
    struct list_head head;
    pthread_mutex_t mutex;
} vres_member_req_queue_t;

void vres_member_init();
void vres_member_put(void *entry);
int vres_member_add(vres_t *resource);
void *vres_member_get(vres_t *resource);
int vres_member_notify(vres_req_t *req);
int vres_member_create(vres_t *resource);
int vres_member_delete(vres_t *resource);
bool vres_member_is_public(vres_t *resource);
bool vres_member_is_active(vres_t *resource);
vres_members_t *vres_member_check(void *entry);
int vres_member_get_pos(vres_t *resource, vres_id_t id);
int vres_member_list(vres_t *resource, vres_id_t *list);
int vres_member_update(vres_t *resource, vres_member_t *member);
int vres_member_save(vres_t *resource, vres_id_t *list, int total);
int vres_member_lookup(vres_t *resource, vres_member_t *member, int *active_members);

#endif
