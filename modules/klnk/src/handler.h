#ifndef _HANDLER_H
#define _HANDLER_H

#include "resource.h"
#include "net.h"

#define klnk_handler_err(req, err) do { \
    if (err) { \
        vres_t *resource = &req->resource; \
        if (resource->cls == VRES_CLS_SHM) { \
            vres_op_t op = vres_get_op(resource); \
            if (op == VRES_OP_SHMFAULT) { \
                vres_shmfault_arg_t *arg = (vres_shmfault_arg_t *)req->buf; \
                log_resource_info(resource, "detect error !!! (%s) <idx:%d>", log_get_err(err), (int)(arg)->index); \
            } \
        } \
    } \
} while (0)

int klnk_handler_create(void *ptr);

#endif
