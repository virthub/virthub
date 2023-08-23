#ifndef _RESOURCE_H
#define _RESOURCE_H

#include <eval.h>
#include <vres.h>
#include <net/if.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <defaults.h>
#include "log.h"
#include "redo.h"
#include "file.h"
#include "node.h"
#include "lock.h"
#include "dump.h"
#include "path.h"
#include "prio.h"
#include "cache.h"
#include "event.h"
#include "trace.h"
#include "region.h"
#include "member.h"
#include "rwlock.h"
#include "barrier.h"
#include "metadata.h"

#ifdef SHOW_RESOURCE
#define LOG_LOOKUP

#ifdef SHOW_MORE
#define LOG_JOIN
#define LOG_SYNC
#define LOG_GET_PEER
#define LOG_SAVE_PEER
#endif
#endif

#include "log_resource.h"

typedef struct vres_sync_result {
    long retval;
    vres_time_t time;
} vres_sync_result_t;

vres_reply_t *vres_sync(vres_req_t *req, int flags);
vres_reply_t *vres_join(vres_req_t *req, int flags);
vres_reply_t *vres_reply(vres_req_t *req, int flags);
vres_reply_t *vres_leave(vres_req_t *req, int flags);
vres_reply_t *vres_cancel(vres_req_t *req, int flags);

bool vres_has_task(vres_id_t id);
int vres_flush(vres_t *resource);
int vres_remove(vres_t *resource);
bool vres_create(vres_t *resource);
int vres_destroy(vres_t *resource);
bool vres_exists(vres_t *resource);
bool vres_is_owner(vres_t *resource);
int vres_get_max_key(vres_cls_t cls);
int vres_check_resource(vres_t *resource);
int vres_get_resource_count(vres_cls_t cls);
bool vres_is_initial_owner(vres_t *resource);
int vres_get_initial_owner(vres_t *resource);
int vres_check_initial_owner(vres_t *resource);
int vres_get(vres_t *resource, vres_desc_t *desc);
int vres_get_peer(vres_id_t id, vres_desc_t *peer);
int vres_save_peer(vres_id_t id, vres_desc_t *peer);
int vres_sync_request(vres_t *resource, vres_time_t *time);
int vres_get_events(vres_t *resource, vres_index_t **events);

#endif
