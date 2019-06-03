/* barrier.c
 *
 * Copyright (C) 2019 Yi-Wei Ci
 *
 * Distributed under the terms of the MIT license.
 */

#include "barrier.h"

int barrier_stat = 0;
vres_barrier_group_t barrier_group[VRES_BARRIER_GROUP_SIZE];

int vres_barrier_compare(const void *val0, const void *val1)
{
    vres_barrier_entry_t *ent0 = ((vres_barrier_desc_t *)val0)->entry;
    vres_barrier_entry_t *ent1 = ((vres_barrier_desc_t *)val1)->entry;
    const size_t size = sizeof(vres_barrier_entry_t) * VRES_BARRIER_ENTRY_SIZE;

    return memcmp(ent0, ent1, size);
}


void vres_barrier_init()
{
    int i;

    if (barrier_stat & VRES_STAT_INIT)
        return;

    for (i = 0; i < VRES_BARRIER_GROUP_SIZE; i++) {
        pthread_rwlock_init(&barrier_group[i].lock, NULL);
        rbtree_new(&barrier_group[i].tree, vres_barrier_compare);
    }
    barrier_stat |= VRES_STAT_INIT;
}


static inline unsigned long vres_barrier_hash(vres_barrier_desc_t *desc)
{
    vres_barrier_entry_t *ent = desc->entry;

    assert(VRES_BARRIER_ENTRY_SIZE == 4);
    return (ent[0] ^ ent[1] ^ ent[2] ^ ent[3]) % VRES_BARRIER_GROUP_SIZE;
}


static inline void vres_barrier_get_desc(vres_t *resource, vres_barrier_desc_t *desc)
{
    desc->entry[0] = resource->key;
    desc->entry[1] = resource->cls;
    if (vres_get_op(resource) == VRES_OP_SHMFAULT) {
        desc->entry[2] = resource->owner;
        desc->entry[3] = vres_get_off(resource);
    } else {
        desc->entry[2] = 0;
        desc->entry[3] = 0;
    }
}


static inline vres_barrier_t *vres_barrier_alloc(vres_barrier_desc_t *desc)
{
    vres_barrier_t *barrier = (vres_barrier_t *)malloc(sizeof(vres_barrier_t));

    if (!barrier)
        return NULL;
    barrier->flags = 0;
    barrier->count = 0;
    memcpy(&barrier->desc, desc, sizeof(vres_barrier_desc_t));
    pthread_mutex_init(&barrier->mutex, NULL);
    pthread_cond_init(&barrier->cond, NULL);
    return barrier;
}


static inline vres_barrier_t *vres_barrier_lookup(vres_barrier_group_t *group, vres_barrier_desc_t *desc)
{
    rbtree_node_t *node = NULL;

    if (!rbtree_find(&group->tree, desc, &node))
        return tree_entry(node, vres_barrier_t, node);
    else
        return NULL;
}


static inline void vres_barrier_insert(vres_barrier_group_t *group, vres_barrier_t *barrier)
{
    rbtree_insert(&group->tree, &barrier->desc, &barrier->node);
}


static inline vres_barrier_t *vres_barrier_get(vres_t *resource, vres_barrier_group_t **group)
{
    vres_barrier_t *barrier;
    vres_barrier_desc_t desc;
    vres_barrier_group_t *grp;

    vres_barrier_get_desc(resource, &desc);
    grp = &barrier_group[vres_barrier_hash(&desc)];
    pthread_rwlock_rdlock(&grp->lock);
    barrier = vres_barrier_lookup(grp, &desc);
    if (barrier) {
        pthread_mutex_lock(&barrier->mutex);
        barrier->flags &= ~VRES_BARRIER_CLEAR;
        barrier->count++;
        *group = grp;
    }
    pthread_rwlock_unlock(&grp->lock);
    return barrier;
}


static inline void vres_barrier_free(vres_barrier_group_t *group, vres_barrier_t *barrier)
{
    rbtree_remove(&group->tree, &barrier->desc);
    pthread_mutex_destroy(&barrier->mutex);
    pthread_cond_destroy(&barrier->cond);
    free(barrier);
}


