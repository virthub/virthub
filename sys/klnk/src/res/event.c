/* event.c
 *
 * Copyright (C) 2017 Yi-Wei Ci
 *
 * Distributed under the terms of the MIT license.
 */

#include "event.h"

int event_stat = 0;
vres_event_group_t event_group[VRES_EVENT_GROUP_MAX];

void vres_event_init()
{
	int i;

	if (event_stat & VRES_STAT_INIT)
		return;

	for (i = 0; i < VRES_EVENT_GROUP_MAX; i++) {
		pthread_mutex_init(&event_group[i].mutex, NULL);
		event_group[i].head = rbtree_create();
	}
	event_stat |= VRES_STAT_INIT;
}


static inline unsigned long vres_event_hash(vres_event_desc_t *desc)
{
	unsigned long *ent = desc->entry;

	return (ent[0] ^ ent[1] ^ ent[2] ^ ent[3]) % VRES_EVENT_GROUP_MAX;
}


static inline int vres_event_compare(char *e1, char *e2)
{
	vres_event_desc_t *desc1 = (vres_event_desc_t *)e1;
	vres_event_desc_t *desc2 = (vres_event_desc_t *)e2;

	return memcmp(desc1->entry, desc2->entry, sizeof(((vres_event_desc_t *)0)->entry));
}


static inline void vres_event_get_desc(vres_t *resource, vres_event_desc_t *desc)
{
	desc->entry[0] = resource->key;
	desc->entry[1] = resource->cls;
	desc->entry[2] = resource->owner;
	desc->entry[3] = vres_get_off(resource);
}


static inline int vres_event_get_index(vres_event_desc_t *desc)
{
	return desc->entry[3];
}


static vres_event_t *vres_event_alloc(vres_event_desc_t *desc)
{
	vres_event_t *event = (vres_event_t *)malloc(sizeof(vres_event_t));

	if (!event)
		return NULL;

	event->buf = NULL;
	event->flags = 0;
	event->length = 0;
	memcpy(&event->desc, desc, sizeof(vres_event_desc_t));
	pthread_cond_init(&event->cond, NULL);
	return event;
}


static inline vres_event_t *vres_event_lookup(vres_event_group_t *group, vres_event_desc_t *desc)
{
	return rbtree_lookup(group->head, (void *)desc, vres_event_compare);
}


static inline void vres_event_insert(vres_event_group_t *group, vres_event_t *event)
{
	rbtree_insert(group->head, (void *)&event->desc, (void *)event, vres_event_compare);
}


static inline void vres_event_free(vres_event_group_t *group, vres_event_t *event)
{
	rbtree_delete(group->head, (void *)&event->desc, vres_event_compare);
	pthread_cond_destroy(&event->cond);
	free(event);
}


int vres_event_wait(vres_t *resource, char *buf, size_t length, struct timespec *timeout)
{
	int ret = 0;
	vres_event_t *event;
	vres_event_desc_t desc;
	vres_event_group_t *grp;

	if (!(event_stat & VRES_STAT_INIT)) {
		vres_log_err(resource, "invalid state");
		return -EINVAL;
	}

	vres_event_get_desc(resource, &desc);
	grp = &event_group[vres_event_hash(&desc)];
	pthread_mutex_lock(&grp->mutex);
	event = vres_event_lookup(grp, &desc);
	if (event) {
		if (event->buf) {
			if (event->length == length)
				memcpy(buf, event->buf, length);
			free(event->buf);
		}
	} else {
		event = vres_event_alloc(&desc);
		if (!event) {
			vres_log_err(resource, "no memory");
			ret = -EINVAL;
			goto out;
		}

		event->buf = buf;
		event->length = length;
		event->flags |= VRES_EVENT_BUSY;
		vres_event_insert(grp, event);
		if (timeout)
			ret = pthread_cond_timedwait(&event->cond, &grp->mutex, timeout);
		else
			ret = pthread_cond_wait(&event->cond, &grp->mutex);
	}

	if (!ret && (event->flags & VRES_EVENT_CANCEL))
		ret = -EINTR;
	vres_event_free(grp, event);
out:
	pthread_mutex_unlock(&grp->mutex);
	return ret > 0 ? -ret : ret;
}


int vres_event_set(vres_t *resource, char *buf, size_t length)
{
	int ret = 0;
	vres_event_t *event;
	vres_event_desc_t desc;
	vres_event_group_t *grp;

	if (!(event_stat & VRES_STAT_INIT)) {
		vres_log_err(resource, "invalid state");
		return -EINVAL;
	}

	vres_event_get_desc(resource, &desc);
	grp = &event_group[vres_event_hash(&desc)];
	pthread_mutex_lock(&grp->mutex);
	event = vres_event_lookup(grp, &desc);
	if (!event) {
		char *tmp = malloc(length);

		if (!tmp) {
			vres_log_err(resource, "no memory");
			ret = -ENOMEM;
			goto out;
		}

		event = vres_event_alloc(&desc);
		if (!event) {
			vres_log_err(resource, "no memory");
			ret = -ENOMEM;
			free(tmp);
			goto out;
		}

		event->buf = tmp;
		event->length = length;
		memcpy(event->buf, buf, length);
		vres_event_insert(grp, event);
	} else {
		if (event->buf && (event->length == length))
			memcpy(event->buf, buf, length);
		pthread_cond_signal(&event->cond);
	}
out:
	pthread_mutex_unlock(&grp->mutex);
	return ret;
}


