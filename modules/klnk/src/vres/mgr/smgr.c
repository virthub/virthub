/* smgr.c (static manager)
 *
 * Copyright (C) 2019 Yi-Wei Ci
 *
 * Distributed under the terms of the MIT license.
 */

#include "smgr.h"

int vres_smgr_get_owner(vres_t *resource)
{
    return (vres_get_off(resource) % vres_nr_managers) + 1; // the id of manager is from 1 to vres_nr_managers
}


bool vres_smgr_is_owner(vres_t *resource)
{
    return resource->owner == vres_smgr_get_owner(resource);
}


int vres_smgr_get_peers(vres_t *resource, vres_page_t *page, vres_peers_t *peers)
{
    vres_id_t owner = vres_smgr_get_owner(resource);

    peers->list[0] = owner;
    peers->total = 1;
    return 0;
}


int vres_smgr_create(vres_t *resource)
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


int vres_smgr_check_resource(vres_t *resource)
{
    char path[VRES_PATH_MAX] = {0};

    vres_get_path(resource, path);
    if (!vres_file_is_dir(path)) {
        if (VRES_CLS_SHM == resource->cls) {
            vres_mkdir(resource);
#ifdef ENABLE_PRIORITY
            vres_prio_create(resource, true);
#endif
            vres_smgr_create(resource);
        } else
            return -ENOOWNER;
    }
    return 0;
}


bool vres_smgr_check_sched(vres_t *resource, vres_page_t *page)
{
    int flags = vres_get_flags(resource);

    if (vres_pg_own(page) && (vres_pg_write(page) || (flags & VRES_RDWR)))
        return true;
    else
        return false;
}
