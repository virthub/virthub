#include "prio.h"

int prio_stat = 0;
int prio_table[VRES_PRIO_MAX][VRES_PRIO_NR_INTERVALS];
vres_prio_lock_group_t prio_lock_group[VRES_PRIO_LOCK_GROUP_SIZE];

int vres_prio_lock_compare(const void *val0, const void *val1)
{
    vres_prio_lock_entry_t *ent0 = ((vres_prio_lock_desc_t *)val0)->entry;
    vres_prio_lock_entry_t *ent1 = ((vres_prio_lock_desc_t *)val1)->entry;
    size_t size = VRES_PRIO_LOCK_ENTRY_SIZE * sizeof(vres_prio_lock_entry_t);

    return memcmp(ent0, ent1, size);
}


static inline unsigned long vres_prio_lock_hash(vres_prio_lock_desc_t *desc)
{
    vres_prio_lock_entry_t *ent = desc->entry;

    assert(VRES_PRIO_LOCK_ENTRY_SIZE == 4);
    return (ent[0] ^ ent[1] ^ ent[2] ^ ent[3]) % VRES_PRIO_LOCK_GROUP_SIZE;
}


static inline void vres_prio_lock_get_desc(vres_t *resource, vres_prio_lock_desc_t *desc)
{
    desc->entry[0] = resource->key;
    desc->entry[1] = resource->cls;
    desc->entry[2] = resource->owner;
    if (vres_get_op(resource) == VRES_OP_SHMFAULT) {
        desc->entry[3] = vres_get_slice(resource); // Block-level priority control
    } else {
        desc->entry[3] = 0;
    }
}


static inline vres_prio_lock_t *vres_prio_lock_alloc(vres_prio_lock_desc_t *desc)
{
    vres_prio_lock_t *lock = (vres_prio_lock_t *)malloc(sizeof(vres_prio_lock_t));

    if (lock) {
        lock->count = 0;
        memcpy(&lock->desc, desc, sizeof(vres_prio_lock_desc_t));
        pthread_mutex_init(&lock->mutex, NULL);
        pthread_cond_init(&lock->cond, NULL);
    }
    return lock;
}


static inline vres_prio_lock_t *vres_prio_lock_lookup(vres_prio_lock_group_t *group, vres_prio_lock_desc_t *desc)
{
    rbtree_node_t *node = NULL;

    if (!rbtree_find(&group->tree, desc, &node))
        return tree_entry(node, vres_prio_lock_t, node);
    else
        return NULL;
}


static inline void vres_prio_lock_insert(vres_prio_lock_group_t *group, vres_prio_lock_t *lock)
{
    rbtree_insert(&group->tree, &lock->desc, &lock->node);
}


static inline vres_prio_lock_t *vres_prio_lock_get(vres_t *resource)
{
    vres_prio_lock_t *lock;
    vres_prio_lock_desc_t desc;
    vres_prio_lock_group_t *grp;

    vres_prio_lock_get_desc(resource, &desc);
    grp = &prio_lock_group[vres_prio_lock_hash(&desc)];
    pthread_mutex_lock(&grp->mutex);
    lock = vres_prio_lock_lookup(grp, &desc);
    if (!lock) {
        lock = vres_prio_lock_alloc(&desc);
        if (!lock)
            goto out;
        vres_prio_lock_insert(grp, lock);
    }
    pthread_mutex_lock(&lock->mutex);
    lock->count++;
out:
    pthread_mutex_unlock(&grp->mutex);
    return lock;
}


static void vres_prio_lock_free(vres_prio_lock_group_t *group, vres_prio_lock_t *lock)
{
    rbtree_remove(&group->tree, &lock->desc);
    pthread_mutex_destroy(&lock->mutex);
    pthread_cond_destroy(&lock->cond);
    free(lock);
}


int vres_prio_lock(vres_t *resource)
{
    vres_prio_lock_t *lock;

    if (!prio_stat) {
        log_resource_warning(resource, "invalid state");
        return -EINVAL;
    }
    lock = vres_prio_lock_get(resource);
    if (!lock)
        return -ENOENT;
    if (lock->count > 1)
        pthread_cond_wait(&lock->cond, &lock->mutex);
    pthread_mutex_unlock(&lock->mutex);
    return 0;
}


