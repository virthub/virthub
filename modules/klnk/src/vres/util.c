/* util.c
 *
 * Copyright (C) 2019 Yi-Wei Ci
 *
 * Distributed under the terms of the MIT license.
 */

#include "util.h"

char master_addr[16];
vres_addr_t node_addr;
int vres_nr_managers = 0;
char node_name[NODE_NAME_SIZE];
char master_name[NODE_NAME_SIZE];
vres_addr_t *vres_managers = NULL;

void vres_sleep(vres_time_t timeout)
{
    pthread_cond_t cond;
    struct timespec time;
    pthread_mutex_t mutex;

    if (timeout <= 0)
        return;
    pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&cond, NULL);
    vres_set_timeout(&time, timeout);
    pthread_mutex_lock(&mutex);
    pthread_cond_timedwait(&cond, &mutex, &time);
    pthread_mutex_unlock(&mutex);
}


int vres_get_errno(vres_reply_t *reply)
{
    if (!reply)
        return 0;
    if (vres_has_err(reply))
        return vres_get_err(reply);
    return reply->length;
}


bool vres_can_expose(vres_t *resource)
{
    vres_op_t op = vres_get_op(resource);

    if ((VRES_OP_DUMP == op) || (VRES_OP_RESTORE == op))
        return true;
    else
        return false;
}


bool vres_can_restart(vres_t *resource)
{
    vres_cls_t cls = resource->cls;

    if((VRES_CLS_MSG == cls) || (VRES_CLS_SEM == cls) || (VRES_CLS_TSK == cls))
        return true;
    else
        return false;
}


bool vres_is_sync(vres_op_t op)
{
    if (((op > VRES_SYNC_START) && (op < VRES_SYNC_END)) || (VRES_OP_DUMP == op) || (VRES_OP_RESTORE == op))
        return true;
    else
        return false;
}


bool vres_is_local(vres_op_t op)
{
    if ((op > VRES_LOCAL_START) && (op < VRES_LOCAL_END))
        return true;
    else
        return false;
}


void vres_set_entry(vres_entry_t *entry, vres_op_t op, vres_id_t id, vres_val_t val1, vres_val_t val2)
{
    entry[VRES_POS_OP] = (vres_entry_t)op;
    entry[VRES_POS_ID] = (vres_entry_t)id;
    entry[VRES_POS_VAL1] = (vres_entry_t)val1;
    entry[VRES_POS_VAL2] = (vres_entry_t)val2;
}


vres_op_t vres_get_op(vres_t *resource)
{
    return vres_entry_op(resource->entry);
}


vres_id_t vres_get_id(vres_t *resource)
{
    return vres_entry_id(resource->entry);
}


vres_flg_t vres_get_flags(vres_t *resource)
{
    return vres_entry_flags(resource->entry);
}


unsigned long vres_get_off(vres_t *resource)
{
    return vres_entry_off(resource->entry);
}


void vres_set_op(vres_t *resource, vres_op_t op)
{
    resource->entry[VRES_POS_OP] = op;
}


void vres_set_id(vres_t *resource, vres_id_t id)
{
    resource->entry[VRES_POS_ID] = id;
}


void vres_set_off(vres_t *resource, unsigned long off)
{
    resource->entry[VRES_POS_VAL1] = off;
}


void vres_set_flags(vres_t *resource, int flags)
{
    resource->entry[VRES_POS_VAL2] = flags | vres_get_flags(resource);
}


void vres_clear_flags(vres_t *resource, int flags)
{
    resource->entry[VRES_POS_VAL2] = vres_get_flags(resource) & ~flags;
}


bool vres_need_timed_lock(vres_t *resource)
{
    return VRES_OP_CANCEL == vres_get_op(resource);
}


bool vres_need_half_lock(vres_t *resource)
{
    return VRES_OP_SHMFAULT == vres_get_op(resource);
}


bool vres_need_reply(vres_t *resource)
{
    return !vres_need_half_lock(resource);
}


bool vres_need_lock(vres_t *resource)
{
    vres_op_t op = vres_get_op(resource);

    return !vres_is_sync(op);
}


bool vres_need_wrlock(vres_t *resource)
{
    vres_op_t op = vres_get_op(resource);

    return (VRES_OP_SHMCTL == op) || (VRES_OP_JOIN == op) || (VRES_OP_LEAVE == op);
}


int vres_index_to_err(vres_index_t index)
{
    return -index - VRES_ERRNO_MAX;
}


int vres_err_to_index(int err)
{
    return -err - VRES_ERRNO_MAX;
}


void vres_set_timeout(struct timespec *time, vres_time_t timeout)
{
    long nsec = (timeout % 1000000) * 1000;

    clock_gettime(CLOCK_REALTIME, time);
    time->tv_sec += timeout / 1000000;
    if (time->tv_nsec + nsec >= 1000000000) {
        time->tv_nsec += nsec - 1000000000;
        time->tv_sec += 1;
    } else
        time->tv_nsec += nsec;
}


vres_time_t vres_get_time()
{
    struct timespec time;

    clock_gettime(CLOCK_REALTIME, &time);
    return (long long)time.tv_sec * 1000000 + time.tv_nsec / 1000;
}


vres_time_t vres_get_time_offset(vres_time_t start, vres_time_t ref)
{
    return ref - start - (vres_get_time() - start) / 2;
}


int vres_memget(unsigned long addr, size_t size, char **buf)
{
    static bool init = false;
    static int desc = -1;

    if (!init) {
        desc = open("/dev/mem", O_RDWR);
        init = true;
    }
    if(desc < 0) {
        log_err("failed to open /dev/mem");
        return -1;
    }
    *buf = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, desc, addr);
    if (!buf || (MAP_FAILED == buf)) {
        log_err("failed to access /dev/mem");
        // close(desc);
        return -1;
    }
    return desc;
}


vres_reply_t *vres_reply_err(int err)
{
    vres_reply_t *reply = (vres_reply_t *)malloc(sizeof(vres_reply_t));

    if (reply)
        vres_set_err(reply, err);
    return reply;
}


vres_reply_t *vres_reply_alloc(size_t size)
{
    vres_reply_t *reply = (vres_reply_t *)calloc(1, sizeof(vres_reply_t) + size);

    if (reply)
        reply->length = size;
    return reply;
}


unsigned long vres_get_nr_queues(vres_cls_t cls)
{
    switch(cls) {
    case VRES_CLS_MSG:
        return 2;
    case VRES_CLS_SHM:
        return 0;
    default:
        return 1;
    }
}


void vres_probe(vres_t *resource)
{
    log("probe: id=%d, val1=%d, val2=%d\n", vres_get_id(resource), resource->entry[VRES_POS_VAL1], resource->entry[VRES_POS_VAL2]);
}
