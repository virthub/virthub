#include "rpc.h"

void klnk_rpc_release(vres_arg_t *arg)
{
    if (arg->buf) {
        vres_memput(arg->buf, arg->size);
        arg->buf = NULL;
    }
}


int klnk_rpc_check(vres_t *resource, klnk_request_t *req, vres_arg_t *arg)
{
    int ret = 0;
    size_t size;
    char *buf = NULL;

    size = max(req->inlen, req->outlen);
    memset(arg, 0, sizeof(vres_arg_t));
    arg->resource = *resource;
    arg->index = -1;
    arg->dest = -1;
    if (size) {
        if (vres_memget(req->buf, size, &buf) < 0) {
            log_resource_warning(resource, "failed to get mem");
            return -EINVAL;
        }
        arg->outlen = req->outlen;
        arg->inlen = req->inlen;
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
        klnk_rpc_release(arg);
    return ret;
}


int klnk_rpc_get(vres_t *resource, klnk_request_t *req, vres_arg_t *arg)
{
    int ret;
    vres_op_t op = vres_get_op(resource);

    log_klnk_rpc_get(resource);
    ret = klnk_rpc_check(resource, req, arg);
    if (ret)
        return ret;
    if (vres_is_local(op))
        arg->call = vres_proc_local;
    else {
        if (op == VRES_OP_SHMFAULT)
            arg->call = vres_shm_call;
        else
            arg->call = klnk_rpc_send;
    }
    switch (resource->cls) {
    case VRES_CLS_SHM:
        ret = vres_shm_get_arg(resource, arg, 0);
        break;
    case VRES_CLS_SEM:
        ret = vres_sem_get_arg(resource, arg);
        break;
    }
    if (ret) {
        if (-EAGAIN == ret)
            return ret;
        else if (ret != -EOK)
            log_resource_warning(resource, "failed, ret=%s", log_get_err(ret));
        klnk_rpc_release(arg);
    }
    return ret;
}


void klnk_rpc_put(vres_arg_t *arg)
{
    vres_t *resource = &arg->resource;

    klnk_rpc_release(arg);
    log_klnk_rpc_put(resource);
}


static inline bool klnk_need_wait(vres_arg_t *arg)
{
    return -1 != arg->index;
}


int klnk_rpc_wait(vres_arg_t *arg)
{
    int ret = 0;
    vres_t res = arg->resource;
    vres_t *resource = &res;

    if (!klnk_need_wait(arg))
        return 0;
    vres_set_off(resource, arg->index);
    log_klnk_rpc_wait(resource, ">-- rpc_wait@start --<");
    ret = vres_event_wait(resource, arg->out, arg->outlen, arg->timeout);
    if (arg->timeout && (-ETIMEDOUT == ret)) {
        if (vres_request_cancel(arg, arg->index)) {
            log_resource_warning(resource, "timeout, failed to cancel");
            ret = -EFAULT;
        }
    } else if (ret)
        log_resource_warning(resource, "ret=%s", log_get_err(ret));
    log_klnk_rpc_wait(resource, ">-- rpc_wait@end --<");
    return ret;
}


int klnk_rpc(vres_arg_t *arg)
{
    return arg->call(arg);
}


int klnk_rpc_send(vres_arg_t *arg)
{
    int ret = klnk_io_sync(&arg->resource, arg->in, arg->inlen, arg->out, arg->outlen, arg->dest);

    if (ret) {
        if (!vres_is_generic_err(ret)) {
            arg->index = vres_err_to_index(ret);
            ret = 0;
        } else
            log_resource_warning(&arg->resource, "failed, ret=%s", log_get_err(ret));
    }
    return ret;
}


int klnk_rpc_send_to_peers(vres_arg_t *arg)
{
    int ret = 0;
    vres_t *resource = &arg->resource;

    if (!arg->peers)
        ret = klnk_io_sync(resource, arg->in, arg->inlen, arg->out, arg->outlen, arg->dest);
    else {
        int i;
        int count = 0;
        pthread_t threads[KLNK_PEER_MAX];

        if (arg->peers->total > KLNK_PEER_MAX) {
            log_resource_warning(resource, "too much peers (total=%d)", arg->peers->total);
            return -EINVAL;
        }
        for (i = 0; i < arg->peers->total; i++) {
            ret = klnk_io_create(&threads[i], arg->peers->list[i], arg, false);
            if (ret) {
                log_resource_warning(resource, "failed, ret=%s", log_get_err(ret));
                break;
            }
            count++;
        }
        for (i = 0; i < count; i++)
            pthread_join(threads[i], NULL);
    }
    log_klnk_rpc_send_to_peers(resource);
    return ret;
}
