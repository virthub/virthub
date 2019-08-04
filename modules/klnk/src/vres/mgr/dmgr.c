/* dmgr.c (dynamic manager)
 *
 * Copyright (C) 2019 Yi-Wei Ci
 *
 * Distributed under the terms of the MIT license.
 */

#include "dmgr.h"

int vres_dmgr_get_owner(vres_t *resource)
{
    return (vres_get_off(resource) % vres_nr_managers) + 1; // the id of manager is from 1 to vres_nr_managers
}


bool vres_dmgr_is_owner(vres_t *resource)
{
    return resource->owner == vres_dmgr_get_owner(resource);
}


int vres_dmgr_get_peers(vres_t *resource, vres_page_t *page, vres_peers_t *peers)
{
    int flags = vres_get_flags(resource);

    if (!vres_pg_own(page)) {
        vres_id_t owner;

        if (!page->owner)
            owner = vres_dmgr_get_owner(resource);
        else
            owner = page->owner;
        peers->list[0] = owner;
        peers->total = 1;
    } else {
        if (flags & VRES_RDWR) {
            int i;
            int cnt = 0;

            for (i = 0; i < page->nr_holders; i++) {
                if (page->holders[i] != resource->owner) {
                    peers->list[cnt] = page->holders[i];
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
        vres_pg_mkcand(page);
    return 0;
}


int vres_dmgr_create(vres_t *resource)
{
    vres_file_t *filp;
    struct shmid_ds shmid_ds;
    char path[VRES_PATH_MAX];

    memset(&shmid_ds, 0, sizeof(struct shmid_ds));
    vres_get_state_path(resource, path);
    filp = vres_file_open(path, "w");
    if (!filp) {
        log_resource_err(resource, "failed to create");
        return -ENOENT;
    }
    if (vres_file_write((char *)&shmid_ds, sizeof(struct shmid_ds), 1, filp) != 1) {
        vres_file_close(filp);
        return -EIO;
    }
    vres_file_close(filp);
    return 0;
}


int vres_dmgr_check_resource(vres_t *resource)
{
    char path[VRES_PATH_MAX] = {0};

    vres_get_path(resource, path);
    if (!vres_file_is_dir(path)) {
        if (VRES_CLS_SHM == resource->cls) {
            vres_mkdir(resource);
#ifdef ENABLE_PRIORITY
            vres_prio_create(resource, true);
#endif
            vres_dmgr_create(resource);
        } else
            return -ENOOWNER;
    }
    return 0;
}


int vres_dmgr_forward(vres_page_t *page, vres_req_t *req)
{
    vres_t *resource = &req->resource;

    if (page->owner) {
        vres_shmfault_arg_t *arg = (vres_shmfault_arg_t *)req->buf;
        int cmd = arg->cmd;
        int ret;

        arg->cmd = VRES_SHM_NOTIFY_OWNER;
        ret = klnk_io_sync(resource, req->buf, req->length, NULL, 0, &page->owner);
        arg->cmd = cmd;
        log_dmgr_forward(resource, "forward to *%d*", page->owner);
        return ret;
    } else {
        log_dmgr_forward(resource, "*cannot* forward (no owner)");
        return -EAGAIN;
    }
}


bool vres_dmgr_check_sched(vres_t *resource, vres_page_t *page)
{
    int flags = vres_get_flags(resource);

    if (vres_pg_own(page) && (vres_pg_write(page) || (flags & VRES_RDWR)))
        return true;
    else
        return false;
}


int vres_dmgr_update_owner(vres_t *resource, vres_page_t *page)
{
    int flags = vres_get_flags(resource);

    if (flags & VRES_RDWR) {
        page->owner = vres_get_id(resource);
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


bool vres_dmgr_page_own(vres_page_t *page)
{
    return vres_pg_cand(page);
}
