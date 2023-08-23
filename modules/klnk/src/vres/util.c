#include "util.h"

char mds_addr[32];
vres_addr_t node_addr;
int nr_resource_managers = 0;
char mds_name[NODE_NAME_SIZE];
char node_name[NODE_NAME_SIZE];
vres_addr_t *resource_manager_list = NULL;

void vres_sleep(vres_time_t timeout)
{
    if (timeout > 0) {
        pthread_cond_t cond;
        struct timespec time;
        pthread_mutex_t mutex;

        pthread_mutex_init(&mutex, NULL);
        pthread_cond_init(&cond, NULL);
        vres_set_timeout(&time, timeout);
        pthread_mutex_lock(&mutex);
        pthread_cond_timedwait(&cond, &mutex, &time);
        pthread_mutex_unlock(&mutex);
    }
}


int vres_get_errno(vres_reply_t *reply)
{
    if (!reply)
        return 0;
    else if (vres_has_err(reply))
        return vres_get_err(reply);
    else
        return reply->length;
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


unsigned long vres_get_off_by_id(int slice_id, int chunk_id)
{
    return (slice_id << VRES_SLICE_SHIFT) + (chunk_id << VRES_CHUNK_SHIFT);
}


unsigned long vres_get_region_off(vres_t *resource) 
{
    return vres_get_off(resource) & VRES_REGION_MASK;
}


int vres_get_slice(vres_t *resource)
{
    return vres_get_region_off(resource) >> VRES_SLICE_SHIFT;
}


unsigned long vres_get_slice_start(vres_t *resource)
{
    return (vres_get_off(resource) >> VRES_SLICE_SHIFT) << VRES_SLICE_SHIFT;
}


unsigned long vres_get_slice_off(vres_t *resource)
{
    return vres_get_slice(resource) << VRES_SLICE_SHIFT;
}


int vres_get_chunk(vres_t *resource)
{
    return (vres_get_region_off(resource) >> VRES_CHUNK_SHIFT) & VRES_CHUNK_MASK;
}


unsigned long vres_get_chunk_start_by_id(vres_t *resource, int chunk_id)
{
    return vres_get_slice_start(resource) + (chunk_id << VRES_CHUNK_SHIFT);
}


unsigned long vres_get_chunk_off_by_id(vres_t *resource, int chunk_id)
{
    return vres_get_slice_off(resource) + (chunk_id << VRES_CHUNK_SHIFT);
}


unsigned long vres_get_chunk_off(vres_t *resource)
{
    return vres_get_chunk_off_by_id(resource, vres_get_chunk(resource));
}


unsigned long vres_get_chunk_start(vres_t *resource)
{
    return (vres_get_off(resource) >> VRES_CHUNK_SHIFT) << VRES_CHUNK_SHIFT;
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

    if ((VRES_CLS_MSG == cls) || (VRES_CLS_SEM == cls) || (VRES_CLS_TSK == cls))
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


void vres_set_op(vres_t *resource, vres_op_t op)
{
    resource->entry[VRES_POS_OP] = op;
}


void vres_set_id(vres_t *resource, vres_id_t id)
{
    resource->entry[VRES_POS_ID] = id;
}


void vres_set_val1(vres_t *resource, vres_val_t val)
{
    resource->entry[VRES_POS_VAL1] = val;
}


void vres_set_val2(vres_t *resource, vres_val_t val)
{
    resource->entry[VRES_POS_VAL2] = val;
}


void vres_set_off(vres_t *resource, unsigned long off)
{
    vres_set_val1(resource, off);
}


void vres_set_flags(vres_t *resource, int flags)
{
    vres_set_val2(resource, flags | vres_get_flags(resource));
}


void vres_clear_flags(vres_t *resource, int flags)
{
    vres_set_val2(resource, vres_get_flags(resource) & ~flags);
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


int vres_memget(void *addr, size_t size, char **buf)
{
    char *map_base;
    static int desc = -1;
    static bool init = false;

    assert(buf);
    if (!init) {
        desc = open("/dev/mem", O_RDWR);
        init = true;
    }
    if (desc < 0) {
        log_warning("failed to open /dev/mem");
        return -ENOENT;
    }
    map_base = mmap(0, size, PROT_WRITE | PROT_READ, MAP_SHARED, desc, (long int)addr);
    if (MAP_FAILED == map_base) {
        log_warning("failed to access /dev/mem");
        close(desc);
        return -EINVAL;
    }
    *buf = map_base;
    return 0;
}


void vres_memput(void *addr, size_t size)
{
    munmap(addr, size);
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
    vres_reply_t *reply = NULL;
    size_t sz = sizeof(vres_reply_t) + size;

    if (sz <= VRES_REPLY_MAX) {
        reply = (vres_reply_t *)calloc(1, sz);
        if (reply) {
            assert(size > 0);
            reply->length = size;
        }
    } else
        log_warning("failed to generate reply, invalid size, size=%zu", size);
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


bool vres_chkown(vres_t *resource, struct vres_region *region)
{
    vres_slice_t *slice = vres_region_get_slice(resource, region);

    return slice->owner == resource->owner;
}


vres_req_t *vres_alloc_request(vres_t *resource, char *buf, size_t size)
{
    size_t reqsz = vres_req_size(size);
    vres_req_t *req = (vres_req_t *)malloc(reqsz);

    if (!req) {
        log_warning("no memory");
        return NULL;
    }
    memset(req, 0, reqsz);
    req->resource = *resource;
    req->length = size;
    if (size > 0)
        memcpy(req->buf, buf, size);
    return req;
}