void vres_prio_unlock(vres_t *resource)
{
    bool release = false;
    vres_prio_lock_t *lock;
    vres_prio_lock_desc_t desc;
    vres_prio_lock_group_t *grp;

    if (!prio_stat) {
        log_resource_warning(resource, "invalid state");
        return;
    }
    vres_prio_lock_get_desc(resource, &desc);
    grp = &prio_lock_group[vres_prio_lock_hash(&desc)];
    pthread_mutex_lock(&grp->mutex);
    lock = vres_prio_lock_lookup(grp, &desc);
    if (!lock) {
        pthread_mutex_unlock(&grp->mutex);
        return;
    }
    pthread_mutex_lock(&lock->mutex);
    if (lock->count > 0) {
        lock->count--;
        if (lock->count > 0)
            pthread_cond_signal(&lock->cond);
        else
            release = true;
    }
    pthread_mutex_unlock(&lock->mutex);
    if (release)
        vres_prio_lock_free(grp, lock);
    pthread_mutex_unlock(&grp->mutex);
}


void vres_prio_gen()
{
    int i, j, k;
    int tmp[VRES_PRIO_MAX];

    srand(1);
    for (j = 0; j < VRES_PRIO_NR_INTERVALS; j++) {
        for (i = 0; i < VRES_PRIO_MAX; i++)
            tmp[i] = i + 1;
        for (i = 0; i < VRES_PRIO_MAX; i++) {
            int n = rand() % (VRES_PRIO_MAX - i);

            prio_table[i][j] = tmp[n];
            for (k = n; k < VRES_PRIO_MAX - i - 1; k++)
                tmp[k] = tmp[k + 1];
        }
    }
}


void vres_prio_init()
{
    int i;

    if (prio_stat & VRES_STAT_INIT)
        return;
    assert(VRES_PRIO_MAX >= VRES_MEMBER_MAX);
    for (i = 0; i < VRES_PRIO_LOCK_GROUP_SIZE; i++) {
        pthread_mutex_init(&prio_lock_group[i].mutex, NULL);
        rbtree_new(&prio_lock_group[i].tree, vres_prio_lock_compare);
    }
    vres_prio_gen();
    prio_stat |= VRES_STAT_INIT;
}


int vres_prio_do_create(vres_t *resource, vres_time_t off)
{
    int i;
    vres_prio_t *prio;
    vres_file_entry_t *entry;
    char path[VRES_PATH_MAX];

    for (i = 0; i < VRES_SLICE_MAX; i++) {
        vres_get_priority_path(resource, i, path);
        entry = vres_file_get_entry(path, sizeof(vres_prio_t), FILE_RDWR | FILE_CREAT);
        if (!entry) {
            log_resource_warning(resource, "no entry");
            return -ENOENT;
        }
        prio = vres_file_get_desc(entry, vres_prio_t);
        memset(prio, 0, sizeof(vres_prio_t));
        prio->t_off = off;
        prio->t_sync = vres_get_time();
        vres_file_put_entry(entry);
    }
    log_prio_create(resource, off);
    return 0;
}


int vres_prio_create(vres_t *resource, bool sync)
{
    int ret;
    vres_time_t off = 0;

#ifdef VRES_PRIO_SYNC_TIME
    if (sync) {
        vres_time_t time;
        vres_time_t start = vres_get_time();

        ret = vres_sync_request(resource, &time);
        if (ret) {
            log_resource_warning(resource, "failed to get time");
            return ret;
        }
        off = vres_get_time_offset(start, time);
    }
#endif
    ret = vres_prio_do_create(resource, off);
    if (ret) {
        log_resource_warning(resource, "failed to create");
        return ret;
    }
    return ret;
}


static int vres_prio_get_time(vres_t *resource, vres_time_t *time)
{
    vres_prio_t *prio;
    vres_file_entry_t *entry;
    char path[VRES_PATH_MAX];

    vres_get_priority_path(resource, vres_get_slice(resource), path);
    entry = vres_file_get_entry(path, sizeof(vres_prio_t), FILE_RDONLY);
    if (!entry) {
        log_resource_warning(resource, "no entry");
        return -ENOENT;
    }
    prio = vres_file_get_desc(entry, vres_prio_t);
    *time = vres_prio_time(prio, vres_get_time());
    vres_file_put_entry(entry);
    return 0;
}


