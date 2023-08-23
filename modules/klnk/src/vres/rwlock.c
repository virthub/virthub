#include "rwlock.h"

int rwlock_stat = 0;
vres_rwlock_group_t rwlock_group[VRES_RWLOCK_GROUP_SIZE];

int vres_rwlock_compare(const void *val0, const void *val1)
{
    vres_rwlock_entry_t *ent0 = ((vres_rwlock_desc_t *)val0)->entry;
    vres_rwlock_entry_t *ent1 = ((vres_rwlock_desc_t *)val1)->entry;
    size_t size = VRES_RWLOCK_ENTRY_SIZE * sizeof(vres_rwlock_entry_t);

    return memcmp(ent0, ent1, size);
}


void vres_rwlock_init()
{
    int i;

    if (rwlock_stat & VRES_STAT_INIT)
        return;
    for (i = 0; i < VRES_RWLOCK_GROUP_SIZE; i++) {
        pthread_mutex_init(&rwlock_group[i].mutex, NULL);
        rbtree_new(&rwlock_group[i].tree, vres_rwlock_compare);
    }
    rwlock_stat |= VRES_STAT_INIT;
}


static inline unsigned long vres_rwlock_hash(vres_rwlock_desc_t *desc)
{
    vres_rwlock_entry_t *ent = desc->entry;

    assert(VRES_RWLOCK_ENTRY_SIZE == 2);
    return (ent[0] ^ ent[1]) % VRES_RWLOCK_GROUP_SIZE;
}


static inline void vres_rwlock_get_desc(vres_t *resource, vres_rwlock_desc_t *desc)
{
    desc->entry[0] = resource->key;
    desc->entry[1] = resource->cls;
}


static inline vres_rwlock_t *vres_rwlock_alloc(vres_rwlock_desc_t *desc)
{
    vres_rwlock_t *lock = (vres_rwlock_t *)malloc(sizeof(vres_rwlock_t));

    if (lock) {
        lock->desc = *desc;
        pthread_rwlock_init(&lock->lock, NULL);
    }
    return lock;
}


static inline vres_rwlock_t *vres_rwlock_lookup(vres_rwlock_group_t *group, vres_rwlock_desc_t *desc)
{
    rbtree_node_t *node = NULL;

    if (!rbtree_find(&group->tree, desc, &node))
        return tree_entry(node, vres_rwlock_t, node);
    else
        return NULL;
}


static inline void vres_rwlock_insert(vres_rwlock_group_t *group, vres_rwlock_t *lock)
{
    if (rbtree_insert(&group->tree, &lock->desc, &lock->node))
        log_warning("failed");
}


static inline vres_rwlock_t *vres_rwlock_get(vres_t *resource)
{
    vres_rwlock_desc_t desc;
    vres_rwlock_group_t *grp;
    vres_rwlock_t *lock = NULL;

    vres_rwlock_get_desc(resource, &desc);
    grp = &rwlock_group[vres_rwlock_hash(&desc)];
    pthread_mutex_lock(&grp->mutex);
    lock = vres_rwlock_lookup(grp, &desc);
    if (!lock) {
        lock = vres_rwlock_alloc(&desc);
        if (!lock) {
            log_resource_warning(resource, "no memory");
            goto out;
        }
        vres_rwlock_insert(grp, lock);
        log_rwlock_get(resource, "create, group=%lu, entry=<%lu, %lu>, lock=0x%lx", vres_rwlock_hash(&desc), desc.entry[0], desc.entry[1], (unsigned long)lock);
    } else
        log_rwlock_get(resource, "group=%lu, entry=(%lu, %lu), lock=0x%lx", vres_rwlock_hash(&desc), desc.entry[0], desc.entry[1], (unsigned long)lock);
out:
    pthread_mutex_unlock(&grp->mutex);
    return lock;
}


int vres_rwlock_rdlock(vres_t *resource)
{
    vres_rwlock_t *lock;

    if (!(rwlock_stat & VRES_STAT_INIT)) {
        log_warning("invalid state");
        return -EINVAL;
    }
    lock = vres_rwlock_get(resource);
    if (!lock) {
        log_resource_warning(resource, "no entry");
        return -ENOENT;
    }
    pthread_rwlock_rdlock(&lock->lock);
    log_rwlock_rdlock(resource, "lock=0x%lx", (unsigned long)lock);
    return 0;
}


int vres_rwlock_wrlock(vres_t *resource)
{
    vres_rwlock_t *lock;

    if (!(rwlock_stat & VRES_STAT_INIT)) {
        log_warning("invalid state");
        return -EINVAL;
    }
    lock = vres_rwlock_get(resource);
    if (!lock) {
        log_resource_warning(resource, "no entry");
        return -ENOENT;
    }
    pthread_rwlock_wrlock(&lock->lock);
    log_rwlock_wrlock(resource, "lock=0x%lx", (unsigned long)lock);
    return 0;
}


int vres_rwlock_trywrlock(vres_t *resource)
{
    int ret;
    vres_rwlock_t *lock;

    if (!(rwlock_stat & VRES_STAT_INIT)) {
        log_warning("invalid state");
        return -EINVAL;
    }
    lock = vres_rwlock_get(resource);
    if (!lock) {
        log_resource_warning(resource, "no entry");
        return -ENOENT;
    }
    ret = pthread_rwlock_trywrlock(&lock->lock);
    log_rwlock_wrlock(resource, "lock=0x%lx, ret=%s", (unsigned long)lock, log_get_err(ret));
    return ret;
}


void vres_rwlock_unlock(vres_t *resource)
{
    vres_rwlock_t *lock;

    if (!(rwlock_stat & VRES_STAT_INIT)) {
        log_warning("invalid state");
        return;
    }
    lock = vres_rwlock_get(resource);
    if (!lock) {
        log_resource_warning(resource, "no entry");
        return;
    }
    log_rwlock_unlock(resource, "lock=0x%lx", (unsigned long)lock);
    pthread_rwlock_unlock(&lock->lock);
}
