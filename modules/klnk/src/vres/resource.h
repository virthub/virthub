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
#include "log.h"
#include "redo.h"
#include "file.h"
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

#ifdef SHOW_RESOURCE
#define LOG_RESOURCE_LOOKUP
#define LOG_RESOURCE_GET_PEER

#ifdef SHOW_MORE
#define LOG_RESOURCE_SAVE_PEER
#endif
#endif

#include "log_resource.h"

bool vres_has_task(vres_id_t id);
int vres_flush(vres_t *resource);
int vres_remove(vres_t *resource);
bool vres_create(vres_t *resource);
int vres_destroy(vres_t *resource);
bool vres_exists(vres_t *resource);
bool vres_is_owner(vres_t *resource);
int vres_get_max_key(vres_cls_t cls);
int vres_check_resource(vres_t *resource);
int vres_get_resource_count(vres_cls_t cls);
int vres_get_initial_owner(vres_t *resource);
int vres_check_initial_owner(vres_t *resource);
bool vres_is_initial_owner(vres_t *resource);
int vres_get(vres_t *resource, vres_desc_t *desc);
int vres_get_peer(vres_id_t id, vres_desc_t *peer);
int vres_save_peer(vres_id_t id, vres_desc_t *peer);
int vres_get_events(vres_t *resource, vres_index_t **events);

#endif
