#ifndef _MEMBER_H
#define _MEMBER_H

#include "resource.h"
#include "trace.h"
#include "sync.h"

#ifdef SHOW_MEMBER
#define LOG_MEMBER_GET
#endif

#include "log_member.h"

#define VRES_MEMBER_MAX 128

typedef struct vres_members {
    int total;
    vres_member_t list[0];
} vres_members_t;

int vres_member_add(vres_t *resource);
int vres_member_notify(vres_req_t *req);
int vres_member_create(vres_t *resource);
int vres_member_delete(vres_t *resource);
int vres_member_is_public(vres_t *resource);
int vres_member_is_active(vres_t *resource);
int vres_member_get_pos(vres_t *resource, vres_id_t id);
int vres_member_list(vres_t *resource, vres_id_t *list);
int vres_member_update(vres_t *resource, vres_member_t *member);
int vres_member_save(vres_t *resource, vres_id_t *list, int total);
int vres_member_lookup(vres_t *resource, vres_member_t *member, int *active);

void vres_member_put(void *entry);
void *vres_member_get(vres_t *resource);
vres_members_t *vres_member_check(void *entry);

#endif