static int vres_prio_get_waittime(vres_t *resource, vres_time_t *waittime)
{
    vres_time_t time;

    if (vres_prio_get_time(resource, &time)) {
        log_resource_warning(resource, "failed to get time");
        return -EINVAL;
    }
    *waittime = VRES_PRIO_PERIOD - (time % VRES_PRIO_PERIOD);
    return 0;
}


static int vres_prio_convert_from_time(vres_t *resource, vres_id_t id, vres_time_t time)
{
#if (MANAGER_TYPE == AREA_MANAGER) && defined(ENABLE_DYNAMIC_OWNER)
    int pos = vres_member_get_pos(resource, id);
#else
    int pos = id % VRES_PRIO_MAX;
#endif

    if (pos < 0) {
        log_resource_warning(resource, "cannot find id %d", id);
        pos = 0;
    }
    return prio_table[pos][vres_prio_interval(time)];
}


static bool vres_prio_is_expired(vres_prio_t *prio)
{
    vres_time_t curr = vres_prio_time(prio, vres_get_time());
    vres_time_t prev = vres_prio_time(prio, prio->t_update);

    return abs(curr - prev) > VRES_PRIO_PERIOD;
}


static int vres_prio_compare(vres_t *resource, vres_id_t dest, vres_id_t src)
{
    int i, j;
    vres_time_t time;

    if (!(prio_stat & VRES_STAT_INIT))
        return -EINVAL;
    if (vres_prio_get_time(resource, &time)) {
        log_resource_warning(resource, "failed to get time");
        return -EINVAL;
    }
    i = vres_prio_convert_from_time(resource, dest, time);
    if (i < 0) {
        log_resource_warning(resource, "failed to get priority");
        return -EINVAL;
    }
    j = vres_prio_convert_from_time(resource, src, time);
    if (j < 0) {
        log_resource_warning(resource, "failed to get priority");
        return -EINVAL;
    }
    if (i > j)
        return 1;
    else if (i == j)
        return 0;
    else
        return -1;
}


int vres_prio_get(vres_t *resource, vres_prio_t *prio)
{
    vres_prio_t *ptr;
    vres_file_entry_t *entry;
    char path[VRES_PATH_MAX];

    if (!(prio_stat & VRES_STAT_INIT))
        return -EINVAL;
    vres_get_priority_path(resource, vres_get_slice(resource), path);
    entry = vres_file_get_entry(path, sizeof(vres_prio_t), FILE_RDONLY);
    if (!entry) {
        log_resource_warning(resource, "no entry");
        return -ENOENT;
    }
    ptr = vres_file_get_desc(entry, vres_prio_t);
    memcpy(prio, ptr, sizeof(vres_prio_t));
    vres_file_put_entry(entry);
    return 0;
}


int vres_prio_set(vres_t *resource, vres_prio_t *prio)
{
    vres_prio_t *ptr;
    vres_file_entry_t *entry;
    char path[VRES_PATH_MAX];

    if (!(prio_stat & VRES_STAT_INIT))
        return -EINVAL;
    vres_get_priority_path(resource, vres_get_slice(resource), path);
    entry = vres_file_get_entry(path, sizeof(vres_prio_t), FILE_RDWR);
    if (!entry) {
        log_resource_warning(resource, "no entry");
        return -ENOENT;
    }
    ptr = vres_file_get_desc(entry, vres_prio_t);
    memcpy(ptr, prio, sizeof(vres_prio_t));
    vres_file_put_entry(entry);
    return 0;
}


int vres_prio_set_busy(vres_t *resource)
{
    int ret = 0;
    vres_file_entry_t *entry;
    char path[VRES_PATH_MAX];

    if (!(prio_stat & VRES_STAT_INIT))
        return -EINVAL;
    log_prio_set_busy(resource);
    vres_get_priority_path(resource, vres_get_slice(resource), path);
    vres_prio_lock(resource);
    entry = vres_file_get_entry(path, sizeof(vres_prio_t), FILE_RDWR);
    if (!entry) {
        log_resource_warning(resource, "no entry");
        ret = -ENOENT;
    } else {
        vres_prio_t *prio;

        prio = vres_file_get_desc(entry, vres_prio_t);
        prio->id = vres_get_id(resource);
        prio->t_update = vres_get_time();
        vres_file_put_entry(entry);
    }
    vres_prio_unlock(resource);
    return ret;
}


