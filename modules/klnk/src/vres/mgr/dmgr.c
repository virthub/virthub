/* dmgr.c (dynamic manager)
 *
 * Copyright (C) 2019 Yi-Wei Ci
 *
 * Distributed under the terms of the MIT license.
 */

#include "dmgr.h"

int vres_dmgr_get_peers(vres_t *resource, vres_page_t *page, vres_peers_t *peers)
{
    int flags = vres_get_flags(resource);
    vres_chunk_t *chunk = vres_page_get_chunk(resource, page);

    if (!vres_page_chkown(resource, page)) {
        vres_id_t owner;

        if (!chunk->owner)
            owner = vres_get_initial_owner(resource);
        else
            owner = chunk->owner;
        peers->list[0] = owner;
        peers->total = 1;
    } else {
        if (flags & VRES_RDWR) {
            int i;
            int cnt = 0;

            for (i = 0; i < chunk->nr_holders; i++) {
                if (chunk->holders[i] != resource->owner) {
                    peers->list[cnt] = chunk->holders[i];
                    cnt++;
                }
            }
            peers->total = cnt;
        } else {
            log_resource_err(resource, "invalid mode");
            return -EINVAL;
        }
    }
    if (flags & VRES_RDWR)
        vres_page_mkcand(resource, page);
    return 0;
}


int vres_dmgr_forward(vres_page_t *page, vres_req_t *req)
{
    vres_t *resource = &req->resource;
    vres_chunk_t *chunk = vres_page_get_chunk(resource, page);

    if (chunk->owner) {
        vres_shm_req_t *shm_req = (vres_shm_req_t *)req->buf;
        int cmd = shm_req->cmd;
        int ret;

        shm_req->cmd = VRES_SHM_NOTIFY_OWNER;
        ret = klnk_io_sync(resource, req->buf, req->length, NULL, 0, chunk->owner);
        shm_req->cmd = cmd;
        log_dmgr_forward(resource, "forward to *%d*", chunk->owner);
        return ret;
    } else {
        log_dmgr_forward(resource, "*cannot* forward (no owner)");
        return -EAGAIN;
    }
}


bool vres_dmgr_check_sched(vres_t *resource, vres_page_t *page)
{
    int flags = vres_get_flags(resource);

    if (vres_page_chkown(resource, page) && (vres_page_write(resource, page) || (flags & VRES_RDWR)))
        return true;
    else
        return false;
}


int vres_dmgr_update_owner(vres_t *resource, vres_page_t *page)
{
    int flags = vres_get_flags(resource);

    if (flags & VRES_RDWR) {
        vres_chunk_t *chunk = vres_page_get_chunk(resource, page);

        chunk->owner = vres_get_id(resource);
        log_dmgr_update_owner(resource, ">> owner=%d <<", vres_get_id(resource));
    }
    return 0;
}


int vres_dmgr_change_owner(vres_page_t *page, vres_req_t *req)
{
    vres_t *resource = &req->resource;
    int flags = vres_get_flags(resource);

    if (flags & VRES_RDWR) {
        vres_set_flags(resource, VRES_CHOWN);
        log_dmgr_change_owner(resource, ">> owner=%d <<", vres_get_id(resource));
    }
    return 0;
}


bool vres_dmgr_can_own(vres_t *resource, vres_page_t *page)
{
    return vres_page_cand(resource, page);
}


bool vres_dmgr_check_forward(vres_page_t *page, vres_req_t *req)
{
    vres_t *resource = &req->resource;
    vres_chunk_t *chunk = vres_page_get_chunk(resource, page);
    vres_shm_req_t *shm_req = (vres_shm_req_t *)req->buf;
    vres_peers_t *peers = &shm_req->peers;

    return !chunk->owner && ((shm_req->cmd != VRES_SHM_CHK_OWNER) || (peers->list[0] == resource->owner));
}
