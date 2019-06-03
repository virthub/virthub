/* io.c
 *
 * Copyright (C) 2019 Yi-Wei Ci
 *
 * Distributed under the terms of the MIT license.
 */

#include "io.h"

int klnk_io_get_output(klnk_desc_t desc, char *buf, size_t size)
{
    int ret = 0;

    if (klnk_recv(desc, (char *)&ret, sizeof(int)) < 0) {
        log_err("failed to receive");
        return -EIO;
    }
    if (ret > 0) {
        if (ret > size) {
            log_err("invalid length");
            return -EINVAL;
        }
        if (klnk_recv(desc, buf, ret) < 0) {
            log_err("failed to receive");
            return -EIO;
        }
        ret = 0;
    }
    return ret;
}


static void *klnk_io_create(void *buf)
{
    vres_arg_t *arg = (vres_arg_t *)buf;

    klnk_io_sync(&arg->resource, arg->in, arg->inlen, arg->out, arg->outlen, arg->dest);
    free(buf);
    return NULL;
}


EVAL_DECL(klnk_io_sync);
int klnk_io_sync(vres_t *resource, char *in, size_t inlen, char *out, size_t outlen, vres_id_t *dest)
{
    int ret = 0;
    int retry = 0;
    klnk_desc_t desc;
    vres_desc_t peer;
    char *buf = NULL;
    vres_req_t *req = NULL;
    int buflen = sizeof(vres_req_t) + inlen;

    EVAL_START(klnk_io_sync);
    if ((inlen > KLNK_IO_MAX) || (outlen > KLNK_IO_MAX)) {
        log_err("invalid parameters");
        return -EINVAL;
    }
    log_klnk_io_sync(resource, ">-- io_sync (begin) --<");
again:
    if (!dest) {
        ret = vres_lookup(resource, &peer);
        if (ret) {
            log_err("failed to lookup (ret=%s)", log_get_err(ret));
            goto release;
        }
    } else {
        ret = vres_get_peer(*dest, &peer);
        if (ret) {
            log_err("failed to get peer %d (ret=%s)", *dest, log_get_err(ret));
            goto release;
        }
    }
    if (!buf) {
        buf = malloc(buflen);
        if (!buf) {
            log_err("no memory");
            ret = -ENOMEM;
            goto release;
        }
        req = (vres_req_t *)buf;
        req->resource = *resource;
        req->length = inlen;
        ret = vres_get_peer(vres_get_id(resource), &req->src);
        if (ret) {
            log_err("failed to get peer %d (ret=%s)", vres_get_id(resource), log_get_err(ret));
            goto release;
        }
        if (inlen > 0)
            memcpy(req->buf, in, inlen);
    }
    if (dest)
        req->resource.owner = *dest;
    else
        req->resource.owner = peer.id;
    log_klnk_io_sync_connect(resource, peer);
    desc = klnk_connect(peer.address, KLNK_PORT);
    if (desc < 0) {
        log_err("failed to connect to %s (ret=%s)", vres_addr2str(peer.address), log_get_err(desc));
        ret = -EFAULT;
        goto retry;
    }
    log_klnk_io_sync_send(resource);
    ret = klnk_send(desc, buf, buflen);
    if (ret) {
        log_err("failed to send to %s (ret=%s)", vres_addr2str(peer.address), log_get_err(ret));
        goto retry;
    }
    log_klnk_io_sync_output(resource);
    ret = klnk_io_get_output(desc, out, outlen);
    if (-ENOOWNER == ret) {
        if (dest)
            log_resource_err(resource, "no owner (dest=%d, owner=%d, addr=%s)", *dest, req->resource.owner, vres_addr2str(peer.address));
        else
            log_resource_err(resource, "no owner (owner=%d, addr=%s)", req->resource.owner, vres_addr2str(peer.address));
        vres_cache_flush(resource);
        goto retry;
    }
    EVAL_END(klnk_io_sync);
    klnk_close(desc);
release:
    if (req)
        free(req);
    log_klnk_io_sync(resource, ">-- io_sync (end, ret=%s) --<", log_get_err(ret));
    return ret;
retry:
    if (desc >= 0)
        klnk_close(desc);
    if (++retry < KLNK_RETRY_MAX) {
        vres_sleep(KLNK_RETRY_INTERVAL);
        goto again;
    } else {
        if (dest)
            log_resource_err(resource, "failed to send (reaching the maximum retry attempts, dest=%d, owner=%d)", *dest, req->resource.owner);
        else
            log_resource_err(resource, "failed to send (reaching the maximum retry attempts, owner=%d)", req->resource.owner);
    }
    free(req);
    log_klnk_io_sync(resource, ">-- io_sync (end, ret=%s) --<", log_get_err(ret));
    return ret;
}


int klnk_io_async(vres_t *resource, char *in, size_t inlen, char *out, size_t outlen, vres_id_t *dest, pthread_t *thread)
{
    int ret;
    pthread_attr_t attr;
    vres_arg_t *arg = (vres_arg_t *)malloc(sizeof(vres_arg_t));

    if (!arg) {
        log_err("no memory");
        return -ENOMEM;
    }
    arg->in = in;
    arg->out = out;
    arg->dest = dest;
    arg->inlen = inlen;
    arg->outlen = outlen;
    arg->resource = *resource;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    ret = pthread_create(thread, &attr, klnk_io_create, (void *)arg);
    pthread_attr_destroy(&attr);
    if (ret)
        free(arg);
    return ret;
}


int klnk_io_direct(vres_t *resource, char *in, size_t inlen, char *out, size_t outlen, vres_addr_t addr)
{
    int ret = 0;
    char *buf = NULL;
    klnk_desc_t desc;
    vres_req_t *req = NULL;
    int buflen = sizeof(vres_req_t) + inlen;

    if ((inlen > KLNK_IO_MAX) || (outlen > KLNK_IO_MAX)) {
        log_err("invalid parameters");
        return -EINVAL;
    }
    buf = malloc(buflen);
    if (!buf) {
        log_err("no memory");
        return -ENOMEM;
    }
    memset(buf, 0, buflen);
    req = (vres_req_t *)buf;
    req->src.id = -1;
    req->resource = *resource;
    req->length = inlen;
    if (inlen > 0)
        memcpy(req->buf, in, inlen);
    desc = klnk_connect(addr, KLNK_PORT);
    if (desc < 0) {
        log_err("failed to connect");
        ret = -EFAULT;
    } else {
        ret = klnk_send(desc, buf, buflen);
        if (!ret)
            ret = klnk_io_get_output(desc, out, outlen);
        klnk_close(desc);
    }
    free(buf);
    return ret;
}
