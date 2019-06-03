/* rpc.c
 *
 * Copyright (C) 2019 Yi-Wei Ci
 *
 * Distributed under the terms of the MIT license.
 */

#include "rpc.h"

void vres_rpc_release(vres_arg_t *arg)
{
    if (arg->buf) {
        munmap(arg->buf, arg->size);
        // close(arg->desc);
        arg->buf = NULL;
    }
}


int vres_rpc_check(vres_t *resource, unsigned long addr, size_t inlen, size_t outlen, vres_arg_t *arg)
{
    int ret = 0;
    size_t size;
    char *buf = NULL;

    size = max(inlen, outlen);
    memset(arg, 0, sizeof(vres_arg_t));
    arg->resource = *resource;
    arg->index = -1;
    if (addr && size) {
        int desc = vres_memget(addr, size, &buf);

        if (desc < 0) {
            log_resource_err(resource, "failed to get mem");
            return -EINVAL;
        }
        // arg->desc = desc;
        arg->outlen = outlen;
        arg->inlen = inlen;
        arg->size = size;
        arg->buf = buf;
        arg->out = buf;
        arg->in = buf;
    }
    switch (resource->cls) {
    case VRES_CLS_SHM:
        ret = vres_shm_check_arg(arg);
        break;
    case VRES_CLS_MSG:
    case VRES_CLS_SEM:
        ret = vres_ipc_check_arg(arg);
        break;
    default:
        break;
    }
    if (ret && (ret != -EAGAIN))
        vres_rpc_release(arg);
    return ret;
}


int vres_rpc_get(vres_t *resource, unsigned long addr, size_t inlen, size_t outlen, vres_arg_t *arg)
{
    int ret;
    vres_op_t op = vres_get_op(resource);

    log_rpc_get(resource);
    ret = vres_rpc_check(resource, addr, inlen, outlen, arg);
    if (ret) {
        log_resource_err(resource, "failed, ret=%s", log_get_err(ret));
        return ret;
    }
    if (vres_is_local(op))
        arg->call = vres_proc_local;
    else {
        if (op == VRES_OP_SHMFAULT)
            arg->call = vres_shm_call;
        else
            arg->call = klnk_call;
    }
    switch (resource->cls) {
    case VRES_CLS_SHM:
        ret = vres_shm_get_arg(resource, arg);
        break;
    case VRES_CLS_SEM:
        ret = vres_sem_get_arg(resource, arg);
        break;
    }
    if (ret) {
        log_resource_err(resource, "failed to get arg, ret=%s", log_get_err(ret));
        vres_rpc_release(arg);
    }
    return ret;
}


void vres_rpc_put(vres_arg_t *arg)
{
    vres_t *resource = &arg->resource;

    if (VRES_CLS_SHM == resource->cls)
        vres_shm_put_arg(arg);
    vres_rpc_release(arg);
    log_rpc_put(resource);
}


int vres_rpc_wait(vres_arg_t *arg)
{
    int ret = 0;
    vres_t res = arg->resource;

    if (-1 == arg->index)
        return 0;
    log_rpc_wait(&res, ">-- rpc_wait (begin) --<");
    vres_set_off(&res, arg->index);
    ret = vres_event_wait(&res, arg->out, arg->outlen, arg->timeout);
    if (arg->timeout && (-ETIMEDOUT == ret)) {
        if (vres_request_cancel(arg, arg->index)) {
            log_resource_err(&res, "timeout, failed to cancel");
            ret = -EFAULT;
        }
    } else if (ret)
        log_resource_err(&res, "ret=%s", log_get_err(ret));
    log_rpc_wait(&res, ">-- rpc_wait (end) --<");
    return ret;
}


int vres_rpc(vres_arg_t *arg)
{
    return arg->call(arg);
}
