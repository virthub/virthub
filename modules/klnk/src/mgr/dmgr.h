#ifndef _DMGR_H
#define _DMGR_H

#include "resource.h"

#ifdef SHOW_DMGR
#define LOG_DMGR_FORWARD
#define LOG_DMGR_UPDATE_OWNER
#define LOG_DMGR_CHANGE_OWNER
#endif

#include "log_dmgr.h"

int vres_dmgr_forward(vres_region_t *region, vres_req_t *req);
int vres_dmgr_update_owner(vres_t *resource, vres_region_t *region);
bool vres_dmgr_check_sched(vres_t *resource, vres_region_t *region);
bool vres_dmgr_can_forward(vres_region_t *region, vres_req_t *req, int flags);
int vres_dmgr_get_peers(vres_t *resource, vres_region_t *region, vres_peers_t *peers);

#endif
