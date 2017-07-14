/* rwlock.c
 *
 * Copyright (C) 2017 Yi-Wei Ci
 *
 * Distributed under the terms of the MIT license.
 */

#include "rwlock.h"

int rwlock_stat = 0;
vres_rwlock_group_t rwlock_group[VRES_RWLOCK_GROUP_SIZE];

void vres_rwlock_init()
{
	int i;

	if (rwlock_stat & VRES_STAT_INIT)
		return;

	for (i = 0; i < VRES_RWLOCK_GROUP_SIZE; i++) {
		pthread_mutex_init(&rwlock_group[i].mutex, NULL);
		rwlock_group[i].head = rbtree_create();
	}
	rwlock_stat |= VRES_STAT_INIT;
}


static inline int vres_rwlock_compare(char *l1, char *l2)
{
	vres_rwlock_desc_t *desc1 = (vres_rwlock_desc_t *)l1;
	vres_rwlock_desc_t *desc2 = (vres_rwlock_desc_t *)l2;

	return memcmp(desc1->entry, desc2->entry, sizeof(((vres_rwlock_desc_t *)0)->entry));
}


static inline unsigned long vres_rwlock_hash(vres_rwlock_desc_t *desc)
{
	unsigned long *ent = desc->entry;

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

	if (!lock)
		return NULL;

	memcpy(&lock->desc, desc, sizeof(vres_rwlock_desc_t));
	pthread_rwlock_init(&lock->lock, NULL);
	INIT_LIST_HEAD(&lock->list);
	return lock;
}


static inline vres_rwlock_t *vres_rwlock_lookup(vres_rwlock_group_t *group, vres_rwlock_desc_t *desc)
{
	return rbtree_lookup(group->head, (void *)desc, vres_rwlock_compare);
}


static inline void vres_rwlock_insert(vres_rwlock_group_t *group, vres_rwlock_t *lock)
{
	rbtree_insert(group->head, (void *)&lock->desc, (void *)lock, vres_rwlock_compare);
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
			vres_log_err(resource, "no memory");
			goto out;
		}
		vres_rwlock_insert(grp, lock);
	}
out:
	pthread_mutex_unlock(&grp->mutex);
	return lock;
}


int vres_rwlock_rdlock(vres_t *resource)
{
	vres_rwlock_t *lock;

	if (!(rwlock_stat & VRES_STAT_INIT)) {
		log_err("invalid state");
		return -EINVAL;
	}

	lock = vres_rwlock_get(resource);
	if (!lock) {
		vres_log_err(resource, "no entry");
		return -ENOENT;
	}

	pthread_rwlock_rdlock(&lock->lock);
	rwlock_log(resource);
	return 0;
}


int vres_rwlock_wrlock(vres_t *resource)
{
	vres_rwlock_t *lock;

	if (!(rwlock_stat & VRES_STAT_INIT)) {
		log_err("invalid state");
		return -EINVAL;
	}

	lock = vres_rwlock_get(resource);
	if (!lock) {
		vres_log_err(resource, "no entry");
		return -ENOENT;
	}

	pthread_rwlock_wrlock(&lock->lock);
	rwlock_log(resource);
	return 0;
}


int vres_rwlock_trywrlock(vres_t *resource)
{
	int ret;
	vres_rwlock_t *lock;

	if (!(rwlock_stat & VRES_STAT_INIT)) {
		log_err("invalid state");
		return -EINVAL;
	}

	lock = vres_rwlock_get(resource);
	if (!lock) {
		vres_log_err(resource, "no entry");
		return -ENOENT;
	}

	ret = pthread_rwlock_trywrlock(&lock->lock);
	rwlock_log(resource);
	return ret;
}


void vres_rwlock_unlock(vres_t *resource)
{
	vres_rwlock_t *lock;

	if (!(rwlock_stat & VRES_STAT_INIT)) {
		log_err("invalid state");
		return;
	}

	lock = vres_rwlock_get(resource);
	if (!lock) {
		vres_log_err(resource, "no entry");
		return;
	}

	pthread_rwlock_unlock(&lock->lock);
	rwlock_log(resource);
}
