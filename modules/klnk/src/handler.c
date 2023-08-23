#include "handler.h"

static inline int klnk_handler_get(klnk_desc_t desc, vres_req_t **preq)
{
    int ret;
    vres_req_t *req;
    vres_req_t head;

    ret = klnk_recv(desc, (char *)&head, sizeof(vres_req_t));
    if ((ret < 0) || (head.length > VRES_BUF_MAX) || (head.length < 0))
        return -EINVAL;
    req = (vres_req_t *)malloc(sizeof(vres_req_t) + head.length);
    if (!req)
        return -ENOMEM;
    if (head.length > 0) {
        ret = klnk_recv(desc, req->buf, head.length);
        if (ret) {
            free(req);
            return ret;
        }
    }
    memcpy(req, &head, sizeof(vres_req_t));
    *preq = req;
    return 0;
}


EVAL_DECL(klnk_handler);
static inline void *klnk_handler(void *arg)
{
    int ret;
    vres_req_t *req;
    vres_t *resource;
    vres_lock_t *lock = NULL;
    vres_reply_t *rep = NULL;
    klnk_desc_t desc = (klnk_desc_t)arg;

    EVAL_START(klnk_handler);
    ret = klnk_handler_get(desc, &req);
    if (ret) {
        log_err("failed to get request");
        goto out;
    }
    resource = &req->resource;
    if (vres_need_timed_lock(resource)) {
        log_klnk_handler(resource, ">-- handler@start (timed lock) --<");
        ret = vres_lock_timeout(resource, VRES_LOCK_TIMEOUT);
    } else if (vres_need_half_lock(resource)) {
        int err;
        log_klnk_handler(resource, ">-- handler@start (half lock) --<");
        lock = vres_lock_top(resource);
        if (!lock) {
            log_resource_err(resource, "failed to lock");
            ret = -EINVAL;
        }
        err = klnk_send(desc, (char *)&ret, sizeof(int));
        if (err) {
            log_resource_err(resource, "failed to send reply");
            ret = err;
        }
        klnk_close(desc);
        if (!ret)
            ret = vres_lock_buttom(lock);
        else if (lock)
            vres_unlock_top(lock);
    } else if (vres_need_wrlock(resource)) {
        log_klnk_handler(resource, ">-- handler@start (wrlock) --<");
        ret = vres_rwlock_wrlock(resource);
    } else if (vres_need_lock(resource)) {
        log_klnk_handler(resource, ">-- handler@start (lock) --<");
        ret = vres_lock(resource);
    } else
        log_klnk_handler(resource, ">-- handler@start (no lock) --<");
    if (ret)
        goto reply;
    ret = vres_check_resource(resource);
    if (ret) {
        klnk_handler_err(req, ret);
        goto unlock;
    }
    rep = vres_proc(req, 0);
    ret = vres_get_errno(rep);
    if (ret < 0) {
        free(rep);
        rep = NULL;
    }
unlock:
    if (vres_need_wrlock(resource))
        vres_rwlock_unlock(resource);
    else if (vres_need_lock(resource))
        vres_unlock(resource, lock);
    log_klnk_handler(resource, ">-- handler@end --<");
reply:
    if (vres_need_reply(resource)) {
        if (rep)
            klnk_send(desc, (void *)rep, rep->length + sizeof(vres_reply_t));
        else
            klnk_send(desc, (void *)&ret, sizeof(int));
        klnk_close(desc);
    }
    free(req);
    if (rep)
        free(rep);
out:
    EVAL_END(klnk_handler);
    return NULL;
}


int klnk_handler_create(void *ptr)
{
    int ret;
    pthread_t tid;
    pthread_attr_t attr;

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
    ret = pthread_create(&tid, &attr, klnk_handler, ptr);
    pthread_attr_destroy(&attr);
    return ret;
}
