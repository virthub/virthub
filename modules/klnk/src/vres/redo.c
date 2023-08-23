#include "redo.h"

int vres_redo_req(vres_req_t *req, vres_index_t index, int flags)
{
    int ret = 0;
    vres_reply_t *reply;
    vres_t *resource = &req->resource;

    reply = vres_proc(req, flags | VRES_REDO);
    if (reply) {
        if (vres_has_err(reply)) {
            ret = vres_get_err(reply);
        } else {
            vres_t res = *resource;
            vres_id_t src = vres_get_id(resource);

            vres_set_op(&res, VRES_OP_REPLY);
            vres_set_off(&res, index);
            klnk_io_sync(&res, reply->buf, reply->length, NULL, 0, src);
        }
        free(reply);
    }
    log_redo_req(resource, req->buf, index, ret);
    return ret;
}


int vres_redo(vres_t *resource, int flags)
{
    int ret;
    vres_index_t index;
    vres_record_t record;
    char path[VRES_PATH_MAX];

    vres_get_record_path(resource, path);
    log_redo(resource, ">-- redo@start --<");
    ret = vres_record_head(path, &index);
    while (!ret) {
        ret = vres_record_get(path, index, &record);
        if (ret) {
            log_resource_err(resource, "failed to get record");
            return ret;
        }
        ret = vres_redo_req(record.req, index, flags);
        vres_record_put(&record);
        if (ret) {
             // TODO: handle redo fault
            assert(ret == -EAGAIN);
            break;
        }
        vres_record_remove(path, index);
        ret = vres_record_next(path, &index);
    }
    log_redo(resource, ">-- redo@end --<");
    return 0;
}


int vres_redo_all(vres_t *resource, int flags)
{
    int i;
    int ret;
    int nr_queues;
    vres_index_t index;
    vres_record_t record;
    char path[VRES_PATH_MAX];
    char *pend;

    vres_get_path(resource, path);
    if (!vres_file_is_dir(path)) {
        log_resource_err(resource, "failed to redo all, cannot get the path of the resource");
        return -ENOENT;
    }
    log_redo_all(resource, ">-- redo_all@start --<");
    pend = path + strlen(path);
    nr_queues = vres_get_nr_queues(resource->cls);
    for (i = 0; i < nr_queues; i++) {
        vres_path_append_que(path, i);
        ret = vres_record_head(path, &index);

        while (!ret) {
            ret = vres_record_get(path, index, &record);
            if (ret) {
                log_resource_err(resource, "failed to get record");
                return ret;
            }
            ret = vres_redo_req(record.req, index, flags);
            vres_record_put(&record);
            if (!ret)
                vres_record_remove(path, index);
            ret = vres_record_next(path, &index);
        }
        pend[0] = '\0';
    }
    log_redo_all(resource, ">-- redo_all@end --<");
    return 0;
}
