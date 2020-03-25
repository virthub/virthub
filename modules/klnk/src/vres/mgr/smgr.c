/* smgr.c (static manager)
 *
 * Copyright (C) 2019 Yi-Wei Ci
 *
 * Distributed under the terms of the MIT license.
 */

#include "smgr.h"

int vres_smgr_get_peers(vres_t *resource, vres_page_t *page, vres_peers_t *peers)
{
    vres_id_t owner = vres_get_initial_owner(resource);

    peers->list[0] = owner;
    peers->total = 1;
    return 0;
}


bool vres_smgr_check_sched(vres_t *resource, vres_page_t *page)
{
    int flags = vres_get_flags(resource);

    if (vres_page_chkown(resource, page) && (vres_page_write(resource, page) || (flags & VRES_RDWR)))
        return true;
    else
        return false;
}
