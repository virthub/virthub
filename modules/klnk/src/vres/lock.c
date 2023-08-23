#include "lock.h"

int lock_stat = 0;
vres_lock_group_t lock_group[VRES_LOCK_GROUP_SIZE];

int vres_lock_compare(const void *val0, const void *val1)
{
    vres_lock_entry_t *ent0 = ((vres_lock_desc_t *)val0)->entry;
    vres_lock_entry_t *ent1 = ((vres_lock_desc_t *)val1)->entry;
    const size_t size = VRES_LOCK_ENTRY_SIZE * sizeof(vres_lock_entry_t);

    return memcmp(ent0, ent1, size);
}


void vres_lock_init()
{
    int i;

    if (lock_stat & VRES_STAT_INIT)
        return;
    for (i = 0; i < VRES_LOCK_GROUP_SIZE; i++) {
        pthread_mutex_init(&lock_group[i].mutex, NULL);
        rbtree_new(&lock_group[i].tree, vres_lock_compare);
    }
    lock_stat |= VRES_STAT_INIT;
}


static inline unsigned long vres_lock_hash(vres_lock_desc_t *desc)
{
    vres_lock_entry_t *ent = desc->entry;

    assert(VRES_LOCK_ENTRY_SIZE == 4);
    return (ent[0] ^ ent[1] ^ ent[2] ^ ent[3]) % VRES_LOCK_GROUP_SIZE;
}


static inline void vres_lock_get_desc(vres_t *resource, vres_lock_desc_t *desc)
{
    desc->entry[0] = resource->key;
    desc->entry[1] = resource->cls;
    if (vres_get_op(resource) == VRES_OP_SHMFAULT) {
        desc->entry[2] = resource->owner;
        desc->entry[3] = vres_get_region(resource);
    } else {
        desc->entry[2] = 0;
        desc->entry[3] = 0;
    }
}


static inline vres_lock_t *vres_lock_alloc(vres_lock_desc_t *desc)
{
    vres_lock_t *lock = (vres_lock_t *)malloc(sizeof(vres_lock_t));

    if (lock) {
        lock->count = 0;
        lock->grp = NULL;
        memcpy(&lock->desc, desc, sizeof(vres_lock_desc_t));
        pthread_mutex_init(&lock->mutex, NULL);
        pthread_cond_init(&lock->cond, NULL);
        INIT_LIST_HEAD(&lock->list);
    }
    return lock;
}


static inline vres_lock_t *vres_lock_lookup(vres_lock_group_t *group, vres_lock_desc_t *desc)
{
    rbtree_node_t *node = NULL;

    if (!rbtree_find(&group->tree, desc, &node))
        return tree_entry(node, vres_lock_t, node);
    else
        return NULL;
}


static inline void vres_lock_insert(vres_lock_group_t *group, vres_lock_t *lock)
{
    lock->grp = group;
    rbtree_insert(&group->tree, &lock->desc, &lock->node);
}


static inline vres_lock_t *vres_lock_get(vres_t *resource)
{
    vres_lock_desc_t desc;
    vres_lock_group_t *grp;
    vres_lock_t *lock = NULL;
    pthread_t tid = pthread_self();
    vres_lock_list_t *list = malloc(sizeof(vres_lock_list_t));

    if (!list) {
        log_resource_warning(resource, "no memory");
        return NULL;
    }
    list->tid = tid;
    vres_lock_get_desc(resource, &desc);
    grp = &lock_group[vres_lock_hash(&desc)];
    pthread_mutex_lock(&grp->mutex);
    lock = vres_lock_lookup(grp, &desc);
    if (!lock) {
        lock = vres_lock_alloc(&desc);
        if (!lock) {
            log_resource_warning(resource, "no memory");
            goto out;
        }
        pthread_mutex_lock(&lock->mutex);
        lock->count = 1;
        list_add_tail(&list->list, &lock->list);
        vres_lock_insert(grp, lock);
    } else {
        pthread_mutex_lock(&lock->mutex);
        lock->count++;
        list_add_tail(&list->list, &lock->list);
    }
out:
    pthread_mutex_unlock(&grp->mutex);
    return lock;
}


static inline vres_lock_t *vres_lock_get_first(vres_t *resource, bool *first)
{
    vres_lock_desc_t desc;
    vres_lock_group_t *grp;
    vres_lock_t *lock = NULL;
    pthread_t tid = pthread_self();

    assert(first);
    *first = false;
    vres_lock_get_desc(resource, &desc);
    grp = &lock_group[vres_lock_hash(&desc)];
    pthread_mutex_lock(&grp->mutex);
    lock = vres_lock_lookup(grp, &desc);
    if (!lock) {
        lock = vres_lock_alloc(&desc);
        if (!lock) {
            log_resource_warning(resource, "no memory");
            goto out;
        }
        vres_lock_insert(grp, lock);
        lock->count = 1;
    } else
        lock->count++;
    if (lock->count > 1)
        pthread_mutex_lock(&lock->mutex);
    else
        *first = true;
out:
    pthread_mutex_unlock(&grp->mutex);
    return lock;
}