int vres_prio_set_idle(vres_t *resource)
{
    int ret = 0;
    vres_file_entry_t *entry;
    char path[VRES_PATH_MAX];

    if (!(prio_stat & VRES_STAT_INIT))
        return -EINVAL;
    vres_get_priority_path(resource, vres_get_slice(resource), path);
    vres_prio_lock(resource);
    entry = vres_file_get_entry(path, sizeof(vres_prio_t), FILE_RDWR);
    if (!entry) {
        log_resource_warning(resource, "no entry");
        ret = -ENOENT;
    } else {
        vres_prio_t *prio;
        vres_id_t id = vres_get_id(resource);

        prio = vres_file_get_desc(entry, vres_prio_t);
        if (!vres_prio_is_expired(prio) && (id == prio->id))
            prio->t_update = vres_get_time();
        vres_file_put_entry(entry);
    }
    vres_prio_unlock(resource);
    log_prio_set_idle(resource);
    return ret;
}


static inline void vres_prio_wait(vres_time_t timeout)
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


void *vres_prio_select(void *arg)
{
    int ret = 0;
    bool has_rec;
    char path[VRES_PATH_MAX];
    vres_t *resource = (vres_t *)arg;

    vres_get_checker_path(resource, vres_get_slice(resource), path);
    do {
        vres_index_t index;

        vres_prio_lock(resource);
        has_rec = !vres_record_head(path, &index);
        if (has_rec) {
            int i;
            int val;
            int curr;
            vres_id_t id;
            vres_time_t time;
            vres_prio_t prio;
            vres_record_t record;
            vres_id_t target = -1;
            vres_index_t pos = index;
            
            if (!ret) {
                ret = vres_prio_get_time(resource, &time);
                if (!ret) {
                    ret = vres_prio_get(resource, &prio);
                    if (ret) {
                        log_resource_err(resource, "failed to get priority");
                        break;
                    }
                    if (vres_prio_is_expired(&prio))
                        curr = -1;
                    else
                        curr = vres_prio_convert_from_time(resource, prio.id, time);
                    do {
                        ret = vres_record_get(path, index, &record);
                        if (ret) {
                            log_resource_warning(resource, "cannot get any record");
                            break;
                        }
                        id = vres_get_id(&record.req->resource);
                        vres_record_put(&record);
                        val = vres_prio_convert_from_time(resource, id, time);
                        if (val < 0) {
                            log_resource_err(resource, "failed to get priority");
                            ret = -EINVAL;
                            break;
                        }
                        if ((val > curr) || (val == curr && target == -1)) {
                            curr = val;
                            pos = index;
                            target = id;
                        }
                    } while (!vres_record_next(path, &index));
                    if (!ret && (curr < 0)) {
                        log_resource_warning(resource, "no available record");
                        ret = -ENOENT;
                    }
                } else
                    log_resource_err(resource, "failed to get time");
            }

            if (!ret && target >= 0) {
                prio.id = target;
                prio.t_update = vres_get_time();
                vres_prio_set(resource, &prio);
                do {
                    vres_t *pres;
                    vres_req_t *req;
                    bool remove = false;

                    ret = vres_record_get(path, pos, &record);
                    if (ret) {
                        log_resource_warning(resource, "cannot get any record");
                        break;
                    }
                    req = record.req;
                    pres = &req->resource;
                    id = vres_get_id(pres);
                    if (id == target) {
                        vres_shm_req_t *shm_req = (vres_shm_req_t *)req->buf;
                        bool need_lock = shm_req->cmd != VRES_SHM_PROPOSE;
                        vres_reply_t *reply = NULL;

                        vres_prio_unlock(resource);
                        log_prio_select(pres, pos);
                        if (need_lock) {
                            vres_lock_t *lock;

                            assert(vres_need_half_lock(pres));
                            lock = vres_lock_top(pres);
                            if (lock) {
                                ret = vres_lock_buttom(lock);
                                if (!ret)
                                    reply = vres_proc(req, VRES_PRIO);
                                else
                                    log_resource_err(resource, "failed to lock");
                                vres_unlock(pres, lock);
                            } else {
                                log_resource_err(resource, "failed to get lock");
                                ret = -ENOENT;
                            }
                        } else
                            reply = vres_proc(req, VRES_PRIO);
                        vres_prio_lock(resource);
                        if (!ret) {
                            if (reply && vres_has_err(reply)) {
                                if (vres_get_err(reply) != -EAGAIN) {
                                    log_resource_err(resource, "failed to handle request");
                                    ret = -EINVAL;
                                }
                            } else
                                remove = true;
                            if (reply)
                                free(reply);
                        }
                    }
                    vres_record_put(&record);
                    if (remove)
                        vres_record_remove(path, pos);
                } while (!vres_record_next(path, &pos));
            }
            if (!ret)
                has_rec = !vres_record_head(path, &index);
        }
        vres_prio_unlock(resource);
        if (has_rec && !ret)
            vres_prio_wait(VRES_PRIO_WAITTIME);
    } while (has_rec && !ret);
    free(arg);
    return NULL;
}


