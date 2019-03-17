/* ipc.c
 *
 * Copyright (C) 2019 Yi-Wei Ci
 *
 * Distributed under the terms of the MIT license.
 */

#include "ipc.h"

int vres_ipc_check_arg(vres_arg_t *arg)
{
    int ret;
    vres_index_t index = -1;
    vres_t *resource = &arg->resource;

    if (!vres_can_restart(resource))
        return 0;

    ret = vres_event_get(resource, &index);
    if (!ret || (-EAGAIN == ret)) {
        if (!ret) {
            arg->index = index;
            log_ipc_check_arg(resource, index);
            return -EAGAIN;
        }
        return 0;
    } else {
        log_resource_err(resource, "failed to get event");
        return ret;
    }
}


int vres_ipc_get(vres_t *resource, ipc_create_t create, ipc_init_t init)
{
    int ret = 0;
    int owner = 0;
    int flags = vres_get_flags(resource);

    vres_rwlock_wrlock(resource);
    ret = vres_check_path(resource);
    if (ret) {
        log_resource_err(resource, "failed to check path");
        vres_rwlock_unlock(resource);
        return ret;
    }

    if (flags & IPC_CREAT) {
        ret = vres_create(resource);
        if (!ret)
            owner = 1;
        else if ((-EEXIST == ret) && !(flags & IPC_EXCL))
            ret = 0;
    } else if (!vres_exists(resource))
        ret = -ENOENT;

    if (ret) {
        log_resource_err(resource, "failed to create, ret=%d", ret);
        goto out;
    }

    ret = vres_member_create(resource);
    if (ret) {
        log_resource_err(resource, "failed to create member");
        goto out;
    }

    if (owner) {
        if (create) {
            ret = create(resource);
            if (ret) {
                log_resource_err(resource, "failed to create owner");
                goto out;
            }
        }

        if (vres_member_add(resource) < 0) {
            ret = -EFAULT;
            goto out;
        }
    } else {
        vres_join_result_t *result = NULL;

        ret = vres_request_join(resource, &result);
        if (ret) {
            log_resource_err(resource, "failed to join");
            goto out;
        }

        if (result) {
            ret = vres_member_save(resource, result->list, result->total);
            free(result);
            if (ret) {
                log_resource_err(resource, "failed to save members");
                goto out;
            }
        }
    }

    if (init) {
        ret = init(resource);
        if (ret) {
            log_resource_err(resource, "failed to initialize");
            goto out;
        }
    }
out:
    //TODO: clean up
    if (ret) {
        if (owner)
            vres_remove(resource);
        vres_clear_path(resource);
    }
    vres_rwlock_unlock(resource);
    return ret;
}


int vres_ipc_put(vres_t *resource)
{
    int ret;

    vres_rwlock_wrlock(resource);
    ret = vres_destroy(resource);
    vres_rwlock_unlock(resource);
    if (ret) {
        log_resource_err(resource, "failed to leave");
        return ret;
    }
    if (!vres_is_owner(resource))
        ret = vres_request_leave(resource);
    return ret;
}
