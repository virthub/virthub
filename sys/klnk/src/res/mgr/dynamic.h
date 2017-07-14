#ifndef _DYNAMIC_H
#define _DYNAMIC_H

#include "resource.h"

int vres_dmgr_is_owner(vres_t *resource);
int vres_dmgr_check_resource(vres_t *resource);
int vres_dmgr_forward(vres_page_t *page, vres_req_t *req);
int vres_dmgr_update_owner(vres_page_t *page, vres_req_t *req);
int vres_dmgr_request_holders(vres_page_t *page, vres_req_t *req);
int vres_dmgr_check_coverage(vres_t *resource, vres_page_t *page, int line);
int vres_dmgr_get_peers(vres_t *resource, vres_page_t *page, vres_peers_t *peers);
void vres_dmgr_check_reply(vres_t *resource, vres_page_t *page, int total, int *head, int *tail, int *reply);

#endif
