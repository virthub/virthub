#include "smgr.h"

int vres_smgr_get_peers(vres_t *resource, vres_region_t *region, vres_peers_t *peers)
{
    vres_id_t owner = vres_get_initial_owner(resource);

    vres_set_flags(resource, VRES_CHUNK);
    peers->list[0] = owner;
    peers->total = 1;
    return 0;
}


bool vres_smgr_check_sched(vres_t *resource, vres_region_t *region)
{
    int flags = vres_get_flags(resource);

    if (vres_chkown(resource, region) && (vres_region_wr(resource, region) || (flags & VRES_RDWR)))
        return true;
    else
        return false;
}
