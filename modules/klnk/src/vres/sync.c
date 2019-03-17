/* sync.c
 *
 * Copyright (C) 2019 Yi-Wei Ci
 *
 * Distributed under the terms of the MIT license.
 */

#include "sync.h"

int vres_sync_lock(vres_t *resource, pthread_mutex_t **lock)
{
    int ret;
    static int init = 0;
    static pthread_mutex_t mutex;

    if (!init) {
        init = 1;
        pthread_mutex_init(&mutex, NULL);
    }

    *lock = NULL;
    ret = vres_rwlock_trywrlock(resource);
    if (ret) {
        if (EBUSY == ret) {
            pthread_mutex_lock(&mutex);
            *lock = &mutex;
        } else
            return -EINVAL;
    }

    return 0;
}


void vres_sync_unlock(vres_t *resource, pthread_mutex_t *lock)
{
    if (lock)
        pthread_mutex_unlock(lock);
    else
        vres_rwlock_unlock(resource);
}


int vres_sync_request(vres_t *resource, vres_time_t *time)
{
    int ret;
    vres_t res = *resource;
    vres_sync_result_t result;

    vres_set_op(&res, VRES_OP_SYNC);
    ret = klnk_io_sync(&res, NULL, 0, (char *)&result, sizeof(vres_sync_result_t), NULL);
    if (ret || result.retval) {
        log_resource_err(&res, "failed to request");
        return ret ? ret : result.retval;
    }

    *time = result.time;
    return 0;
}


vres_reply_t *vres_sync(vres_req_t *req, int flags)
{
    int ret = 0;
    vres_reply_t *reply = NULL;

    vres_create_reply(vres_sync_result_t, ret, reply);
    if (reply) {
        vres_sync_result_t *result;

        result = vres_result_check(reply, vres_sync_result_t);
        result->time = vres_get_time();
    }

    return reply;
}


vres_reply_t *vres_cancel(vres_req_t *req, int flags)
{
    int ret;
    char path[VRES_PATH_MAX];
    vres_t res = req->resource;
    vres_index_t index = vres_get_off(&res);
    vres_cancel_arg_t *arg = (vres_cancel_arg_t *)req->buf;

    vres_set_op(&res, arg->op);
    vres_get_record_path(&res, path);
    ret = vres_record_remove(path, index);
    if (ret)
        return vres_reply_err(ret);
    return NULL;
}


vres_reply_t *vres_join(vres_req_t *req, int flags)
{
    int total;
    int ret = 0;
    int public = 0;
    pthread_mutex_t *lock;
    vres_reply_t *reply = NULL;
    vres_t *resource = &req->resource;

    ret = vres_sync_lock(resource, &lock);
    if (ret) {
        log_resource_err(resource, "failed to lock");
        goto err;
    }

    if (vres_member_is_public(resource) && vres_is_owner(resource)) {
        ret = vres_member_notify(req);
        if (ret) {
            log_resource_err(resource, "failed to join");
            goto out;
        }
        public = 1;
    }

    total = vres_member_add(resource);
    if (total < 0) {
        log_resource_err(resource, "failed to add member");
        ret = -EINVAL;
        goto out;
    }

    if (public) {
        vres_join_result_t *result;
        size_t size = sizeof(vres_join_result_t) + total * sizeof(vres_id_t);

        reply = vres_reply_alloc(size);
        if (!reply) {
            log_resource_err(resource, "no memory");
            ret = -ENOMEM;
            goto out;
        }

        result = vres_result_check(reply, vres_join_result_t);
        result->retval = 0;
        result->total = total;
        ret = vres_member_list(resource, result->list);
        if (ret) {
            log_resource_err(resource, "failed to get members");
            free(reply);
        }
    }
    log_sync(resource);
out:
    vres_sync_unlock(resource, lock);
err:
    if (ret)
        return vres_reply_err(ret);
    return reply;
}


vres_reply_t *vres_leave(vres_req_t *req, int flags)
{
    int ret;
    pthread_mutex_t *lock;
    vres_t *resource = &req->resource;

    ret = vres_sync_lock(resource, &lock);
    if (ret) {
        log_resource_err(resource, "failed to lock");
        goto out;
    }
    ret = vres_destroy(resource);
    if (ret)
        log_resource_err(resource, "failed to destroy");
    vres_sync_unlock(resource, lock);
out:
    if (ret)
        return vres_reply_err(ret);
    return NULL;
}


vres_reply_t *vres_reply(vres_req_t *req, int flags)
{
    int ret = vres_event_set(&req->resource, req->buf, req->length);

    if (ret)
        return vres_reply_err(ret);
    return NULL;
}
