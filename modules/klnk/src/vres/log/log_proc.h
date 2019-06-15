#ifndef _LOG_PROC_H
#define _LOG_PROC_H

#include "log.h"

#ifdef LOG_PROC
#define log_proc(req, str) do { \
    vres_t *resource = &req->resource; \
    if (vres_get_op(resource) == VRES_OP_SHMFAULT) { \
        vres_shmfault_arg_t *arg = (vres_shmfault_arg_t *)req->buf; \
        log_resource_info(resource, "op=%s, cmd=%s, %s", log_get_op(vres_get_op(resource)), log_get_shm_cmd(arg->cmd), str); \
    } else \
        log_resource_info(resource, "op=%s, %s", log_get_op(vres_get_op(resource)), str); \
} while(0)
#else
#define log_proc(...) do {} while (0)
#endif

#endif
