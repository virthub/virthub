/* mutex.c
 *
 * Copyright (C) 2019 Yi-Wei Ci
 *
 * Distributed under the terms of the MIT license.
 */

#include "mutex.h"

int mutex_stat = 0;
klnk_mutex_group_t mutex_group[KLNK_MUTEX_GROUP_SIZE];

void klnk_mutex_init()
{
    int i;

    if (mutex_stat)
        return;

    for (i = 0; i < KLNK_MUTEX_GROUP_SIZE; i++) {
        pthread_mutex_init(&mutex_group[i].mutex, NULL);
        mutex_group[i].head = rbtree_create();
    }
    mutex_stat = 1;
}


static inline unsigned long klnk_mutex_hash(klnk_mutex_desc_t *desc)
{
    unsigned long *ent = desc->entry;

    return (ent[0] ^ ent[1] ^ ent[2] ^ ent[3]) % KLNK_MUTEX_GROUP_SIZE;
}


static inline int klnk_mutex_compare(char *m1, char *m2)
{
    klnk_mutex_desc_t *desc1 = (klnk_mutex_desc_t *)m1;
    klnk_mutex_desc_t *desc2 = (klnk_mutex_desc_t *)m2;

    return memcmp(desc1->entry, desc2->entry, sizeof(((klnk_mutex_desc_t *)0)->entry));
}


static inline void klnk_mutex_get_desc(vres_t *resource, klnk_mutex_desc_t *desc)
{
    desc->entry[0] = resource->key;
    desc->entry[1] = resource->cls;
    desc->entry[2] = resource->owner;
    if (VRES_CLS_SHM == resource->cls)
        desc->entry[3] = vres_get_off(resource);
    else
        desc->entry[3] = 0;
}


static inline klnk_mutex_t *klnk_mutex_alloc(klnk_mutex_desc_t *desc)
{
    klnk_mutex_t *mutex = (klnk_mutex_t *)malloc(sizeof(klnk_mutex_t));

    if (!mutex)
        return NULL;

    mutex->count = 0;
    memcpy(&mutex->desc, desc, sizeof(klnk_mutex_desc_t));
    pthread_mutex_init(&mutex->mutex, NULL);
    pthread_cond_init(&mutex->cond, NULL);
    return mutex;
}


static inline klnk_mutex_t *klnk_mutex_lookup(klnk_mutex_group_t *group, klnk_mutex_desc_t *desc)
{
    return rbtree_lookup(group->head, (void *)desc, klnk_mutex_compare);
}


static inline void klnk_mutex_insert(klnk_mutex_group_t *group, klnk_mutex_t *mutex)
{
    rbtree_insert(group->head, (void *)&mutex->desc, (void *)mutex, klnk_mutex_compare);
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
    rbtree_delete(group->head, (void *)&mutex->desc, klnk_mutex_compare);
    pthread_mutex_destroy(&mutex->mutex);
    pthread_cond_destroy(&mutex->cond);
    free(mutex);
}


int klnk_mutex_lock(vres_t *resource)
{
    klnk_mutex_t *mutex;

    if (!mutex_stat) {
        klnk_log_err("invalid state");
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
    int release = 0;
    klnk_mutex_t *mutex;
    klnk_mutex_desc_t desc;
    klnk_mutex_group_t *grp;

    if (!mutex_stat) {
        klnk_log_err("invalid state");
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
            release = 1;
    }
    pthread_mutex_unlock(&mutex->mutex);
    if (release)
        klnk_mutex_free(grp, mutex);
    pthread_mutex_unlock(&grp->mutex);
}