static inline void vres_lock_free(vres_lock_group_t *group, vres_lock_t *lock)
{
    rbtree_remove(&group->tree, &lock->desc);
    pthread_mutex_destroy(&lock->mutex);
    pthread_cond_destroy(&lock->cond);
    free(lock);
}


vres_lock_t *vres_lock_top(vres_t *resource)
{
    if (!(lock_stat & VRES_STAT_INIT)) {
        log_warning("invalid state");
        return NULL;
    }
    log_lock_top(resource);
    return vres_lock_get(resource);
}


void vres_lock_remove(vres_lock_t *lock)
{
    vres_lock_list_t *list;
    pthread_t tid = pthread_self();
    vres_lock_group_t *grp = lock->grp;

    pthread_mutex_lock(&grp->mutex);
    list_for_each_entry(list, &lock->list, list) {
        if (list->tid == tid) {
            list_del(&list->list);
            lock->count--;
            free(list);
            break;
        }
    }
    if (!lock->count)
        vres_lock_free(grp, lock);
    pthread_mutex_unlock(&grp->mutex);
}


void vres_unlock_top(vres_lock_t *lock)
{
    pthread_mutex_unlock(&lock->mutex);
    vres_lock_remove(lock);
}


int vres_lock_buttom(vres_lock_t *lock)
{
    pthread_t tid = pthread_self();
    vres_lock_list_t *list = list_entry(lock->list.next, vres_lock_list_t, list);

    if (!(lock_stat & VRES_STAT_INIT)) {
        log_warning("invalid state");
        return -EINVAL;
    }
    assert(!list_empty(&lock->list));
    while (list->tid != tid) {
        pthread_cond_wait(&lock->cond, &lock->mutex);
        list = list_entry(lock->list.next, vres_lock_list_t, list);
    }
    pthread_mutex_unlock(&lock->mutex);
    return 0;
}


int vres_lock(vres_t *resource)
{
    int ret = 0;
    vres_lock_t *lock;
    bool first = false;

    if (!(lock_stat & VRES_STAT_INIT)) {
        log_warning("invalid state");
        return -EINVAL;
    }
    log_lock(resource);
    lock = vres_lock_get_first(resource, &first);
    if (!lock) {
        log_resource_warning(resource, "no entry");
        return -ENOENT;
    }
    if (!first) {
        pthread_cond_wait(&lock->cond, &lock->mutex);
        pthread_mutex_unlock(&lock->mutex);
    }
    return ret;
}


int vres_lock_timeout(vres_t *resource, vres_time_t timeout)
{
    int ret = 0;
    vres_lock_t *lock;
    bool first = false;

    if (!(lock_stat & VRES_STAT_INIT)) {
        log_warning("invalid state");
        return -EINVAL;
    }
    log_lock_timeout(resource);
    lock = vres_lock_get_first(resource, &first);
    if (!lock) {
        log_resource_warning(resource, "no entry");
        return -ENOENT;
    }
    if (!first) {
        struct timespec time;

        vres_set_timeout(&time, timeout);
        ret = pthread_cond_timedwait(&lock->cond, &lock->mutex, &time);
        if (!ret)
            pthread_mutex_unlock(&lock->mutex);
        else
            vres_lock_remove(lock);
    }
    return ret > 0 ? -ret : ret;
}


void vres_unlock(vres_t *resource, vres_lock_t *lock)
{
    bool signal = false;
    bool release = false;
    bool broadcast = false;
    vres_lock_group_t *grp = NULL;
    vres_lock_desc_t desc;

    if (!(lock_stat & VRES_STAT_INIT)) {
        log_warning("invalid state");
        return;
    }
    if (lock)
        grp = lock->grp;
    if (!grp) {
        vres_lock_get_desc(resource, &desc);
        grp = &lock_group[vres_lock_hash(&desc)];
    }
    pthread_mutex_lock(&grp->mutex);
    if (!lock) {
        lock = vres_lock_lookup(grp, &desc);
        if (!lock) {
            log_resource_warning(resource, "no lock");
            goto out;
        }
    }
    pthread_mutex_lock(&lock->mutex);
    if (lock->count > 0) {
        lock->count--;
        if (lock->count > 0) {
            if (list_empty(&lock->list))
                signal = true;
            else
                broadcast = true;
        } else
            release = true;
        if (!list_empty(&lock->list)) {
            pthread_t tid = pthread_self();
            vres_lock_list_t *list = list_entry(lock->list.next, vres_lock_list_t, list);

            if (tid == list->tid) {
                list_del(&list->list);
                free(list);
                if (release && !list_empty(&lock->list))
                    log_resource_err(resource, "failed to release");
            } else
                log_resource_err(resource, "invalid lock");
        }
    }
    if (signal)
        pthread_cond_signal(&lock->cond);
    else if (broadcast)
        pthread_cond_broadcast(&lock->cond);
    pthread_mutex_unlock(&lock->mutex);
    if (release) {
        vres_lock_free(grp, lock);
        lock = NULL;
    }
out:
    pthread_mutex_unlock(&grp->mutex);
    log_unlock(resource);
}
