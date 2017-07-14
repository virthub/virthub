/* lock.c
 *
 * Copyright (C) 2017 Yi-Wei Ci
 *
 * Distributed under the terms of the MIT license.
 */

#include "lock.h"

int lock_stat = 0;
vres_lock_group_t lock_group[VRES_LOCK_GROUP_SIZE];

void vres_lock_init()
{
	int i;

	if (lock_stat & VRES_STAT_INIT)
		return;

	for (i = 0; i < VRES_LOCK_GROUP_SIZE; i++) {
		pthread_mutex_init(&lock_group[i].mutex, NULL);
		lock_group[i].head = rbtree_create();
	}
	lock_stat |= VRES_STAT_INIT;
}


static inline int vres_lock_compare(char *l1, char *l2)
{
	vres_lock_desc_t *desc1 = (vres_lock_desc_t *)l1;
	vres_lock_desc_t *desc2 = (vres_lock_desc_t *)l2;

	return memcmp(desc1->entry, desc2->entry, sizeof(((vres_lock_desc_t *)0)->entry));
}


static inline unsigned long vres_lock_hash(vres_lock_desc_t *desc)
{
	unsigned long *ent = desc->entry;

	return (ent[0] ^ ent[1] ^ ent[2] ^ ent[3]) % VRES_LOCK_GROUP_SIZE;
}


static inline void vres_lock_get_desc(vres_t *resource, vres_lock_desc_t *desc)
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


static inline vres_lock_t *vres_lock_alloc(vres_lock_desc_t *desc)
{
	vres_lock_t *lock = (vres_lock_t *)malloc(sizeof(vres_lock_t));

	if (!lock)
		return NULL;

	lock->count = 0;
	memcpy(&lock->desc, desc, sizeof(vres_lock_desc_t));
	pthread_mutex_init(&lock->mutex, NULL);
	pthread_cond_init(&lock->cond, NULL);
	INIT_LIST_HEAD(&lock->list);
	return lock;
}


static inline vres_lock_t *vres_lock_lookup(vres_lock_group_t *group, vres_lock_desc_t *desc)
{
	return rbtree_lookup(group->head, (void *)desc, vres_lock_compare);
}


static inline void vres_lock_insert(vres_lock_group_t *group, vres_lock_t *lock)
{
	rbtree_insert(group->head, (void *)&lock->desc, (void *)lock, vres_lock_compare);
}


static inline vres_lock_t *vres_lock_get(vres_t *resource)
{
	vres_lock_desc_t desc;
	vres_lock_group_t *grp;
	vres_lock_t *lock = NULL;

	vres_lock_get_desc(resource, &desc);
	grp = &lock_group[vres_lock_hash(&desc)];
	pthread_mutex_lock(&grp->mutex);
	lock = vres_lock_lookup(grp, &desc);
	if (!lock) {
		lock = vres_lock_alloc(&desc);
		if (!lock) {
			vres_log_err(resource, "no memory");
			goto out;
		}
		vres_lock_insert(grp, lock);
	}
	pthread_mutex_lock(&lock->mutex);
	lock->count++;
out:
	pthread_mutex_unlock(&grp->mutex);
	return lock;
}


static inline void vres_lock_free(vres_lock_group_t *group, vres_lock_t *lock)
{
	rbtree_delete(group->head, (void *)&lock->desc, vres_lock_compare);
	pthread_mutex_destroy(&lock->mutex);
	pthread_cond_destroy(&lock->cond);
	free(lock);
}


vres_lock_t *vres_lock_top(vres_t *resource)
{
	vres_lock_t *lock;

	if (!(lock_stat & VRES_STAT_INIT)) {
		log_err("invalid state");
		return NULL;
	}

	lock = vres_lock_get(resource);
	lock_log(resource);
	return lock;
}


void vres_unlock_top(vres_lock_t *lock)
{
	if (lock->count > 0)
		lock->count--;

	pthread_mutex_unlock(&lock->mutex);
}


int vres_lock_buttom(vres_lock_t *lock)
{
	vres_lock_list_t *list;
	pthread_t tid = pthread_self();

	if (!(lock_stat & VRES_STAT_INIT)) {
		log_err("invalid state");
		return -EINVAL;
	}

	list = malloc(sizeof(vres_lock_list_t));
	if (!list) {
		log_err("no memory");
		return -ENOMEM;
	}

	list->tid = tid;
	list_add_tail(&list->list, &lock->list);
	if (lock->count > 1) {
		vres_lock_list_t *curr;

		do {
			pthread_cond_wait(&lock->cond, &lock->mutex);
			curr = list_entry(lock->list.next, vres_lock_list_t, list);
		} while (curr->tid != tid);
	}
	pthread_mutex_unlock(&lock->mutex);
	return 0;
}


int vres_lock(vres_t *resource)
{
	int ret = 0;
	vres_lock_t *lock;

	if (!(lock_stat & VRES_STAT_INIT)) {
		log_err("invalid state");
		return -EINVAL;
	}

	lock = vres_lock_get(resource);
	if (!lock) {
		vres_log_err(resource, "no entry");
		return -ENOENT;
	}

	if (lock->count > 1)
		pthread_cond_wait(&lock->cond, &lock->mutex);
	pthread_mutex_unlock(&lock->mutex);
	lock_log(resource);
	return ret;
}


int vres_lock_timeout(vres_t *resource, vres_time_t timeout)
{
	int ret = 0;
	vres_lock_t *lock;

	if (!(lock_stat & VRES_STAT_INIT)) {
		log_err("invalid state");
		return -EINVAL;
	}

	lock = vres_lock_get(resource);
	if (!lock) {
		vres_log_err(resource, "no entry");
		return -ENOENT;
	}

	if (lock->count > 1) {
		struct timespec time;

		vres_set_timeout(&time, timeout);
		ret = pthread_cond_timedwait(&lock->cond, &lock->mutex, &time);
		if (ret)
			lock->count--;
	}
	pthread_mutex_unlock(&lock->mutex);
	lock_log(resource);
	return ret > 0 ? -ret : ret;
}


void vres_unlock(vres_t *resource)
{
	int empty = 0;
	vres_lock_t *lock;
	vres_lock_desc_t desc;
	vres_lock_group_t *grp;

	if (!(lock_stat & VRES_STAT_INIT)) {
		log_err("invalid state");
		return;
	}

	vres_lock_get_desc(resource, &desc);
	grp = &lock_group[vres_lock_hash(&desc)];
	pthread_mutex_lock(&grp->mutex);
	lock = vres_lock_lookup(grp, &desc);
	if (!lock) {
		vres_log_err(resource, "cannot find the lock");
		goto out;
	}

	pthread_mutex_lock(&lock->mutex);
	if (lock->count > 0) {
		lock->count--;
		if (lock->count > 0) {
			if (list_empty(&lock->list))
				pthread_cond_signal(&lock->cond);
			else
				pthread_cond_broadcast(&lock->cond);
		} else
			empty = 1;

		if (!list_empty(&lock->list)) {
			vres_lock_list_t *curr = list_entry(lock->list.next, vres_lock_list_t, list);

			if (pthread_self() == curr->tid) {
				list_del(&curr->list);
				free(curr);
			} else
				log_err("found a mismatched lock (tid=%lx)", pthread_self());

			if (empty && !list_empty(&lock->list))
				log_err("failed to clear lock list");
		}
	}
	pthread_mutex_unlock(&lock->mutex);
	if (empty)
		vres_lock_free(grp, lock);
out:
	pthread_mutex_unlock(&grp->mutex);
	lock_log(resource);
}
