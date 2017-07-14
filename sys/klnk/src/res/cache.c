/* cache.c
 *
 * Copyright (C) 2017 Yi-Wei Ci
 *
 * Distributed under the terms of the MIT license.
 */

#include "cache.h"

int cache_stat = 0;
vres_cache_group_t cache_group[VRES_CACHE_GROUP_SIZE];

void vres_cache_init()
{
	int i;

	if (cache_stat & VRES_STAT_INIT)
		return;

	for (i = 0; i < VRES_CACHE_GROUP_SIZE; i++) {
		pthread_mutex_init(&cache_group[i].mutex, NULL);
		cache_group[i].head = rbtree_create();
		INIT_LIST_HEAD(&cache_group[i].list);
		cache_group[i].count = 0;
	}
	cache_stat |= VRES_STAT_INIT;
}


static inline int vres_cache_compare(char *c1, char *c2)
{
	vres_cache_desc_t *desc1 = (vres_cache_desc_t *)c1;
	vres_cache_desc_t *desc2 = (vres_cache_desc_t *)c2;

	return memcmp(desc1->entry, desc2->entry, sizeof(((vres_cache_desc_t *)0)->entry));
}


static inline unsigned long vres_cache_hash(vres_cache_desc_t *desc)
{
	unsigned long *ent = desc->entry;

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
	return rbtree_lookup(group->head, desc, vres_cache_compare);
}


static inline void vres_cache_free(vres_cache_group_t *group, vres_cache_t *cache)
{
	rbtree_delete(group->head, (void *)&cache->desc, vres_cache_compare);
	list_del(&cache->list);
	free(cache->buf);
	free(cache);
	group->count--;
}


static inline void vres_cache_insert(vres_cache_group_t *group, vres_cache_t *cache)
{
	rbtree_insert(group->head, &cache->desc, (void *)cache, vres_cache_compare);
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
			vres_log_err(resource, "no memory");
			ret = -ENOMEM;
			goto out;
		}
		vres_cache_insert(grp, cache);
		if (VRES_CACHE_QUEUE_SIZE == grp->count)
			vres_cache_free(grp, list_entry(grp->list.prev, vres_cache_t, list));
	} else {
		ret = vres_cache_realloc(cache, len);
		if (ret) {
			vres_log_err(resource, "failed to write cache");
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
