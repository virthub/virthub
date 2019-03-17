/* prio.c
 *
 * Copyright (C) 2019 Yi-Wei Ci
 *
 * Distributed under the terms of the MIT license.
 */

#include "prio.h"

int prio_stat = 0;
int prio_table[VRES_MEMBER_MAX][VRES_PRIO_NR_INTERVALS];
vres_prio_lock_group_t prio_lock_group[VRES_PRIO_LOCK_GROUP_SIZE];

#ifdef CHECK_LIVE_TIME
void vres_prio_gen()
{
    int i, j, k;

    srand(1);
    for (j = 0; j < VRES_PRIO_NR_INTERVALS; j++) {
        for (i = 0; i < VRES_MEMBER_MAX; i++) {
            int tmp;

            for (;;) {
                tmp = rand();
                for (k = 0; k < i; k++) {
                    if (prio_table[k][j] == tmp)
                        break;
                }
                if (k == i)
                    break;
            }
            prio_table[i][j] = tmp;
        }
    }
}
#else
void vres_prio_gen()
{
    int i, j, k;
    int tmp[VRES_MEMBER_MAX];

    srand(1);
    for (j = 0; j < VRES_PRIO_NR_INTERVALS; j++) {
        for (i = 0; i < VRES_MEMBER_MAX; i++)
            tmp[i] = i + 1;
        for (i = 0; i < VRES_MEMBER_MAX; i++) {
            int n = rand() % (VRES_MEMBER_MAX - i);

            prio_table[i][j] = tmp[n];
            for (k = n; k < VRES_MEMBER_MAX - i - 1; k++)
                tmp[k] = tmp[k + 1];
        }
    }
}
#endif

void vres_prio_init()
{
    int i;

    if (prio_stat & VRES_STAT_INIT)
        return;

    vres_prio_gen();
    for (i = 0; i < VRES_PRIO_LOCK_GROUP_SIZE; i++) {
        pthread_mutex_init(&prio_lock_group[i].mutex, NULL);
        prio_lock_group[i].head = rbtree_create();
    }
    prio_stat |= VRES_STAT_INIT;
}


int vres_prio_create(vres_t *resource, vres_time_t off)
{
    vres_prio_t *prio;
    char path[VRES_PATH_MAX];

    vres_get_priority_path(resource, path);
    vres_file_entry_t *entry = vres_file_get_entry(path, sizeof(vres_prio_t), FILE_RDWR | FILE_CREAT);
    if (!entry) {
        log_resource_err(resource, "no entry");
        return -ENOENT;
    }

    prio = vres_file_get_desc(entry, vres_prio_t);
    memset(prio, 0, sizeof(vres_prio_t));
    prio->t_off = off;
    prio->t_sync = vres_get_time();
    vres_file_put_entry(entry);
    log_prio_create(resource, off);
    return 0;
}


static inline unsigned long vres_prio_lock_hash(vres_prio_lock_desc_t *desc)
{
    unsigned long *ent = desc->entry;

    return (ent[0] ^ ent[1] ^ ent[2]) % VRES_PRIO_LOCK_GROUP_SIZE;
}


static inline int vres_prio_lock_compare(char *l1, char *l2)
{
    vres_prio_lock_desc_t *desc1 = (vres_prio_lock_desc_t *)l1;
    vres_prio_lock_desc_t *desc2 = (vres_prio_lock_desc_t *)l2;

    return memcmp(desc1->entry, desc2->entry, sizeof(((vres_prio_lock_desc_t *)0)->entry));
}


static inline void vres_prio_lock_get_desc(vres_t *resource, vres_prio_lock_desc_t *desc)
{
    desc->entry[0] = resource->key;
    desc->entry[1] = resource->cls;
    desc->entry[2] = resource->owner;
}


static inline vres_prio_lock_t *vres_prio_lock_alloc(vres_prio_lock_desc_t *desc)
{
    vres_prio_lock_t *lock = (vres_prio_lock_t *)malloc(sizeof(vres_prio_lock_t));

    if (!lock)
        return NULL;

    lock->count = 0;
    memcpy(&lock->desc, desc, sizeof(vres_prio_lock_desc_t));
    pthread_mutex_init(&lock->mutex, NULL);
    pthread_cond_init(&lock->cond, NULL);
    return lock;
}


