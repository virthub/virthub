#ifndef _LOG_PROC_H
#define _LOG_PROC_H

#include "log.h"

#ifdef LOG_PROC
#define _log_proc(req, fmt, ...) do { \
    vres_t *resource = &req->resource; \
    if (vres_get_op(resource) == VRES_OP_SHMFAULT) { \
        vres_shm_req_t *shm_req = (vres_shm_req_t *)req->buf; \
        log_resource_info(resource, "op=%s, cmd=%s, " fmt, log_get_op(vres_get_op(resource)), log_get_shm_cmd(shm_req->cmd), ##__VA_ARGS__); \
    } else \
        log_resource_info(resource, "op=%s, " fmt, log_get_op(vres_get_op(resource)), ##__VA_ARGS__); \
} while(0)

#define log_proc(req, rep, start) do { \
    if (start) \
        _log_proc(req, ">-- proc@start --<"); \
    else { \
        if (vres_get_errno(rep) < 0) \
            _log_proc(req, ">-- proc@end (ret=%s) --<", log_get_err(vres_get_errno(rep))); \
        else \
            _log_proc(req, ">-- proc@end --<"); \
    } \
} while (0)
#else
#define log_proc(...) do {} while (0)
#endif

#endif
