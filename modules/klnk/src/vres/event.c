#include "event.h"

int event_stat = 0;
vres_event_group_t event_group[VRES_EVENT_GROUP_MAX];

int vres_event_compare(const void *val0, const void *val1)
{
    vres_event_entry_t *ent0 = ((vres_event_desc_t *)val0)->entry;
    vres_event_entry_t *ent1 = ((vres_event_desc_t *)val1)->entry;
    const size_t size = VRES_EVENT_ENTRY_SIZE * sizeof(vres_event_entry_t);

    return memcmp(ent0, ent1, size);
}


void vres_event_init()
{
    int i;

    if (event_stat & VRES_STAT_INIT)
        return;
    
    for (i = 0; i < VRES_EVENT_GROUP_MAX; i++) {
        pthread_mutex_init(&event_group[i].mutex, NULL);
        rbtree_new(&event_group[i].tree, vres_event_compare);
    }
    event_stat |= VRES_STAT_INIT;
}


static inline unsigned long vres_event_hash(vres_event_desc_t *desc)
{
    vres_event_entry_t *ent = desc->entry;

    assert(VRES_EVENT_ENTRY_SIZE == 3);
    return (ent[0] ^ ent[1] ^ ent[2]) % VRES_EVENT_GROUP_MAX;
}


static inline void vres_event_get_desc(vres_t *resource, vres_event_desc_t *desc)
{
    desc->entry[0] = resource->key;
    desc->entry[1] = resource->cls;
    desc->entry[2] = vres_get_off(resource);
}


static inline int vres_event_get_index(vres_event_desc_t *desc)
{
    return desc->entry[2];
}


static vres_event_t *vres_event_alloc(vres_event_desc_t *desc)
{
    vres_event_t *event = (vres_event_t *)malloc(sizeof(vres_event_t));

    if (!event) {
        log_err("failed to allocate event");
        return NULL;
    }
    event->flags = 0;
    event->length = 0;
    event->buf = NULL;
    memcpy(&event->desc, desc, sizeof(vres_event_desc_t));
    pthread_cond_init(&event->cond, NULL);
    return event;
}


static inline vres_event_t *vres_event_lookup(vres_event_group_t *group, vres_event_desc_t *desc)
{
    rbtree_node_t *node = NULL;

    if (!rbtree_find(&group->tree, desc, &node))
        return tree_entry(node, vres_event_t, node);
    else
        return NULL;
}


static inline void vres_event_insert(vres_event_group_t *group, vres_event_t *event)
{
    rbtree_insert(&group->tree, &event->desc, &event->node);
}


static inline void vres_event_free(vres_event_group_t *group, vres_event_t *event)
{
    rbtree_remove(&group->tree, &event->desc);
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
        log_resource_err(resource, "invalid state");
        return -EINVAL;
    }
    vres_event_get_desc(resource, &desc);
    log_event_wait(resource, desc.entry, VRES_EVENT_ENTRY_SIZE, ">-- wait@start --<");
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
            log_resource_err(resource, "failed to allocate");
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
    log_event_wait(resource, desc.entry, VRES_EVENT_ENTRY_SIZE, ">-- wait@end --<");
    return ret > 0 ? -ret : ret;
}


