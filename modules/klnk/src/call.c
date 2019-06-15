/* call.c
 *
 * Copyright (C) 2019 Yi-Wei Ci
 *
 * Distributed under the terms of the MIT license.
 */

#include "call.h"

int klnk_call(vres_arg_t *arg)
{
    int ret;

    ret = klnk_io_sync(&arg->resource, arg->in, arg->inlen, arg->out, arg->outlen, arg->dest);
    if (ret) {
        if (!vres_is_generic_err(ret)) {
            arg->index = vres_err_to_index(ret);
            ret = 0;
        } else
            log_resource_err(&arg->resource, "failed, ret=%s", log_get_err(ret));
    }
    return ret;
}


int klnk_broadcast(vres_arg_t *arg)
{
    int ret = 0;

    if (!arg->peers)
        ret = klnk_io_sync(&arg->resource, arg->in, arg->inlen, arg->out, arg->outlen, arg->dest);
    else {
        int i;
        int count = 0;
        pthread_t threads[KLNK_CALL_MAX];

        if (arg->peers->total > KLNK_CALL_MAX) {
            log_resource_err(&arg->resource, "too much requests (total=%d)", arg->peers->total);
            return -EINVAL;
        }
        for (i = 0; i < arg->peers->total; i++) {
            ret = klnk_io_async(&arg->resource, arg->in, arg->inlen, arg->out, arg->outlen, &arg->peers->list[i], &threads[i]);
            if (ret) {
                log_resource_err(&arg->resource, "failed, ret=%s", log_get_err(ret));
                break;
            }
            count++;
        }
        for (i = 0; i < count; i++)
            pthread_join(threads[i], NULL);
    }
    log_klnk_broadcast(&arg->resource);
    return ret;
}
