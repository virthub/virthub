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
    if (ret && !vres_is_generic_err(ret)) {
        arg->index = vres_err_to_index(ret);
        ret = 0;
    }

    return ret;
}


int klnk_call_broadcast(vres_arg_t *arg)
{
    int ret = 0;

    if (!arg->peers)
        ret = klnk_io_sync(&arg->resource, arg->in, arg->inlen, arg->out, arg->outlen, arg->dest);
    else {
        int i;
        int count = 0;
        pthread_t *threads = (pthread_t *)malloc(arg->peers->total * sizeof(pthread_t));

        if (!threads) {
            klnk_log_err("no memory");
            return -ENOMEM;
        }

        for (i = 0; i < arg->peers->total; i++) {
            ret = klnk_io_async(&arg->resource, arg->in, arg->inlen, arg->out, arg->outlen, &arg->peers->list[i], &threads[i]);
            if (ret) {
                klnk_log_err("I/O error");
                break;
            }
            count++;
        }

        for (i = 0; i < count; i++)
            pthread_join(threads[i], NULL);
        free(threads);
    }

    return ret;
}