int vres_event_set(vres_t *resource, char *buf, size_t length)
{
    int ret = 0;
    vres_event_t *event;
    vres_event_desc_t desc;
    vres_event_group_t *grp;

    if (!(event_stat & VRES_STAT_INIT)) {
        log_resource_err(resource, "invalid state");
        return -EINVAL;
    }
    vres_event_get_desc(resource, &desc);
    grp = &event_group[vres_event_hash(&desc)];
    pthread_mutex_lock(&grp->mutex);
    event = vres_event_lookup(grp, &desc);
    if (!event) {
        char *ev_buf = malloc(length);

        if (!ev_buf) {
            log_resource_err(resource, "no memory");
            ret = -ENOMEM;
            goto out;
        }
        event = vres_event_alloc(&desc);
        if (!event) {
            log_resource_err(resource, "no memory");
            ret = -ENOMEM;
            free(ev_buf);
            goto out;
        }
        event->buf = ev_buf;
        event->length = length;
        memcpy(event->buf, buf, length);
        vres_event_insert(grp, event);
        log_event_set(resource, desc.entry, VRES_EVENT_ENTRY_SIZE, "insert");
    } else {
        assert(event->buf && (event->length == length));
        memcpy(event->buf, buf, length);
        pthread_cond_broadcast(&event->cond);
        log_event_set(resource, desc.entry, VRES_EVENT_ENTRY_SIZE, "wakeup");
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
        log_resource_err(resource, "invalid state");
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
            log_resource_err(resource, "failed to get events");
            ret = -EINVAL;
        } else if (total > 0) {
            char path[VRES_PATH_MAX];

            vres_get_event_path(resource, path);
            total--;
            if (total) {
                vres_file_t *filp;

                filp = vres_file_open(path, "w");
                if (!filp) {
                    log_resource_err(resource, "no entry");
                    ret = -ENOENT;
                } else {
                    if (vres_file_write(&events[1], sizeof(vres_index_t), total, filp) != total) {
                        log_resource_err(resource, "failed to write");
                        ret = -EIO;
                    }
                    vres_file_close(filp);
                }
            } else
                vres_file_remove(path);

            if (!ret)
                *index = events[0];
            free(events);
        } else
            ret = -EAGAIN;
    }
    pthread_mutex_unlock(&grp->mutex);
    return ret;
}


int vres_event_do_handle(vres_t *resource, rbtree_node_t *node, vres_event_callback_t func)
{
    int ret;
    vres_event_t *event;

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
    event = tree_entry(node, vres_event_t, node);
    return func(resource, event);
}


int vres_event_handle(vres_t *resource, vres_event_callback_t func)
{
    int i;

    for (i = 0; i < VRES_EVENT_GROUP_MAX; i++) {
        int ret;
        vres_event_group_t *grp;

        grp = &event_group[i];
        pthread_mutex_lock(&grp->mutex);
        ret = vres_event_do_handle(resource, grp->tree.root, func);
        pthread_mutex_unlock(&grp->mutex);
        if (ret) {
            log_resource_err(resource, "failed to handle");
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
        vres_file_entry_t *entry;
        char path[VRES_PATH_MAX];

        vres_get_event_path(resource, path);
        entry = vres_file_get_entry(path, 0, FILE_RDWR | FILE_CREAT);
        if (entry) {
            ret = vres_file_append(entry, &ent[3], sizeof(int));
            if (ret)
                log_resource_err(resource, "failed to append");
            vres_file_put_entry(entry);
        } else {
            log_resource_err(resource, "no entry");
            ret = -ENOENT;
        }
        if (!ret) {
            event->flags |= VRES_EVENT_CANCEL;
            pthread_cond_broadcast(&event->cond);
            log_event_cancel(path, (int)ent[3]);
        }
    }
    return ret;
}


int vres_event_cancel(vres_t *resource)
{
    if (!(event_stat & VRES_STAT_INIT)) {
        log_resource_err(resource, "invalid state");
        return -EINVAL;
    }
    return vres_event_handle(resource, vres_event_do_cancel);
}


bool vres_event_exists(vres_t *resource)
{
    bool ret = false;
    vres_event_t *event;
    vres_event_desc_t desc;
    vres_event_group_t *grp;

    if (!(event_stat & VRES_STAT_INIT)) {
        log_resource_err(resource, "invalid state");
        return ret;
    }
    vres_event_get_desc(resource, &desc);
    grp = &event_group[vres_event_hash(&desc)];
    pthread_mutex_lock(&grp->mutex);
    event = vres_event_lookup(grp, &desc);
    if (event && (event->flags & VRES_EVENT_BUSY))
        ret = true;
    pthread_mutex_unlock(&grp->mutex);
    return ret;
}
