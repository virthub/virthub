#ifndef _TSK_H
#define _TSK_H

#include <unistd.h>
#include "resource.h"
#include "file.h"

#define TSK_RESERVE
#define TSK_WAKEUP 0x0001

#ifdef SHOW_TSK
#define LOG_TSK_PUT
#define LOG_TSK_TSKCTL
#define LOG_TSK_RESUME
#define LOG_TSK_WAKEUP
#define LOG_TSK_SUSPEND
#endif

#include "log_tsk.h"

#ifdef SHOW_TSK
#define tsk_log log_resource_ln
#else
#define tsk_log(...) do {} while(0)
#endif

typedef struct vres_tskctl_arg {
    int cmd;
} vres_tskctl_arg_t;

typedef struct vres_tskctl_result {
    long retval;
} vres_tskctl_result_t;

typedef struct vres_tsk_req {
    int cmd;
    vres_t resource;
    vres_addr_t addr;
} vres_tsk_req_t;

int vres_tsk_resume(vres_id_t id);
int vres_tsk_suspend(vres_id_t id);
int vres_tsk_put(vres_t *resource);
int vres_tsk_get(vres_t *resource, vres_id_t *id);
vres_reply_t *vres_tsk_tskctl(vres_req_t *req, int flags);
void vres_tsk_get_resource(vres_id_t id, vres_t *resource);

#endif