int vres_barrier_wait(vres_t *resource)
{
    bool release = false;
    vres_barrier_t *barrier;
    vres_barrier_group_t *grp;

    if (!(barrier_stat & VRES_STAT_INIT)) {
        log_err("invalid state");
        return -EINVAL;
    }
    barrier = vres_barrier_get(resource, &grp);
    if (!barrier)
        return 0;
    log_barrier_wait(resource, ">-- barrier_wait (begin) --<");
    pthread_cond_wait(&barrier->cond, &barrier->mutex);
    barrier->count--;
    if (!barrier->count && !(barrier->flags & VRES_BARRIER_CLEAR)) {
        pthread_rwlock_wrlock(&grp->lock);
        release = true;
    }
    pthread_mutex_unlock(&barrier->mutex);
    if (release) {
        vres_barrier_free(grp, barrier);
        pthread_rwlock_unlock(&grp->lock);
    }
    log_barrier_wait(resource, ">-- barrier_wait (end) --<");
    return 0;
}


int vres_barrier_wait_timeout(vres_t *resource, vres_time_t timeout)
{
    bool release = false;
    struct timespec time;
    vres_barrier_t *barrier;
    vres_barrier_group_t *grp;

    if (!(barrier_stat & VRES_STAT_INIT)) {
        log_err("invalid state");
        return -EINVAL;
    }
    barrier = vres_barrier_get(resource, &grp);
    if (!barrier)
        return 0;
    log_barrier_wait_timeout(resource, ">-- barrier_wait_timeout (begin) --<");
    vres_set_timeout(&time, timeout);
    pthread_cond_timedwait(&barrier->cond, &barrier->mutex, &time);
    barrier->count--;
    if (!barrier->count && !(barrier->flags & VRES_BARRIER_CLEAR)) {
        pthread_rwlock_wrlock(&grp->lock);
        release = true;
    }
    pthread_mutex_unlock(&barrier->mutex);
    if (release) {
        vres_barrier_free(grp, barrier);
        pthread_rwlock_unlock(&grp->lock);
    }
    log_barrier_wait_timeout(resource, ">-- barrier_wait_timeout (end) --<");
    return 0;
}


int vres_barrier_set(vres_t *resource)
{
    vres_barrier_t *barrier;
    vres_barrier_desc_t desc;
    vres_barrier_group_t *grp;

    if (!(barrier_stat & VRES_STAT_INIT)) {
        log_err("invalid state");
        return -EINVAL;
    }
    vres_barrier_get_desc(resource, &desc);
    grp = &barrier_group[vres_barrier_hash(&desc)];
    pthread_rwlock_rdlock(&grp->lock);
    barrier = vres_barrier_lookup(grp, &desc);
    if (!barrier) {
        pthread_rwlock_unlock(&grp->lock);
        return 0;
    }
    pthread_mutex_lock(&barrier->mutex);
    if (barrier->count > 0)
        pthread_cond_broadcast(&barrier->cond);
    pthread_mutex_unlock(&barrier->mutex);
    pthread_rwlock_unlock(&grp->lock);
    log_barrier_set(resource);
    return 0;
}


int vres_barrier_clear(vres_t *resource)
{
    int ret = 0;
    vres_barrier_t *barrier;
    vres_barrier_desc_t desc;
    vres_barrier_group_t *grp;

    if (!(barrier_stat & VRES_STAT_INIT)) {
        log_err("invalid state");
        return -EINVAL;
    }
    vres_barrier_get_desc(resource, &desc);
    grp = &barrier_group[vres_barrier_hash(&desc)];
    pthread_rwlock_wrlock(&grp->lock);
    barrier = vres_barrier_lookup(grp, &desc);
    if (!barrier) {
        barrier = vres_barrier_alloc(&desc);
        if (!barrier) {
            log_resource_err(resource, "no memory");
            pthread_rwlock_unlock(&grp->lock);
            return -ENOMEM;
        }
        vres_barrier_insert(grp, barrier);
    }
    pthread_mutex_lock(&barrier->mutex);
    barrier->flags |= VRES_BARRIER_CLEAR;
    pthread_mutex_unlock(&barrier->mutex);
    pthread_rwlock_unlock(&grp->lock);
    log_barrier_clear(resource);
    return ret;
}
