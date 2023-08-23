#ifndef _MON_H
#define _MON_H

#include "resource.h"

#define VRES_MON_REQ_MAX 1024

#ifdef SHOW_MON
#define LOG_MON_RELEASE
#define LOG_MON_REQ_GET
#define LOG_MON_REQ_PUT
#endif

#include "log_mon.h"

typedef struct {
    vres_cls_t cls;
    vres_key_t key;
    unsigned long pos;
} vres_mon_id_t;

typedef struct {
    vres_time_t t;
    vres_mon_id_t id;
    struct list_head list;
} vres_mon_entry_t;

void vres_mon_init();
void vres_mon_req_get(vres_t *res);
void vres_mon_req_put(vres_t *res);

#endif
