#include "barrier.h"

int barrier_stat = 0;
klnk_barrier_group_t barrier_group[KLNK_BARRIER_GROUP_SIZE];

int klnk_barrier_compare(const void *val0, const void *val1)
{
    klnk_barrier_entry_t *ent0 = ((klnk_barrier_desc_t *)val0)->entry;
    klnk_barrier_entry_t *ent1 = ((klnk_barrier_desc_t *)val1)->entry;
    const size_t size = sizeof(klnk_barrier_entry_t) * KLNK_BARRIER_ENTRY_SIZE;

    return memcmp(ent0, ent1, size);
}


void klnk_barrier_init()
{
    int i;

    if (barrier_stat & VRES_STAT_INIT)
        return;
    for (i = 0; i < KLNK_BARRIER_GROUP_SIZE; i++) {
        pthread_rwlock_init(&barrier_group[i].lock, NULL);
        rbtree_new(&barrier_group[i].tree, klnk_barrier_compare);
    }
    barrier_stat |= VRES_STAT_INIT;
}


static inline unsigned long klnk_barrier_hash(klnk_barrier_desc_t *desc)
{
    klnk_barrier_entry_t *ent = desc->entry;

    assert(KLNK_BARRIER_ENTRY_SIZE == 4);
    return (ent[0] ^ ent[1] ^ ent[2] ^ ent[3]) % KLNK_BARRIER_GROUP_SIZE;
}


static inline void klnk_barrier_get_desc(vres_t *resource, klnk_barrier_desc_t *desc)
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


static inline klnk_barrier_t *klnk_barrier_alloc(klnk_barrier_desc_t *desc)
{
    klnk_barrier_t *barrier = (klnk_barrier_t *)malloc(sizeof(klnk_barrier_t));

    if (!barrier)
        return NULL;
    barrier->flags = 0;
    barrier->count = 0;
    memcpy(&barrier->desc, desc, sizeof(klnk_barrier_desc_t));
    pthread_mutex_init(&barrier->mutex, NULL);
    pthread_cond_init(&barrier->cond, NULL);
    return barrier;
}


static inline klnk_barrier_t *klnk_barrier_lookup(klnk_barrier_group_t *group, klnk_barrier_desc_t *desc)
{
    rbtree_node_t *node = NULL;

    if (!rbtree_find(&group->tree, desc, &node))
        return tree_entry(node, klnk_barrier_t, node);
    else
        return NULL;
}


static inline void klnk_barrier_insert(klnk_barrier_group_t *group, klnk_barrier_t *barrier)
{
    rbtree_insert(&group->tree, &barrier->desc, &barrier->node);
}


static inline klnk_barrier_t *klnk_barrier_get(vres_t *resource, klnk_barrier_group_t **group)
{
    klnk_barrier_t *barrier;
    klnk_barrier_desc_t desc;
    klnk_barrier_group_t *grp;

    klnk_barrier_get_desc(resource, &desc);
    grp = &barrier_group[klnk_barrier_hash(&desc)];
    pthread_rwlock_rdlock(&grp->lock);
    barrier = klnk_barrier_lookup(grp, &desc);
    if (barrier) {
        pthread_mutex_lock(&barrier->mutex);
        barrier->flags &= ~KLNK_BARRIER_CLEAR;
        barrier->count++;
        *group = grp;
    }
    pthread_rwlock_unlock(&grp->lock);
    return barrier;
}


static inline void klnk_barrier_free(klnk_barrier_group_t *group, klnk_barrier_t *barrier)
{
    rbtree_remove(&group->tree, &barrier->desc);
    pthread_mutex_destroy(&barrier->mutex);
    pthread_cond_destroy(&barrier->cond);
    free(barrier);
}


