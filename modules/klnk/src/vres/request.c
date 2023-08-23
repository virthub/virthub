#include "request.h"

int vres_request_join(vres_t *resource, vres_join_result_t **result)
{
    int ret;
    size_t size = 0;
    vres_t res = *resource;
    vres_join_result_t *p = NULL;

    log_request_join(resource);
    if (vres_member_is_public(resource)) {
        size  = sizeof(vres_join_result_t) + VRES_MEMBER_MAX * sizeof(vres_id_t);
        p = (vres_join_result_t *)malloc(size);
        if (!p) {
            log_resource_warning(resource, "no memory");
            return -ENOMEM;
        }
    }
    vres_set_op(&res, VRES_OP_JOIN);
    ret = klnk_io_sync(&res, NULL, 0, (char *)p, size, -1);
    if (ret) {
        log_resource_warning(resource, "failed to join");
        goto out;
    }
    if (p && p->retval) {
        log_resource_warning(resource, "failed to get members");
        ret = -EINVAL;
        goto out;
    }
    *result = p;
out:
    if (ret && p)
        free(p);
    return ret;
}


int vres_request_leave(vres_t *resource)
{
    int ret;
    vres_t res = *resource;
    vres_leave_result_t result;

    vres_set_op(&res, VRES_OP_LEAVE);
    ret = klnk_io_sync(&res, NULL, 0, (char *)&result, sizeof(vres_leave_result_t), -1);
    if (ret || (result.retval && (result.retval != -ENOOWNER))) {
        log_resource_warning(resource, "failed to leave");
        return -EFAULT;
    }
    return 0;
}


int vres_request_cancel(vres_arg_t *arg, vres_index_t index)
{
    int ret;
    vres_cancel_arg_t cancel;
    vres_t res = arg->resource;
    vres_op_t op = vres_get_op(&res);

    cancel.op = op;
    vres_set_op(&res, VRES_OP_CANCEL);
    vres_set_off(&res, index);
    do {
        ret = klnk_io_sync(&res, (char *)&cancel, sizeof(vres_cancel_arg_t), NULL, 0, arg->dest);
        if (-ENOENT == ret) {
            ret = vres_event_wait(&res, arg->out, arg->outlen, NULL);
            if (!ret) //The request has been processed.
                ret = -EOK;
        } else if (-ETIMEDOUT == ret) {
            struct timespec time;

            vres_set_timeout(&time, VRES_RETRY_INTERVAL);
            ret = vres_event_wait(&res, arg->out, arg->outlen, &time);
        }
    } while (-ETIMEDOUT == ret);

    return ret;
}
