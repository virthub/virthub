#include "mutex.h"

int mutex_stat = 0;
klnk_mutex_group_t mutex_group[KLNK_MUTEX_GROUP_SIZE];

int klnk_mutex_compare(const void *val0, const void *val1)
{
    klnk_mutex_entry_t *ent0 = ((klnk_mutex_desc_t *)val0)->entry;
    klnk_mutex_entry_t *ent1 = ((klnk_mutex_desc_t *)val1)->entry;
    size_t size = KLNK_MUTEX_ENTRY_SIZE * sizeof(klnk_mutex_entry_t);

    return memcmp(ent0, ent1, size);
}


void klnk_mutex_init()
{
    int i;

    if (mutex_stat)
        return;
    for (i = 0; i < KLNK_MUTEX_GROUP_SIZE; i++) {
        pthread_mutex_init(&mutex_group[i].mutex, NULL);
        rbtree_new(&mutex_group[i].tree, klnk_mutex_compare);
    }
    mutex_stat = VRES_STAT_INIT;
}


static inline unsigned long klnk_mutex_hash(klnk_mutex_desc_t *desc)
{
    klnk_mutex_entry_t *ent = desc->entry;

    assert(KLNK_MUTEX_ENTRY_SIZE == 4);
    return (ent[0] ^ ent[1] ^ ent[2] ^ ent[3]) % KLNK_MUTEX_GROUP_SIZE;
}


static inline void klnk_mutex_get_desc(vres_t *resource, klnk_mutex_desc_t *desc)
{
    desc->entry[0] = resource->key;
    desc->entry[1] = resource->cls;
    desc->entry[2] = resource->owner;
    if (VRES_CLS_SHM == resource->cls)
        desc->entry[3] = vres_get_region(resource);
    else
        desc->entry[3] = 0;
}


static inline klnk_mutex_t *klnk_mutex_alloc(klnk_mutex_desc_t *desc)
{
    klnk_mutex_t *mutex = (klnk_mutex_t *)malloc(sizeof(klnk_mutex_t));

    if (mutex) {
        mutex->count = 0;
        memcpy(&mutex->desc, desc, sizeof(klnk_mutex_desc_t));
        pthread_mutex_init(&mutex->mutex, NULL);
        pthread_cond_init(&mutex->cond, NULL);
    }
    return mutex;
}


static inline klnk_mutex_t *klnk_mutex_lookup(klnk_mutex_group_t *group, klnk_mutex_desc_t *desc)
{
    rbtree_node_t *node = NULL;

    if (!rbtree_find(&group->tree, desc, &node))
        return tree_entry(node, klnk_mutex_t, node);
    else
        return NULL;
}


static inline void klnk_mutex_insert(klnk_mutex_group_t *group, klnk_mutex_t *mutex)
{
    rbtree_insert(&group->tree, &mutex->desc, &mutex->node);
}


static inline klnk_mutex_t *klnk_mutex_get(vres_t *resource)
{
    klnk_mutex_t *mutex;
    klnk_mutex_desc_t desc;
    klnk_mutex_group_t *grp;

    klnk_mutex_get_desc(resource, &desc);
    grp = &mutex_group[klnk_mutex_hash(&desc)];
    pthread_mutex_lock(&grp->mutex);
    mutex = klnk_mutex_lookup(grp, &desc);
    if (!mutex) {
        mutex = klnk_mutex_alloc(&desc);
        if (!mutex)
            goto out;
        klnk_mutex_insert(grp, mutex);
    }
    pthread_mutex_lock(&mutex->mutex);
    mutex->count++;
out:
    pthread_mutex_unlock(&grp->mutex);
    return mutex;
}


static void klnk_mutex_free(klnk_mutex_group_t *group, klnk_mutex_t *mutex)
{
    rbtree_remove(&group->tree, &mutex->desc);
    pthread_mutex_destroy(&mutex->mutex);
    pthread_cond_destroy(&mutex->cond);
    free(mutex);
}


int klnk_mutex_lock(vres_t *resource)
{
    klnk_mutex_t *mutex;

    if (!mutex_stat) {
        log_warning("invalid state");
        return -EINVAL;
    }
    mutex = klnk_mutex_get(resource);
    if (!mutex)
        return -ENOENT;
    if (mutex->count > 1)
        pthread_cond_wait(&mutex->cond, &mutex->mutex);
    pthread_mutex_unlock(&mutex->mutex);
    return 0;
}


void klnk_mutex_unlock(vres_t *resource)
{
    klnk_mutex_t *mutex;
    bool release = false;
    klnk_mutex_desc_t desc;
    klnk_mutex_group_t *grp;

    if (!mutex_stat) {
        log_warning("invalid state");
        return;
    }
    klnk_mutex_get_desc(resource, &desc);
    grp = &mutex_group[klnk_mutex_hash(&desc)];
    pthread_mutex_lock(&grp->mutex);
    mutex = klnk_mutex_lookup(grp, &desc);
    if (!mutex) {
        pthread_mutex_unlock(&grp->mutex);
        return;
    }
    pthread_mutex_lock(&mutex->mutex);
    if (mutex->count > 0) {
        mutex->count--;
        if (mutex->count > 0)
            pthread_cond_signal(&mutex->cond);
        else
            release = true;
    }
    pthread_mutex_unlock(&mutex->mutex);
    if (release)
        klnk_mutex_free(grp, mutex);
    pthread_mutex_unlock(&grp->mutex);
}
