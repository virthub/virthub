#ifndef _SMGR_H
#define _SMGR_H

#include "resource.h"

bool vres_smgr_check_sched(vres_t *resource, vres_region_t *region);
int vres_smgr_get_peers(vres_t *resource, vres_region_t *region, vres_peers_t *peers);

#endif
