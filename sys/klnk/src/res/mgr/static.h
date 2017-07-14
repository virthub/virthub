#ifndef _STATIC_H
#define _STATIC_H

#include "resource.h"

int vres_smgr_is_owner(vres_t *resource);
int vres_smgr_check_resource(vres_t *resource);
int vres_smgr_request_holders(vres_page_t *page, vres_req_t *req);
int vres_smgr_check_coverage(vres_t *resource, vres_page_t *page, int line);
int vres_smgr_get_peers(vres_t *resource, vres_page_t *page, vres_peers_t *peers);
void vres_smgr_check_reply(vres_t *resource, vres_page_t *page, int total, int *head, int *tail, int *reply);

#endif