int klnk_barrier_wait(vres_t *resource)
{
    bool release = false;
    klnk_barrier_t *barrier;
    klnk_barrier_group_t *grp;

    if (!(barrier_stat & VRES_STAT_INIT)) {
        log_warning("invalid state");
        return -EINVAL;
    }
    barrier = klnk_barrier_get(resource, &grp);
    if (!barrier)
        return 0;
    log_klnk_barrier_wait(resource, ">-- barrier_wait@start --<");
    pthread_cond_wait(&barrier->cond, &barrier->mutex);
    barrier->count--;
    if (!barrier->count && !(barrier->flags & KLNK_BARRIER_CLEAR)) {
        pthread_rwlock_wrlock(&grp->lock);
        release = true;
    }
    pthread_mutex_unlock(&barrier->mutex);
    if (release) {
        klnk_barrier_free(grp, barrier);
        pthread_rwlock_unlock(&grp->lock);
    }
    log_klnk_barrier_wait(resource, ">-- barrier_wait@end --<");
    return 0;
}


int klnk_barrier_wait_timeout(vres_t *resource, vres_time_t timeout)
{
    bool release = false;
    struct timespec time;
    klnk_barrier_t *barrier;
    klnk_barrier_group_t *grp;

    if (!(barrier_stat & VRES_STAT_INIT)) {
        log_warning("invalid state");
        return -EINVAL;
    }
    barrier = klnk_barrier_get(resource, &grp);
    if (!barrier)
        return 0;
    log_klnk_barrier_wait_timeout(resource, ">-- barrier_wait_timeout@start --<");
    vres_set_timeout(&time, timeout);
    pthread_cond_timedwait(&barrier->cond, &barrier->mutex, &time);
    barrier->count--;
    if (!barrier->count && !(barrier->flags & KLNK_BARRIER_CLEAR)) {
        pthread_rwlock_wrlock(&grp->lock);
        release = true;
    }
    pthread_mutex_unlock(&barrier->mutex);
    if (release) {
        klnk_barrier_free(grp, barrier);
        pthread_rwlock_unlock(&grp->lock);
    }
    log_klnk_barrier_wait_timeout(resource, ">-- barrier_wait_timeout@end --<");
    return 0;
}


int klnk_barrier_set(vres_t *resource)
{
    klnk_barrier_t *barrier;
    klnk_barrier_desc_t desc;
    klnk_barrier_group_t *grp;

    if (!(barrier_stat & VRES_STAT_INIT)) {
        log_warning("invalid state");
        return -EINVAL;
    }
    klnk_barrier_get_desc(resource, &desc);
    grp = &barrier_group[klnk_barrier_hash(&desc)];
    pthread_rwlock_rdlock(&grp->lock);
    barrier = klnk_barrier_lookup(grp, &desc);
    if (!barrier) {
        pthread_rwlock_unlock(&grp->lock);
        return 0;
    }
    pthread_mutex_lock(&barrier->mutex);
    if (barrier->count > 0)
        pthread_cond_broadcast(&barrier->cond);
    pthread_mutex_unlock(&barrier->mutex);
    pthread_rwlock_unlock(&grp->lock);
    log_klnk_barrier_set(resource);
    return 0;
}


int klnk_barrier_clear(vres_t *resource)
{
    int ret = 0;
    klnk_barrier_t *barrier;
    klnk_barrier_desc_t desc;
    klnk_barrier_group_t *grp;

    if (!(barrier_stat & VRES_STAT_INIT)) {
        log_warning("invalid state");
        return -EINVAL;
    }
    klnk_barrier_get_desc(resource, &desc);
    grp = &barrier_group[klnk_barrier_hash(&desc)];
    pthread_rwlock_wrlock(&grp->lock);
    barrier = klnk_barrier_lookup(grp, &desc);
    if (!barrier) {
        barrier = klnk_barrier_alloc(&desc);
        if (!barrier) {
            log_resource_warning(resource, "no memory");
            pthread_rwlock_unlock(&grp->lock);
            return -ENOMEM;
        }
        klnk_barrier_insert(grp, barrier);
    }
    pthread_mutex_lock(&barrier->mutex);
    barrier->flags |= KLNK_BARRIER_CLEAR;
    pthread_mutex_unlock(&barrier->mutex);
    pthread_rwlock_unlock(&grp->lock);
    log_klnk_barrier_clear(resource);
    return ret;
}
