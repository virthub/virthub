#include "resource.h"

#if MANAGER_TYPE == STATIC_MANAGER
#include "mgr/smgr.h"
#elif MANAGER_TYPE == DYNAMIC_MANAGER
#include "mgr/dmgr.h"
#endif

void vres_init()
{
#ifdef ENABLE_MONITOR
    vres_mon_init();
#endif
    vres_shm_init();
    vres_member_init();
    vres_file_init();
#ifdef NODE
    vres_node_init();
#endif
    vres_event_init();
    vres_lock_init();
    vres_rwlock_init();
    vres_region_lock_init();
    vres_cache_init();
    vres_prio_init();
#ifdef ENABLE_CKPT
    vres_ckpt_init();
#endif
    vres_metadata_init();
}


int vres_do_check_resource(vres_t *resource)
{
    char path[VRES_PATH_MAX] = {0};

    vres_get_path(resource, path);
    if (!vres_file_is_dir(path)) {
        log_resource_warning(resource, "no owner");
        return -ENOOWNER;
    }
    return 0;
}


bool vres_has_task(vres_id_t id)
{
    vres_t res;
    char path[VRES_PATH_MAX] = {0};

    vres_tsk_get_resource(id, &res);
    return vres_is_owner(&res);
}


int vres_check_resource(vres_t *resource)
{
    vres_t res = *resource;

    if (resource->cls == VRES_CLS_TSK)
        return 0;
    if (VRES_OP_REPLY == vres_get_op(resource)) {
        res.cls = VRES_CLS_TSK;
        res.key = vres_get_id(resource);
    }
#if MANAGER_TYPE == AREA_MANAGER && defined(ENABLE_DYNAMIC_OWNER)
    return vres_do_check_resource(&res);
#else
    return vres_check_initial_owner(&res);
#endif
}


bool vres_is_owner(vres_t *resource)
{
    char path[VRES_PATH_MAX] = {0};

    vres_get_state_path(resource, path);
    if (vres_file_access(path, F_OK))
        return false;
    else
        return true;
}


int vres_get(vres_t *resource, vres_desc_t *desc)
{
    char path[VRES_PATH_MAX] = {0};

    vres_get_key_path(resource, path);
    return vres_metadata_read(path, (char *)desc, sizeof(vres_desc_t));
}


int vres_save_peer(vres_id_t id, vres_desc_t *peer)
{
    vres_t res;

    if (!peer || (peer->id <= 0)) {
        log_warning("invalid peer");
        return -EINVAL;
    }
    res.key = id;
    res.cls = VRES_CLS_TSK;
    log_save_peer(&res, "addr=%s, pid=%d (gpid=%d)", addr2str(peer->address), peer->id, id);
    return vres_cache_write(&res, (char *)peer, sizeof(vres_desc_t));
}


int vres_get_peer(vres_id_t id, vres_desc_t *peer)
{
    int ret;
    vres_t res;

    res.key = id;
    res.cls = VRES_CLS_TSK;
    ret = vres_lookup(&res, peer);
    if (!ret)
        log_get_peer(&res, "id=%d (peer_id=%d), addr=%s", id, peer->id, addr2str(peer->address));
    else
        log_warning("failed to find peer, id=%d", id);
    return ret;
}


int vres_lookup(vres_t *resource, vres_desc_t *desc)
{
    int ret = vres_cache_read(resource, (char *)desc, sizeof(vres_desc_t));

    if (-ENOENT == ret) {
        ret = vres_get(resource, desc);
        if (!ret) {
            ret = vres_cache_write(resource, (char *)desc, sizeof(vres_desc_t));
            if (!ret)
                log_lookup(resource, "save result, addr=%s", addr2str(desc->address));
            else
                log_resource_warning(resource, "failed to save, ret=%s", log_get_err(ret));
        } else
            log_resource_warning(resource, "failed to find, ret=%s", log_get_err(ret));
    } else if (!ret)
        log_lookup(resource, "addr=%s", addr2str(desc->address));
    else if (ret)
        log_resource_warning(resource, "failed to read, ret=%s", log_get_err(ret));
    return ret;
}


bool vres_exists(vres_t *resource)
{
    char path[VRES_PATH_MAX] = {0};

    vres_get_key_path(resource, path);
    return vres_metadata_exists(path);
}


void vres_get_desc(vres_t *resource, vres_desc_t *desc)
{
    desc->address = node_addr;
    desc->id = vres_get_id(resource);
}


bool vres_create(vres_t *resource)
{
    vres_desc_t desc;
    char path[VRES_PATH_MAX] = {0};

    vres_get_desc(resource, &desc);
    vres_get_key_path(resource, path);
    if (vres_metadata_create(path, (char *)&desc, sizeof(vres_desc_t))) {
        vres_cache_write(resource, (char *)&desc, sizeof(vres_desc_t));
        return true;
    } else
        return false;
}


int vres_flush(vres_t *resource)
{
    int ret;
    char path[VRES_PATH_MAX] = {0};

    vres_get_key_path(resource, path);
    ret = vres_metadata_remove(path);
    if (ret) {
        log_resource_warning(resource, "failed to remove");
        return ret;
    }
    return 0;
}


int vres_remove(vres_t *resource)
{
    char path[VRES_PATH_MAX] = {0};

    if (vres_is_owner(resource))
        vres_flush(resource);
    vres_cache_flush(resource);
    vres_get_path(resource, path);
    vres_file_rmdir(path);
    vres_get_cls_path(resource, path);
    if (vres_file_is_empty_dir(path))
        vres_file_rmdir(path);
    return 0;
}


int vres_get_max_key(vres_cls_t cls)
{
    char path[VRES_PATH_MAX] = {0};

    vres_path_append_cls(path, cls);
    return vres_metadata_max(path);
}


