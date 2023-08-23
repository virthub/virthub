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


EVAL_DECL(klnk_io_sync);
int klnk_io_sync(vres_t *resource, char *in, size_t inlen, char *out, size_t outlen, vres_id_t dest) // if dest is set to -1, the request is sent to the resource owner by default
{
    int ret = 0;
    int retry = 0;
    klnk_desc_t desc;
    vres_desc_t peer;
    vres_req_t *req = NULL;

    EVAL_START(klnk_io_sync);
    if ((inlen > KLNK_IO_MAX) || (outlen > KLNK_IO_MAX)) {
        log_resource_err(resource, "invalid parameters");
        return -EINVAL;
    }
    log_klnk_io_sync(resource, dest, ">-- io_sync@start --<");
again:
    if (dest == -1) {
        ret = vres_lookup(resource, &peer);
        if (ret) {
            log_resource_err(resource, "failed to lookup (ret=%s)", log_get_err(ret));
            goto release;
        }
    } else {
        ret = vres_get_peer(dest, &peer);
        if (ret) {
            log_resource_err(resource, "failed to get peer %d (ret=%s)", dest, log_get_err(ret));
            goto release;
        }
    }
    if (!req) {
        req = vres_alloc_request(resource, in, inlen);
        if (!req) {
            log_resource_err(resource, "failed to allocate request");
            ret = -EINVAL;
            goto release;
        }
    }
    if (dest != -1)
        req->resource.owner = dest;
    else
        req->resource.owner = peer.id;
    log_klnk_io_sync_connect(resource, peer, dest);
    desc = klnk_connect(peer.address, KLNK_PORT);
    if (desc < 0) {
        log_resource_warning(resource, "failed to connect to %s (ret=%s)", addr2str(peer.address), log_get_err(desc));
        ret = -EFAULT;
        goto retry;
    }
    log_klnk_io_sync_send(resource);
    ret = klnk_send(desc, (char *)req, vres_req_size(inlen));
    if (ret) {
        log_resource_warning(resource, "failed to send to %s (ret=%s)", addr2str(peer.address), log_get_err(ret));
        goto retry;
    }
    log_klnk_io_sync_output(resource);
    ret = klnk_io_get_output(desc, out, outlen);
    if (-ENOOWNER == ret) {
        if (dest != -1)
            log_resource_err(resource, "no owner (dest=%d, owner=%d, addr=%s)", dest, req->resource.owner, addr2str(peer.address));
        else
            log_resource_err(resource, "no owner (owner=%d, addr=%s)", req->resource.owner, addr2str(peer.address));
        vres_cache_flush(resource);
        goto retry;
    }
    EVAL_END(klnk_io_sync);
    klnk_close(desc);
release:
    if (req)
        free(req);
    log_klnk_io_sync(resource, dest, ">-- io_sync@end --<");
    return ret;
retry:
    if (desc >= 0)
        klnk_close(desc);
    if (++retry < KLNK_RETRY_MAX) {
        vres_sleep(KLNK_RETRY_INTERVAL);
        goto again;
    } else {
        if (dest != -1)
            log_resource_err(resource, "failed to send (reaching the maximum retry attempts, dest=%d, owner=%d)", dest, req->resource.owner);
        else
            log_resource_err(resource, "failed to send (reaching the maximum retry attempts, owner=%d)", req->resource.owner);
    }
    free(req);
    return ret;
}


static void *klnk_io_do_create(void *buf)
{
    int ret;
    klnk_arg_t *p = (klnk_arg_t *)buf;
    vres_arg_t *arg = p->arg;
    vres_id_t dest = p->dest;
    bool release = p->release;
    vres_t *resource = &arg->resource;

    ret = klnk_io_sync(resource, arg->in, arg->inlen, arg->out, arg->outlen, dest);
    if (ret)
        log_resource_err(resource, "failed to send (ret=%s)", log_get_err(ret));
    if (release)
        free(arg);
    free(buf);
    // pthread_exit(NULL);
    log_klnk_io_create(resource, dest);
    return NULL;
}


// if release is true, arg will be released at last
int klnk_io_create(pthread_t *thread, vres_id_t dest, vres_arg_t *arg, bool release)
{
    int ret;
    pthread_attr_t attr;
    klnk_arg_t *karg = (klnk_arg_t *)calloc(1, sizeof(klnk_arg_t));

    assert(karg);
    karg->arg = arg;
    karg->dest = dest;
    karg->release = release;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    ret = pthread_create(thread, &attr, klnk_io_do_create, (void *)karg);
    pthread_attr_destroy(&attr);
    if (ret)
        log_resource_err(&arg->resource, "failed to create IO (ret=%d)", ret);
    return ret;
}


int klnk_io_async(vres_t *resource, char *in, size_t inlen, char *out, size_t outlen, vres_id_t dest, pthread_t *thread)
{
    int ret;
    vres_arg_t *arg = (vres_arg_t *)calloc(1, sizeof(vres_arg_t));

    if (!arg) {
        log_resource_err(resource, "no memory");
        return -ENOMEM;
    }
    arg->in = in;
    arg->out = out;
    arg->inlen = inlen;
    arg->outlen = outlen;
    arg->resource = *resource;
    arg->dest = -1;
    ret = klnk_io_create(thread, dest, arg, true);
    log_klnk_io_async(resource, dest);
    return ret;
}


int klnk_io_sync_by_addr(vres_t *resource, char *in, size_t inlen, char *out, size_t outlen, vres_addr_t addr)
{
    int ret = 0;
    vres_req_t *req;
    klnk_desc_t desc;

    if ((inlen > KLNK_IO_MAX) || (outlen > KLNK_IO_MAX)) {
        log_resource_err(resource, "invalid parameters");
        return -EINVAL;
    }
    req = vres_alloc_request(resource, in, inlen);
    if (!req) {
        log_resource_err(resource, "failed to generate request");
        return -ENOMEM;
    }
    desc = klnk_connect(addr, KLNK_PORT);
    if (desc < 0) {
        log_resource_err(resource, "failed to connect");
        ret = -EFAULT;
    } else {
        ret = klnk_send(desc, (char *)req, vres_req_size(inlen));
        if (!ret)
            ret = klnk_io_get_output(desc, out, outlen);
        klnk_close(desc);
    }
    free(req);
    return ret;
}
