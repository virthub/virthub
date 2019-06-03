#ifndef _DMGR_H
#define _DMGR_H

#include "resource.h"

#ifdef SHOW_DMGR
#define LOG_DMGR_FORWARD
#define LOG_DMGR_UPDATE_OWNER
#define LOG_DMGR_CHANGE_OWNER
#endif

#include "log_dmgr.h"

bool vres_dmgr_is_owner(vres_t *resource);
int vres_dmgr_check_resource(vres_t *resource);
int vres_dmgr_forward(vres_page_t *page, vres_req_t *req);
int vres_dmgr_update_owner(vres_page_t *page, vres_req_t *req);
int vres_dmgr_change_owner(vres_page_t *page, vres_req_t *req);
bool vres_dmgr_check_sched(vres_t *resource, vres_page_t *page);
int vres_dmgr_get_peers(vres_t *resource, vres_page_t *page, vres_peers_t *peers);

#endif
