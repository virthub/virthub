#ifndef _SMGR_H
#define _SMGR_H

#include "resource.h"

int vres_smgr_check_resource(vres_t *resource);
bool vres_smgr_check_sched(vres_t *resource, vres_page_t *page);
int vres_smgr_get_peers(vres_t *resource, vres_page_t *page, vres_peers_t *peers);

#endif
