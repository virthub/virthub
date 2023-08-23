#include "cache.h"

int cache_stat = 0;
vres_cache_group_t cache_group[VRES_CACHE_GROUP_SIZE];

int vres_cache_compare(const void *val0, const void *val1)
{
    vres_cache_entry_t *ent0 = ((vres_cache_desc_t *)val0)->entry;
    vres_cache_entry_t *ent1 = ((vres_cache_desc_t *)val1)->entry;
    const size_t size = sizeof(vres_cache_entry_t) * VRES_CACHE_ENTRY_SIZE;

    return memcmp(ent0, ent1, size);
}


void vres_cache_init()
{
    int i;

    if (cache_stat & VRES_STAT_INIT)
        return;
    for (i = 0; i < VRES_CACHE_GROUP_SIZE; i++) {
        rbtree_new(&cache_group[i].tree, vres_cache_compare);
        pthread_mutex_init(&cache_group[i].mutex, NULL);
        INIT_LIST_HEAD(&cache_group[i].list);
        cache_group[i].count = 0;
    }
    cache_stat |= VRES_STAT_INIT;
}


static inline unsigned long vres_cache_hash(vres_cache_desc_t *desc)
{
    vres_cache_entry_t *ent = desc->entry;

    assert(VRES_CACHE_ENTRY_SIZE == 2);
    return (ent[0] ^ ent[1]) % VRES_CACHE_GROUP_SIZE;
}


static inline void vres_cache_get_desc(vres_t *resource, vres_cache_desc_t *desc)
{
    desc->entry[0] = resource->key;
    desc->entry[1] = resource->cls;
}


static inline vres_cache_t *vres_cache_alloc(vres_cache_desc_t *desc, size_t len)
{
    vres_cache_t *cache;

    if (!len)
        return NULL;
    cache = (vres_cache_t *)malloc(sizeof(vres_cache_t));
    if (!cache)
        return NULL;
    cache->buf = malloc(len);
    if (!cache->buf) {
        free(cache);
        return NULL;
    }
    cache->len = len;
    INIT_LIST_HEAD(&cache->list);
    memcpy(&cache->desc, desc, sizeof(vres_cache_desc_t));
    return cache;
}


static inline vres_cache_t *vres_cache_lookup(vres_cache_group_t *group, vres_cache_desc_t *desc)
{
    rbtree_node_t *node = NULL;

    if (!rbtree_find(&group->tree, desc, &node))
        return tree_entry(node, vres_cache_t, node);
    else
        return NULL;
}


static inline void vres_cache_free(vres_cache_group_t *group, vres_cache_t *cache)
{
    rbtree_remove(&group->tree, &cache->desc);
    list_del(&cache->list);
    free(cache->buf);
    free(cache);
    group->count--;
}


static inline void vres_cache_insert(vres_cache_group_t *group, vres_cache_t *cache)
{
    rbtree_insert(&group->tree, &cache->desc, &cache->node);
    list_add(&cache->list, &group->list);
    group->count++;
}


int vres_cache_realloc(vres_cache_t *cache, size_t len)
{
    if (len > cache->len) {
        cache->buf = realloc(cache->buf, len);
        if (!cache->buf)
            return -EINVAL;
        cache->len = len;
    }
    return 0;
}


int vres_cache_write(vres_t *resource, char *buf, size_t len)
{
    int ret = 0;
    vres_cache_t *cache;
    vres_cache_desc_t desc;
    vres_cache_group_t *grp;

    if (!(cache_stat & VRES_STAT_INIT))
        return -EINVAL;
    vres_cache_get_desc(resource, &desc);
    grp = &cache_group[vres_cache_hash(&desc)];
    pthread_mutex_lock(&grp->mutex);
    cache = vres_cache_lookup(grp, &desc);
    if (!cache) {
        cache = vres_cache_alloc(&desc, len);
        if (!cache) {
            log_resource_warning(resource, "no memory");
            ret = -ENOMEM;
            goto out;
        }
        if (VRES_CACHE_QUEUE_SIZE == grp->count)
            vres_cache_free(grp, list_entry(grp->list.prev, vres_cache_t, list));
        vres_cache_insert(grp, cache);
    } else {
        ret = vres_cache_realloc(cache, len);
        if (ret) {
            log_resource_warning(resource, "failed to write cache");
            goto out;
        }
    }
    memcpy(cache->buf, buf, len);
out:
    pthread_mutex_unlock(&grp->mutex);
    return ret;
}


int vres_cache_read(vres_t *resource, char *buf, size_t len)
{
    int ret = 0;
    vres_cache_desc_t desc;
    vres_cache_group_t *grp;
    vres_cache_t *cache = NULL;

    if (!(cache_stat & VRES_STAT_INIT))
        return -EINVAL;
    vres_cache_get_desc(resource, &desc);
    grp = &cache_group[vres_cache_hash(&desc)];
    pthread_mutex_lock(&grp->mutex);
    cache = vres_cache_lookup(grp, &desc);
    if (!cache) {
        ret = -ENOENT;
        goto out;
    }
    if (len > cache->len) {
        ret = -EINVAL;
        goto out;
    } else if (len > 0) {
        list_del(&cache->list);
        list_add(&cache->list, &grp->list);
        memcpy(buf, cache->buf, len);
    }
out:
    pthread_mutex_unlock(&grp->mutex);
    return ret;
}


void vres_cache_flush(vres_t *resource)
{
    vres_cache_t *cache;
    vres_cache_desc_t desc;
    vres_cache_group_t *grp;

    if (!(cache_stat & VRES_STAT_INIT))
        return;
    vres_cache_get_desc(resource, &desc);
    grp = &cache_group[vres_cache_hash(&desc)];
    pthread_mutex_lock(&grp->mutex);
    cache = vres_cache_lookup(grp, &desc);
    if (cache)
        vres_cache_free(grp, cache);
    pthread_mutex_unlock(&grp->mutex);
}


void vres_cache_release()
{
    int i, j;
    vres_cache_group_t *grp = cache_group;

    if (!(cache_stat & VRES_STAT_INIT))
        return;
    for (i = 0; i < VRES_CACHE_GROUP_SIZE; i++) {
        pthread_mutex_lock(&grp->mutex);
        for (j = 0; j < grp->count; j++)
            vres_cache_free(grp, list_entry(grp->list.next, vres_cache_t, list));
        pthread_mutex_unlock(&grp->mutex);
        grp++;
    }
}
