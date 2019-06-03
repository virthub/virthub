#ifndef _SYNC_H
#define _SYNC_H

#include "resource.h"

#ifdef SHOW_SYNC
#define LOG_JOIN
#endif

#include "log_sync.h"

typedef struct vres_sync_result {
    long retval;
    vres_time_t time;
} vres_sync_result_t;

vres_reply_t *vres_sync(vres_req_t *req, int flags);
vres_reply_t *vres_join(vres_req_t *req, int flags);
vres_reply_t *vres_reply(vres_req_t *req, int flags);
vres_reply_t *vres_leave(vres_req_t *req, int flags);
vres_reply_t *vres_cancel(vres_req_t *req, int flags);
int vres_sync_request(vres_t *resource, vres_time_t *time);

#endif