int vres_prio_poll(vres_req_t *req)
{
    int ret;
    vres_index_t start;
    vres_index_t index;
    char path[VRES_PATH_MAX];
    vres_t *resource = &req->resource;

    vres_get_checker_path(resource, vres_get_slice(resource), path);
    ret = vres_record_save(path, req, &index);
    if (ret) {
        log_resource_warning(resource, "failed to save record");
        return ret;
    }
    if (!vres_record_head(path, &start)) {
        if (start == index) {
            vres_t *arg;
            pthread_t tid;
            pthread_attr_t attr;

            arg = (vres_t *)malloc(sizeof(vres_t));
            if (!arg) {
                log_resource_warning(resource, "no memory");
                return -ENOMEM;
            }
            memcpy(arg, resource, sizeof(vres_t));
            pthread_attr_init(&attr);
            pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
            pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
            ret = pthread_create(&tid, &attr, vres_prio_select, arg);
            pthread_attr_destroy(&attr);
            if (ret) {
                log_resource_warning(resource, "failed to create thread");
                free(arg);
                return ret;
            }
        }
    }
    return 0;
}


int vres_prio_check(vres_req_t *req, int flags)
{
    int ret;
    vres_prio_t prio;
    vres_t *resource = &req->resource;

    log_prio_check(resource, ">-- prio_check@start --<");
    vres_prio_lock(resource);
    ret = vres_prio_get(resource, &prio);
    if (ret) {
        log_resource_warning(resource, "failed to get priority");
        goto out;
    }
    if (!vres_prio_is_expired(&prio)) {
        if (vres_prio_compare(resource, vres_get_id(resource), prio.id) >= 0)
            goto out;
    } else
        goto out;
    if (!(flags & VRES_PRIO)) {
        ret = vres_prio_poll(req);
        if (ret)
            goto out;
    }
    vres_prio_unlock(resource);
    log_prio_check(resource, ">-- prio_check@end (retry) --<");
    return -EAGAIN;
out:
    vres_prio_unlock(resource);
    log_prio_check(resource, ">-- prio_check@end (ret=%s) --<", log_get_err(ret));
    return ret;
}


int vres_prio_sync_time(vres_t *resource)
{
    int ret;
    vres_prio_t prio;
    vres_time_t start;

    vres_prio_lock(resource);
    ret = vres_prio_get(resource, &prio);
    if (ret) {
        log_resource_warning(resource, "failed to get priority");
        goto out;
    }
    start = vres_get_time();
    if (start - prio.t_sync > VRES_PRIO_SYNC_INTV) {
        vres_time_t time;

        ret = vres_sync_request(resource, &time);
        if (ret) {
            log_resource_warning(resource, "failed to send request");
            goto out;
        }
        prio.t_sync = start;
        prio.t_off = vres_get_time_offset(start, time);
        ret = vres_prio_set(resource, &prio);
        if (ret) {
            log_resource_warning(resource, "failed to save priority");
            goto out;
        }
        log_prio_sync_time(resource, prio.t_off);
    }
out:
    vres_prio_unlock(resource);
    return ret;
}