int vres_get_resource_count(vres_cls_t cls)
{
    char path[VRES_PATH_MAX] = {0};

    vres_path_append_cls(path, cls);
    return vres_metadata_count(path);
}


int vres_destroy(vres_t *resource)
{
    int ret = 0;
    bool own = vres_is_owner(resource);
    bool pub = vres_member_is_public(resource);

    log_resource_ln(resource);
    if (own || pub) {
        ret = vres_member_delete(resource);
        if (ret) {
            log_resource_warning(resource, "failed to delete this member");
            return ret;
        }
        if (own) {
            switch(resource->cls) {
            case VRES_CLS_SHM:
                ret = vres_shm_destroy(resource);
                break;
            default:
                break;
            }
        }
    }
    return ret;
}


int vres_get_events(vres_t *resource, vres_index_t **events)
{
    int size;
    int ret = 0;
    vres_file_t *filp;
    char path[VRES_PATH_MAX];

    *events = NULL;
    vres_get_event_path(resource, path);
    filp = vres_file_open(path, "r");
    if (!filp)
        return 0;

    size = vres_file_size(filp);
    if (size > 0) {
        char *buf;

        buf = malloc(size);
        if (buf) {
            if (vres_file_read(buf, 1, size, filp) != size) {
                log_resource_warning(resource, "failed to read");
                free(buf);
                ret = -EIO;
            } else {
                *events = (vres_index_t *)buf;
                ret = size / sizeof(vres_index_t);
            }
        } else {
            log_resource_warning(resource, "no memory");
            ret = -ENOMEM;
        }
    }
    vres_file_close(filp);
    return ret;
}


int vres_get_initial_owner(vres_t *resource)
{
    return (vres_get_region(resource) % nr_resource_managers) + 1;
}


int vres_create_initial_owner(vres_t *resource)
{
    vres_file_t *filp;
    struct shmid_ds shmid_ds;
    char path[VRES_PATH_MAX];

    memset(&shmid_ds, 0, sizeof(struct shmid_ds));
    vres_get_state_path(resource, path);
    filp = vres_file_open(path, "w");
    if (!filp) {
        log_resource_warning(resource, "failed to create");
        return -ENOENT;
    }
    if (vres_file_write((char *)&shmid_ds, sizeof(struct shmid_ds), 1, filp) != 1) {
        vres_file_close(filp);
        return -EIO;
    }
    vres_file_close(filp);
    return 0;
}


int vres_check_initial_owner(vres_t *resource)
{
    char path[VRES_PATH_MAX] = {0};

    vres_get_path(resource, path);
    if (!vres_file_is_dir(path)) {
        if (VRES_CLS_SHM == resource->cls) {
            vres_mkdir(resource);
#ifdef ENABLE_PRIORITY
            vres_prio_create(resource, true);
#endif
            vres_create_initial_owner(resource);
        } else
            return -ENOOWNER;
    }
    return 0;
}


bool vres_is_initial_owner(vres_t *resource)
{
    return resource->owner == vres_get_initial_owner(resource);
}


vres_reply_t *vres_leave(vres_req_t *req, int flags)
{
    int ret;
    vres_t *resource = &req->resource;

    ret = vres_destroy(resource);
    if (ret)
    {
        log_resource_err(resource, "failed to release resource");
        return vres_reply_err(ret);
    }
    else
        return NULL;
}

int vres_sync_request(vres_t *resource, vres_time_t *time)
{
    int ret;
    vres_t res = *resource;
    vres_sync_result_t result;

    vres_set_op(&res, VRES_OP_SYNC);
    ret = klnk_io_sync(&res, NULL, 0, (char *)&result, sizeof(vres_sync_result_t), -1);
    if (ret || result.retval) {
        log_resource_err(&res, "failed to request");
        return ret ? ret : result.retval;
    } else {
        *time = result.time;
        return 0;
    }
}


vres_reply_t *vres_sync(vres_req_t *req, int flags)
{
    int ret = 0;
    vres_reply_t *reply = NULL;
    vres_t *resource = &req->resource;

    reply = vres_reply_alloc(sizeof(vres_sync_result_t));
    log_sync(resource, reply);
    if (reply) {
        vres_sync_result_t *result = vres_result_check(reply, vres_sync_result_t);

        result->retval = 0;
        result->time = vres_get_time();
        return reply;
    } else {
        log_resource_err(resource, "failed to sync");
        return vres_reply_err(-ENOMEM);
    }
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
    else
        return NULL;
}


vres_reply_t *vres_join(vres_req_t *req, int flags)
{
    int total;
    int ret = 0;
    bool public = false;
    vres_reply_t *reply = NULL;
    vres_t *resource = &req->resource;

    log_join(resource, ">-- join@start --<");
    if (vres_member_is_public(resource) && vres_is_owner(resource)) {
        ret = vres_member_notify(req);
        if (ret) {
            log_resource_err(resource, "failed to join (ret=%s)", log_get_err(ret));
            goto out;
        }
        public = true;
    }
    total = vres_member_add(resource);
    if (total == -EEXIST)
        goto out;
    else if (total < 0) {
        log_resource_err(resource, "failed to add member (ret=%s)", log_get_err(total));
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
            log_resource_err(resource, "failed to get members (ret=%s)", log_get_err(ret));
            free(reply);
        }
    }
out:
    log_join(resource, ">-- join@end --<");
    if (ret)
        return vres_reply_err(ret);
    else
        return reply;
}


vres_reply_t *vres_reply(vres_req_t *req, int flags)
{
    int ret = vres_event_set(&req->resource, req->buf, req->length);

    if (ret)
        return vres_reply_err(ret);
    else
        return NULL;
}
