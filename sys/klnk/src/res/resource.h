#ifndef _RESOURCE_H
#define _RESOURCE_H

#include <eval.h>
#include <vres.h>
#include <net/if.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include "tmp.h"
#include "log.h"
#include "node.h"
#include "lock.h"
#include "dump.h"
#include "path.h"
#include "page.h"
#include "prio.h"
#include "cache.h"
#include "event.h"
#include "trace.h"
#include "member.h"
#include "rwlock.h"
#include "barrier.h"
#include "metadata.h"

int vres_flush(vres_t *resource);
int vres_create(vres_t *resource);
int vres_remove(vres_t *resource);
int vres_exists(vres_t *resource);
int vres_destroy(vres_t *resource);
int vres_has_owner_path(char *path);
int vres_is_owner(vres_t *resource);
int vres_get_max_key(vres_cls_t cls);
int vres_save_peer(vres_desc_t *desc);
int vres_check_path(vres_t *resource);
int vres_check_resource(vres_t *resource);
int vres_get_resource_count(vres_cls_t cls);
int vres_get(vres_t *resource, vres_desc_t *desc);
int vres_get_peer(vres_id_t id, vres_desc_t *desc);
int vres_get_events(vres_t *resource, vres_index_t **events);

void vres_clear_path(vres_t *resource);

#endif