int vres_event_get(vres_t *resource, vres_index_t *index)
{
	int ret = 0;
	vres_event_t *event;
	vres_event_desc_t desc;
	vres_event_group_t *grp;

	if (!(event_stat & VRES_STAT_INIT)) {
		vres_log_err(resource, "invalid state");
		return -EINVAL;
	}

	vres_event_get_desc(resource, &desc);
	grp = &event_group[vres_event_hash(&desc)];
	pthread_mutex_lock(&grp->mutex);
	event = vres_event_lookup(grp, &desc);
	if (!event && vres_can_restart(resource)) {
		int total;
		vres_index_t *events = NULL;

		total = vres_get_events(resource, &events);
		if (total < 0) {
			vres_log_err(resource, "failed to get events");
			ret = -EINVAL;
		} else if (total > 0) {
			char path[VRES_PATH_MAX];

			vres_get_event_path(resource, path);
			total--;
			if (total) {
				tmp_file_t *filp;

				filp = tmp_open(path, "w");
				if (!filp) {
					vres_log_err(resource, "no entry");
					ret = -ENOENT;
				} else {
					if (tmp_write(&events[1], sizeof(vres_index_t), total, filp) != total) {
						vres_log_err(resource, "failed to write");
						ret = -EIO;
					}
					tmp_close(filp);
				}
			} else
				tmp_remove(path);

			if (!ret)
				*index = events[0];
			free(events);
		} else
			ret = -EAGAIN;
	}
	pthread_mutex_unlock(&grp->mutex);
	return ret;
}


int vres_event_do_handle(vres_t *resource, rbtree_node node, vres_event_callback_t func)
{
	int ret;

    if (!node)
        return 0;

    if (node->right) {
        ret = vres_event_do_handle(resource, node->right, func);
		if (ret)
			return ret;
	}

    if (node->left) {
        ret = vres_event_do_handle(resource, node->left, func);
		if (ret)
			return ret;
	}

	return func(resource, (vres_event_t *)node->value);
}


int vres_event_handle(vres_t *resource, vres_event_callback_t func)
{
	int i;

	for (i = 0; i < VRES_EVENT_GROUP_MAX; i++) {
		int ret;
		vres_event_group_t *grp;

		grp = &event_group[i];
		pthread_mutex_lock(&grp->mutex);
		ret = vres_event_do_handle(resource, grp->head->root, func);
		pthread_mutex_unlock(&grp->mutex);
		if (ret) {
			vres_log_err(resource, "failed to handle");
			return ret;
		}
	}

	return 0;
}


int vres_event_do_cancel(vres_t *resource, vres_event_t *event)
{
	int ret = 0;
	unsigned long *ent = event->desc.entry;

	if ((resource->key == ent[0]) && (resource->cls == ent[1]) && (resource->owner == ent[2])) {
		tmp_entry_t *entry;
		char path[VRES_PATH_MAX];

		vres_get_event_path(resource, path);
		entry = tmp_get_entry(path, 0, TMP_RDWR | TMP_CREAT);
		if (entry) {
			ret = tmp_append(entry, &ent[3], sizeof(int));
			if (ret)
				vres_log_err(resource, "failed to append");
			tmp_put_entry(entry);
		} else {
			vres_log_err(resource, "no entry");
			ret = -ENOENT;
		}

		if (!ret) {
			event->flags |= VRES_EVENT_CANCEL;
			pthread_cond_signal(&event->cond);
			log_event_cancel(path, (int)ent[3]);
		}
	}

	return ret;
}


int vres_event_cancel(vres_t *resource)
{
	if (!(event_stat & VRES_STAT_INIT)) {
		vres_log_err(resource, "invalid state");
		return -EINVAL;
	}

	return vres_event_handle(resource, vres_event_do_cancel);
}


int vres_event_exists(vres_t *resource)
{
	int ret = 0;
	vres_event_t *event;
	vres_event_desc_t desc;
	vres_event_group_t *grp;

	if (!(event_stat & VRES_STAT_INIT)) {
		vres_log_err(resource, "invalid state");
		return 0;
	}

	vres_event_get_desc(resource, &desc);
	grp = &event_group[vres_event_hash(&desc)];
	pthread_mutex_lock(&grp->mutex);
	event = vres_event_lookup(grp, &desc);
	if (event && (event->flags & VRES_EVENT_BUSY))
		ret = 1;
	pthread_mutex_unlock(&grp->mutex);
	return ret;
}
