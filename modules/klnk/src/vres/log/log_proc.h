#ifndef _LOG_PROC_H
#define _LOG_PROC_H

#include "log.h"

#ifdef LOG_PROC
#define log_proc(req) do { \
    vres_t *resource = &req->resource; \
    log_resource(resource); \
    log(", op=%s", log_get_op(vres_get_op(resource))); \
    if (vres_get_op(resource) == VRES_OP_SHMFAULT) { \
        vres_shmfault_arg_t *arg = (vres_shmfault_arg_t *)req->buf; \
        log(", cmd=%s", log_get_shmop(arg->cmd)); \
    } \
    log("\n"); \
} while(0)
#else
#define log_proc(...) do {} while (0)
#endif

#endif
