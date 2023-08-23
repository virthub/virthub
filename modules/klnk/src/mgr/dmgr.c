#include "dmgr.h"

int vres_dmgr_get_peers(vres_t *resource, vres_region_t *region, vres_peers_t *peers)
{
    int flags = vres_get_flags(resource);
    vres_slice_t *slice = vres_region_get_slice(resource, region);
    vres_chunk_t *chunk = &slice->chunks[vres_get_chunk(resource)];

    if (!vres_chkown(resource, region)) {
        vres_id_t owner;
        
        if (!slice->owner)
            owner = vres_get_initial_owner(resource);
        else
            owner = slice->owner;
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
            log_resource_err(resource, "invalid state of owner");
            return -EINVAL;
        }
    }
    if (flags & VRES_RDWR)
        vres_set_flags(resource, VRES_CHOWN);
    vres_set_flags(resource, VRES_CHUNK);
    return 0;
}


int vres_dmgr_forward(vres_region_t *region, vres_req_t *req)
{
    vres_t *resource = &req->resource;
    vres_slice_t *slice = vres_region_get_slice(resource, region);

    if (slice->owner) {
        vres_shm_req_t *shm_req = (vres_shm_req_t *)req->buf;
        int cmd = shm_req->cmd;
        int ret;

        shm_req->cmd = VRES_SHM_NOTIFY_OWNER;
        ret = klnk_io_sync(resource, req->buf, req->length, NULL, 0, slice->owner);
        shm_req->cmd = cmd;
        log_dmgr_forward(resource, "forward to *%d*", slice->owner);
        return ret;
    } else {
        log_dmgr_forward(resource, "*cannot* forward (no owner)");
        return -EAGAIN;
    }
}


bool vres_dmgr_check_sched(vres_t *resource, vres_region_t *region)
{
    int flags = vres_get_flags(resource);

    if (vres_chkown(resource, region) && (vres_region_wr(resource, region) || (flags & VRES_RDWR)))
        return true;
    else
        return false;
}


int vres_dmgr_update_owner(vres_t *resource, vres_region_t *region)
{
    int flags = vres_get_flags(resource);

    if (flags & VRES_CHOWN) {
        vres_slice_t *slice = vres_region_get_slice(resource, region);

        slice->owner = vres_get_id(resource);
        log_dmgr_update_owner(resource, ">> owner=%d <<", vres_get_id(resource));
    }
    return 0;
}


bool vres_dmgr_can_forward(vres_region_t *region, vres_req_t *req, int flags)
{
    vres_t *resource = &req->resource;
    vres_shm_req_t *shm_req = (vres_shm_req_t *)req->buf;
    vres_peers_t *peers = &shm_req->peers;

    return !(flags & VRES_OWN) && ((shm_req->cmd != VRES_SHM_CHK_OWNER) || (peers->list[0] == resource->owner));
}