static inline vres_prio_lock_t *vres_prio_lock_lookup(vres_prio_lock_group_t *group, vres_prio_lock_desc_t *desc)
{
    return rbtree_lookup(group->head, (void *)desc, vres_prio_lock_compare);
}


static inline void vres_prio_lock_insert(vres_prio_lock_group_t *group, vres_prio_lock_t *lock)
{
    rbtree_insert(group->head, (void *)&lock->desc, (void *)lock, vres_prio_lock_compare);
}


static inline vres_prio_lock_t *vres_prio_lock_get(vres_t *resource)
{
    vres_prio_lock_desc_t desc;
    vres_prio_lock_group_t *grp;
    vres_prio_lock_t *lock = NULL;

    vres_prio_lock_get_desc(resource, &desc);
    grp = &prio_lock_group[vres_prio_lock_hash(&desc)];
    pthread_mutex_lock(&grp->mutex);
    lock = vres_prio_lock_lookup(grp, &desc);
    if (!lock) {
        lock = vres_prio_lock_alloc(&desc);
        if (!lock) {
            log_resource_err(resource, "no memory");
            goto out;
        }
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
    rbtree_delete(group->head, (void *)&lock->desc, vres_prio_lock_compare);
    pthread_mutex_destroy(&lock->mutex);
    pthread_cond_destroy(&lock->cond);
    free(lock);
}


static int vres_prio_lock(vres_t *resource)
{
    vres_prio_lock_t *lock;

    if (!(prio_stat & VRES_STAT_INIT))
        return -EINVAL;

    lock = vres_prio_lock_get(resource);
    if (!lock) {
        log_resource_err(resource, "no entry");
        return -ENOENT;
    }

    if (lock->count > 1)
        pthread_cond_wait(&lock->cond, &lock->mutex);
    pthread_mutex_unlock(&lock->mutex);
    log_prio_lock(resource);
    return 0;
}


static void vres_prio_unlock(vres_t *resource)
{
    int empty = 0;
    vres_prio_lock_t *lock;
    vres_prio_lock_desc_t desc;
    vres_prio_lock_group_t *grp;

    if (!(prio_stat & VRES_STAT_INIT))
        return;

    vres_prio_lock_get_desc(resource, &desc);
    grp = &prio_lock_group[vres_prio_lock_hash(&desc)];
    pthread_mutex_lock(&grp->mutex);
    lock = vres_prio_lock_lookup(grp, &desc);
    if (!lock) {
        log_resource_err(resource, "cannot find the lock");
        pthread_mutex_unlock(&grp->mutex);
        return;
    }

    pthread_mutex_lock(&lock->mutex);
    if (lock->count > 0) {
        lock->count--;
        if (lock->count > 0)
            pthread_cond_signal(&lock->cond);
        else
            empty = 1;
    }
    pthread_mutex_unlock(&lock->mutex);

    if (empty)
        vres_prio_lock_free(grp, lock);
    pthread_mutex_unlock(&grp->mutex);
    log_prio_unlock(resource);
}


static int vres_prio_get_time(vres_t *resource, vres_time_t *time)
{
    vres_prio_t *prio;
    vres_file_entry_t *entry;
    char path[VRES_PATH_MAX];

    vres_get_priority_path(resource, path);
    entry = vres_file_get_entry(path, sizeof(vres_prio_t), FILE_RDONLY);
    if (!entry) {
        log_resource_err(resource, "no entry");
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
        log_resource_err(resource, "failed to get time");
        return -EINVAL;
    }

    *waittime = VRES_PRIO_PERIOD - time % VRES_PRIO_PERIOD;
    return 0;
}


static int vres_prio_convert_from_time(vres_t *resource, vres_id_t id, vres_time_t time)
{
    int pos = vres_member_get_pos(resource, id);

    if (pos < 0) {
        log_resource_err(resource, "invalid id");
        return -EINVAL;
    }

    return prio_table[pos][vres_prio_interval(time)];
}


#ifdef CHECK_LIVE_TIME
static int vres_prio_is_expired(vres_prio_t *prio)
{
    return prio->t_update + prio->t_live < vres_get_time();
}
#else
static int vres_prio_is_expired(vres_prio_t *prio)
{
    vres_time_t curr = vres_prio_time(prio, vres_get_time());
    vres_time_t prev = vres_prio_time(prio, prio->t_update);

    return curr - prev > VRES_PRIO_PERIOD;
}


static int vres_prio_compare(vres_t *resource, vres_id_t dest, vres_id_t src)
{
    int i, j;
    vres_time_t time;

    if (!(prio_stat & VRES_STAT_INIT))
        return -EINVAL;

    if (vres_prio_get_time(resource, &time)) {
        log_err(resource, "failed to get time");
        return -EINVAL;
    }

    i = vres_prio_convert_from_time(resource, dest, time);
    if (i < 0) {
        log_err(resource, "failed to get priority");
        return -EINVAL;
    }

    j = vres_prio_convert_from_time(resource, src, time);
    if (j < 0) {
        log_err(resource, "failed to get priority");
        return -EINVAL;
    }

    if (i > j)
        return 1;
    else if (i == j)
        return 0;
    else
        return -1;
}
#endif


int vres_prio_get(vres_t *resource, vres_prio_t *prio)
{
    vres_prio_t *ptr;
    vres_file_entry_t *entry;
    char path[VRES_PATH_MAX];

    if (!(prio_stat & VRES_STAT_INIT))
        return -EINVAL;

    vres_get_priority_path(resource, path);
    entry = vres_file_get_entry(path, sizeof(vres_prio_t), FILE_RDONLY);
    if (!entry) {
        log_resource_err(resource, "no entry");
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

    vres_get_priority_path(resource, path);
    entry = vres_file_get_entry(path, sizeof(vres_prio_t), FILE_RDWR);
    if (!entry) {
        log_resource_err(resource, "no entry");
        return -ENOENT;
    }

    ptr = vres_file_get_desc(entry, vres_prio_t);
    memcpy(ptr, prio, sizeof(vres_prio_t));
    vres_file_put_entry(entry);
    return 0;
}


#ifdef CHECK_LIVE_TIME
int vres_prio_check_live_time(vres_t *resource, int *prio, vres_time_t *live)
{
    int ret;
    vres_time_t curr;
    vres_id_t id = vres_get_id(resource);

    if (!(prio_stat & VRES_STAT_INIT))
        return -EINVAL;

    vres_prio_lock(resource);
    ret = vres_prio_get_time(resource, &curr);
    if (ret < 0) {
        log_resource_err(resource, "failed to get time");
        goto unlock;
    }

    ret = vres_prio_convert_from_time(resource, id, curr);
    if (ret < 0) {
        log_resource_err(resource, "failed to get priority");
        goto unlock;
    }

    *live = VRES_PRIO_PERIOD - curr % VRES_PRIO_PERIOD;
    *prio = ret;
    ret = 0;
unlock:
    vres_prio_unlock(resource);
    return ret;
}


int vres_prio_set_busy(vres_t *resource)
{
    return 0;
}


int vres_prio_set_idle(vres_t *resource)
{
    return 0;
}


static int vres_prio_update(vres_t *resource, int val, vres_time_t live)
{
    vres_prio_t *prio;
    vres_file_entry_t *entry;
    char path[VRES_PATH_MAX];

    vres_get_priority_path(resource, path);
    entry = vres_file_get_entry(path, sizeof(vres_prio_t), FILE_RDWR);
    if (!entry) {
        log_resource_err(resource, "no entry");
        return -ENOENT;
    }

    prio = vres_file_get_desc(entry, vres_prio_t);
    prio->val = val;
    prio->t_live = live;
    prio->t_update = vres_get_time();
    vres_file_put_entry(entry);
    return 0;
}
#else
int vres_prio_set_busy(vres_t *resource)
{
    int ret = 0;
    vres_prio_t *prio;
    vres_file_entry_t *entry;
    char path[VRES_PATH_MAX];

    if (!(prio_stat & VRES_STAT_INIT))
        return -EINVAL;

#ifdef VRES_PRIO_UPDATE_CONTROL
    if (vres_get_flags(resource) & VRES_REDO)
        return 0;
#endif
    vres_get_priority_path(resource, path);
    vres_prio_lock(resource);
    entry = vres_file_get_entry(path, sizeof(vres_prio_t), FILE_RDWR);
    if (!entry) {
        log_err(resource, "no entry");
        ret = -ENOENT;
    } else {
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
    vres_prio_t *prio;
    vres_file_entry_t *entry;
    char path[VRES_PATH_MAX];

    if (!(prio_stat & VRES_STAT_INIT))
        return -EINVAL;

#ifdef VRES_PRIO_UPDATE_CONTROL
    if (vres_get_flags(resource) & VRES_REDO)
        return 0;
#endif
    vres_get_priority_path(resource, path);
    vres_prio_lock(resource);
    entry = vres_file_get_entry(path, sizeof(vres_prio_t), FILE_RDWR);
    if (!entry) {
        log_err(resource, "no entry");
        ret = -ENOENT;
    } else {
        vres_id_t id = vres_get_id(resource);
        prio = vres_file_get_desc(entry, vres_prio_t);
        if (!vres_prio_is_expired(prio) && (id == prio->id)) {
            prio->id = id;
            prio->t_update = vres_get_time();
        }
        vres_file_put_entry(entry);
    }
    vres_prio_unlock(resource);
    return ret;
}
#endif

static inline void vres_prio_wait(vres_time_t timeout)
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


void *vres_prio_select(void *arg)
{
    vres_index_t index;
    char path[VRES_PATH_MAX];
    vres_t *resource = (vres_t *)arg;

    vres_get_path(resource, path);
    vres_prio_lock(resource);

    while (!vres_record_first(path, &index)) {
        int ret;
        vres_id_t id;
        vres_time_t time;
        vres_prio_t prio;
        vres_id_t user = 0;
        vres_record_t record;
        vres_index_t pos = index;
#if !defined(CHECK_LIVE_TIME) && !defined(VRES_PRIO_NOWAIT)
        int curr = -1;
        int val;
#endif
retry:
        ret = vres_prio_get(resource, &prio);
        if (ret) {
            log_resource_err(resource, "failed to get priority");
            goto unlock;
        }

        if (!vres_prio_is_expired(&prio)) {
            ret = vres_prio_get_waittime(resource, &time);
            if (ret) {
                log_resource_err(resource, "failed to get wait time");
                goto unlock;
            }
            vres_prio_unlock(resource);
            vres_prio_wait(time);
            vres_prio_lock(resource);
            goto retry;
        }
#if !defined(CHECK_LIVE_TIME) && !defined(VRES_PRIO_NOWAIT)
        ret = vres_prio_get_time(resource, &time);
        if (ret) {
            log_err(resource, "failed to get time");
            goto unlock;
        }

        do {
            ret = vres_record_get(path, index, &record);
            if (ret) {
                log_err(resource, "failed to get record");
                goto unlock;
            }
            id = vres_get_id(&record.req->resource);
            vres_record_put(&record);
            val = vres_prio_convert_from_time(resource, id, time);
            if (val < 0) {
                log_err(resource, "failed to get priority");
                goto unlock;
            }
            if (val > curr) {
                user = id;
                curr = val;
                pos = index;
            }
        } while (!vres_record_next(path, &index));

        if (curr < 0) {
            log_err(resource, "no record");
            goto unlock;
        }
#endif
        do {
            ret = vres_record_get(path, pos, &record);
            if (ret) {
                log_resource_err(resource, "failed to get record");
                goto unlock;
            }
            id = vres_get_id(&record.req->resource);
            if ((id == user) || !user) {
                vres_prio_unlock(resource);
                vres_proc(record.req, VRES_SELECT);
                vres_prio_lock(resource);
            }
            vres_record_put(&record);
            if ((id == user) || !user) {
                vres_record_remove(path, pos);
#if !defined(CHECK_LIVE_TIME) && !defined(VRES_PRIO_NOWAIT)
                ret = vres_prio_get(resource, &prio);
                if (ret) {
                    log_err(resource, "failed to get priority");
                    goto unlock;
                }
                if (vres_prio_is_expired(&prio) || (prio.id != user))
                    break;
#endif
            }
        } while (!vres_record_next(path, &pos));
    }
unlock:
    vres_prio_unlock(resource);
    free(arg);
    return NULL;
}


void vres_prio_clear(vres_req_t *req)
{
    vres_clear_flags(&req->resource, VRES_REDO);
}


#ifdef CHECK_LIVE_TIME
int vres_prio_extract(vres_req_t *req, int *val, vres_time_t *live)
{
    int ret = -EINVAL;
    vres_t *resource = &req->resource;
    vres_op_t op = vres_get_op(resource);

    switch (op) {
    case VRES_OP_SHMFAULT:
    {
        vres_shmfault_arg_t *arg = (vres_shmfault_arg_t *)req->buf;

        *val = arg->prio;
        *live = arg->live;
        return 0;
    }
    default:
        break;
    }
    return ret;
}
#endif


int vres_prio_check(vres_req_t *req)
{
    int ret = 0;
    int empty = 0;
    int expire = 0;
    vres_prio_t prio;
    vres_index_t index;
    char path[VRES_PATH_MAX];
    vres_t *resource = &req->resource;
#ifdef CHECK_LIVE_TIME
    vres_time_t live;
    int val;
#endif

    vres_prio_lock(resource);
    ret = vres_prio_get(resource, &prio);
    if (ret) {
        log_resource_err(resource, "failed to get priority");
        goto out;
    }
#ifdef CHECK_LIVE_TIME
    ret = vres_prio_extract(req, &val, &live);
    if (ret) {
        log_resource_err(resource, "failed to extract priority");
        goto out;
    }
#endif
    expire = vres_prio_is_expired(&prio);
    if (!expire) {
#ifdef CHECK_LIVE_TIME
        if (val >= prio.val)
            goto out;
#else
        vres_id_t id = vres_get_id(resource);

        if (vres_prio_compare(resource, id, prio.id) >= 0)
            goto out;
#endif
    }

    vres_get_path(resource, path);
    empty = vres_record_is_empty(path);
    if (empty < 0) {
        log_resource_err(resource, "failed to check record");
        ret = -EINVAL;
        goto out;
    }

    if (expire && empty)
        goto out;

    ret = vres_record_save(path, req, &index);
    if (ret) {
        log_resource_err(resource, "failed to save record");
        goto out;
    }

    if (empty) {
        vres_t *arg;
        pthread_t tid;
        pthread_attr_t attr;

        arg = (vres_t *)malloc(sizeof(vres_t));
        if (!arg) {
            log_resource_err(resource, "no memory");
            ret = -ENOMEM;
            goto out;
        }

        memcpy(arg, resource, sizeof(vres_t));
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
        ret = pthread_create(&tid, &attr, vres_prio_select, arg);
        pthread_attr_destroy(&attr);
        if (ret) {
            log_resource_err(resource, "failed to create thread");
            free(arg);
            goto out;
        }
    }
    vres_prio_unlock(resource);
    log_prio_check(req);
    return -EAGAIN;
out:
#ifdef CHECK_LIVE_TIME
    if (!ret)
        ret = vres_prio_update(resource, val, live);
#endif
    vres_prio_unlock(resource);
    log_prio_check(req);
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
        log_resource_err(resource, "failed to get priority");
        goto out;
    }

    start = vres_get_time();
    if (start - prio.t_sync > VRES_PRIO_SYNC_INTERVAL) {
        vres_time_t time;

        ret = vres_sync_request(resource, &time);
        if (ret) {
            log_resource_err(resource, "failed to send request");
            goto out;
        }

        prio.t_sync = start;
        prio.t_off = vres_get_time_offset(start, time);
        ret = vres_prio_set(resource, &prio);
        if (ret) {
            log_resource_err(resource, "failed to save priority");
            goto out;
        }
        log_prio_sync_time(resource, prio.t_off);
    }
out:
    vres_prio_unlock(resource);
    return ret;
}
