#ifndef _UTIL_H
#define _UTIL_H

#include <time.h>
#include <ctrl.h>
#include <stdbool.h>
#include <sys/ipc.h>
#include <sys/types.h>
#include "resource.h"
#include "barrier.h"
#include "member.h"
#include "rwlock.h"
#include "record.h"
#include "region.h"
#include "event.h"
#include "file.h"
#include "proc.h"
#include "shm.h"
#include "sys.h"

struct vres_chunk;
struct vres_region;

#define vres_entry_off(entry) vres_entry_val1(entry)
#define vres_entry_flags(entry) vres_entry_val2(entry)
#define vres_entry_op(entry) ((vres_op_t)(entry)[VRES_POS_OP])
#define vres_entry_id(entry) ((vres_id_t)(entry)[VRES_POS_ID])
#define vres_entry_val1(entry) ((vres_val_t)(entry)[VRES_POS_VAL1])
#define vres_entry_val2(entry) ((vres_val_t)(entry)[VRES_POS_VAL2])
#define vres_entry_index(entry) ((vres_index_t)(entry)[VRES_POS_INDEX])
#define vres_entry_chunk(entry) (vres_entry_off(entry) >> VRES_CHUNK_SHIFT)

#define vres_get_err(p) ((p)->length)
#define vres_has_err(p) ((p)->length < 0)
#define vres_set_err(p, err) do { (p)->length = err; } while (0)
#define vres_is_generic_err(err) ((err) > -VRES_ERRNO_MAX && (err) < 0)

#define vres_result_check(p, type) (type *)((p)->buf)
#define vres_get_reply_owner(resource) ((resource)->key)
#define vres_get_region(resouce) (vres_get_off(resource) >> VRES_REGION_SHIFT)

#define vres_create_reply(type, err, reply) do { \
    if (err >= 0) { \
        reply = vres_reply_alloc(sizeof(type)); \
        if (reply) { \
            type *result = vres_result_check(reply, type); \
            result->retval = err; \
        } else \
            reply = vres_reply_err(-ENOMEM); \
    } else \
        reply = vres_reply_err(err); \
} while (0)

void vres_set_flags(vres_t *resource, int flags);
void vres_set_id(vres_t *resource, vres_id_t id);
void vres_set_op(vres_t *resource, vres_op_t op);
void vres_set_val1(vres_t *resource, vres_val_t val);
void vres_set_val2(vres_t *resource, vres_val_t val);
void vres_set_off(vres_t *resource, unsigned long off);
void vres_set_timeout(struct timespec *time, vres_time_t timeout);
void vres_set_entry(vres_entry_t *entry, vres_op_t op, vres_id_t id, vres_val_t val1, vres_val_t val2);

void vres_probe(vres_t *resource);
void vres_sleep(vres_time_t timeout);
void vres_clear_flags(vres_t *resource, int flags);

bool vres_is_sync(vres_op_t op);
bool vres_is_local(vres_op_t op);
bool vres_can_expose(vres_t *resource);
bool vres_can_restart(vres_t *resource);
bool vres_need_lock(vres_t *resource);
bool vres_need_reply(vres_t *resource);
bool vres_need_wrlock(vres_t *resource);
bool vres_need_half_lock(vres_t *resource);
bool vres_need_timed_lock(vres_t *resource);
bool vres_chkown(vres_t *resource, struct vres_region *region);

vres_op_t vres_get_op(vres_t *resource);
vres_id_t vres_get_id(vres_t *resource);
vres_flg_t vres_get_flags(vres_t *resource);

vres_time_t vres_get_time();
vres_time_t vres_get_time_offset(vres_time_t start, vres_time_t ref);

unsigned long vres_get_off(vres_t *resource);
unsigned long vres_get_nr_queues(vres_cls_t cls);
unsigned long vres_get_chunk_off(vres_t *resource);
unsigned long vres_get_slice_off(vres_t *resource);
unsigned long vres_get_region_off(vres_t *resource);
unsigned long vres_get_chunk_start(vres_t *resource);
unsigned long vres_get_slice_start(vres_t *resource);
unsigned long vres_get_off_by_id(int slice_id, int chunk_id);
unsigned long vres_get_chunk_off_by_id(vres_t *resource, int chunk_id);
unsigned long vres_get_chunk_start_by_id(vres_t *resource, int chunk_id);

int vres_get_slice(vres_t *resource);
int vres_get_chunk(vres_t *resource);
int vres_get_errno(vres_reply_t *reply);

int vres_err_to_index(int err);
int vres_index_to_err(vres_index_t index);

int vres_memget(void *addr, size_t size, char **buf);
void vres_memput(void *addr, size_t size);

vres_reply_t *vres_reply_err(int err);
vres_reply_t *vres_reply_alloc(size_t size);
vres_req_t *vres_alloc_request(vres_t *resource, char *buf, size_t size);

#endif
