/* page.c
 *
 * Copyright (C) 2019 Yi-Wei Ci
 *
 * Distributed under the terms of the MIT license.
 */

#include "page.h"

int vres_page_lock_stat = 0;
vres_page_lock_group_t vres_page_lock_group[VRES_PAGE_LOCK_GROUP_SIZE];

int vres_page_lock_compare(const void *val0, const void *val1)
{
    vres_page_lock_entry_t *ent0 = ((vres_page_lock_desc_t *)val0)->entry;
    vres_page_lock_entry_t *ent1 = ((vres_page_lock_desc_t *)val1)->entry;
    const size_t size = VRES_PAGE_LOCK_ENTRY_SIZE * sizeof(vres_page_lock_entry_t);

    return memcmp(ent0, ent1, size);
}


void vres_page_lock_init()
{
    int i;

    if (vres_page_lock_stat & VRES_STAT_INIT)
        return;
    for (i = 0; i < VRES_PAGE_LOCK_GROUP_SIZE; i++) {
        pthread_mutex_init(&vres_page_lock_group[i].mutex, NULL);
        rbtree_new(&vres_page_lock_group[i].tree, vres_page_lock_compare);
    }
    vres_page_lock_stat |= VRES_STAT_INIT;
}


static inline unsigned long vres_page_lock_hash(vres_page_lock_desc_t *desc)
{
    vres_page_lock_entry_t *ent = desc->entry;

    assert(VRES_PAGE_LOCK_ENTRY_SIZE == 2);
    return (ent[0] ^ ent[1]) % VRES_PAGE_LOCK_GROUP_SIZE;
}


static inline void vres_page_lock_get_desc(vres_t *resource, vres_page_lock_desc_t *desc)
{
    desc->entry[0] = resource->key;
    desc->entry[1] = vres_get_off(resource);
}


static inline vres_page_lock_t *vres_page_lock_alloc(vres_page_lock_desc_t *desc)
{
    vres_page_lock_t *lock = (vres_page_lock_t *)malloc(sizeof(vres_page_lock_t));

    if (lock) {
        lock->count = 0;
        memcpy(&lock->desc, desc, sizeof(vres_page_lock_desc_t));
        pthread_mutex_init(&lock->mutex, NULL);
        pthread_cond_init(&lock->cond, NULL);
    }
    return lock;
}


static inline vres_page_lock_t *vres_page_lock_lookup(vres_page_lock_group_t *group, vres_page_lock_desc_t *desc)
{
    rbtree_node_t *node = NULL;

    if (!rbtree_find(&group->tree, desc, &node))
        return tree_entry(node, vres_page_lock_t, node);
    else
        return NULL;
}


static inline void vres_page_lock_insert(vres_page_lock_group_t *group, vres_page_lock_t *lock)
{
    rbtree_insert(&group->tree, &lock->desc, &lock->node);
}


static inline vres_page_lock_t *vres_page_lock_get(vres_t *resource)
{
    vres_page_lock_desc_t desc;
    vres_page_lock_group_t *grp;
    vres_page_lock_t *lock = NULL;

    vres_page_lock_get_desc(resource, &desc);
    grp = &vres_page_lock_group[vres_page_lock_hash(&desc)];
    pthread_mutex_lock(&grp->mutex);
    lock = vres_page_lock_lookup(grp, &desc);
    if (!lock) {
        lock = vres_page_lock_alloc(&desc);
        if (!lock) {
            log_resource_err(resource, "no memory");
            goto out;
        }
        vres_page_lock_insert(grp, lock);
    }
    pthread_mutex_lock(&lock->mutex);
    lock->count++;
out:
    pthread_mutex_unlock(&grp->mutex);
    return lock;
}


static void vres_page_lock_free(vres_page_lock_group_t *group, vres_page_lock_t *lock)
{
    rbtree_remove(&group->tree, &lock->desc);
    pthread_mutex_destroy(&lock->mutex);
    pthread_cond_destroy(&lock->cond);
    free(lock);
}


static int vres_page_lock(vres_t *resource)
{
    int ret = 0;
    vres_page_lock_t *lock;

    if (!(vres_page_lock_stat & VRES_STAT_INIT))
        return -EINVAL;
    lock = vres_page_lock_get(resource);
    if (!lock) {
        log_resource_err(resource, "no entry");
        return -ENOENT;
    }
    if (lock->count > 1)
        pthread_cond_wait(&lock->cond, &lock->mutex);
    pthread_mutex_unlock(&lock->mutex);
    log_page_lock(resource);
    return ret;
}


static void vres_page_unlock(vres_t *resource)
{
    int empty = 0;
    vres_page_lock_t *lock;
    vres_page_lock_desc_t desc;
    vres_page_lock_group_t *grp;

    if (!(vres_page_lock_stat & VRES_STAT_INIT))
        return;
    vres_page_lock_get_desc(resource, &desc);
    grp = &vres_page_lock_group[vres_page_lock_hash(&desc)];
    pthread_mutex_lock(&grp->mutex);
    lock = vres_page_lock_lookup(grp, &desc);
    if (!lock) {
        log_resource_err(resource, "cannot find the lock");
        pthread_mutex_unlock(&grp->mutex);
        return;
    }
    pthread_mutex_lock(&lock->mutex);
    if (lock->count > 0) {
        lock->count--;
        if (lock->count > 0)
            pthread_cond_signal(&lock->cond);
        else
            empty = 1;
    }
    pthread_mutex_unlock(&lock->mutex);
    if (empty)
        vres_page_lock_free(grp, lock);
    pthread_mutex_unlock(&grp->mutex);
    log_page_unlock(resource);
}


void *vres_page_get(vres_t *resource, vres_page_t **page, int flags)
{
    vres_file_entry_t *entry;
    char path[VRES_PATH_MAX];

    vres_get_data_path(resource, path);
    vres_page_lock(resource);
    entry = vres_file_get_entry(path, sizeof(vres_page_t), flags & (VRES_RDWR | VRES_RDONLY | VRES_CREAT));
    if (!entry) {
        vres_page_unlock(resource);
        return NULL;
    }
    *page = vres_file_get_desc(entry, vres_page_t);
    log_page_get(resource, path);
    return entry;
}


void vres_page_put(vres_t *resource, void *entry)
{
    vres_file_put_entry((vres_file_entry_t *)entry);
    vres_page_unlock(resource);
    log_page_put(resource);
}


int vres_page_update(vres_page_t *page, char *buf)
{
    int i, j;
    int ret = 0;
    bool dirty = false;
    int diff[VRES_LINE_MAX];
    vres_digest_t digest[VRES_LINE_MAX];

    for (i = 0; i < VRES_LINE_MAX; i++)
        digest[i] = vres_line_get_digest(&buf[i * VRES_LINE_SIZE]);
    for (i = 0; i < VRES_LINE_MAX; i++) {
        j = i * VRES_LINE_SIZE;
        if (page->digest[i] != digest[i]) {
            dirty = true;
            diff[i] = 1;
            memcpy(&page->buf[j], &buf[j], VRES_LINE_SIZE);
        } else {
            if (!memcmp(&page->buf[j], &buf[j], VRES_LINE_SIZE))
                diff[i] = 0;
            else {
                dirty = true;
                diff[i] = 1;
                memcpy(&page->buf[j], &buf[j], VRES_LINE_SIZE);
            }
        }
    }
    if (dirty) {
        for (i = VRES_PAGE_NR_VERSIONS - 1; i > 0; i--)
            for (j = 0; j < VRES_LINE_MAX; j++)
                page->diff[i][j] |= page->diff[i - 1][j] | diff[j];
        for (j = 0; j < VRES_LINE_MAX; j++) {
            page->diff[0][j] = diff[j];
            page->digest[j] = digest[j];
        }
    }
    return ret;
}


vres_index_t vres_page_update_index(vres_page_t *page)
{
    page->index++;
    if (VRES_INDEX_MAX == page->index)
        page->index = 1;
    return page->index;
}


void vres_page_update_candidates(vres_t *resource, vres_page_t *page, vres_id_t *holders, int nr_holders)
{
    int i, j, k = 0;
    int total = page->nr_candidates;
    vres_id_t nu[VRES_PAGE_NR_HOLDERS];
    vres_member_t *cand = page->candidates;

    for (i = 0; i < nr_holders; i++) {
        if (holders[i] == resource->owner)
            continue;
        for (j = 0; j < total; j++) {
            if (holders[i] == cand[j].id) {
                if (cand[j].count < VRES_PAGE_NR_HITS) {
                    cand[j].count++;
                    while ((j > 0) && (cand[j].count > cand[j - 1].count)) {
                        vres_member_t tmp = cand[j - 1];
                        cand[j - 1] = cand[j];
                        cand[j] = tmp;
                        j--;
                    }
                }
                break;
            }
        }
        if (j == total) {
            nu[k] = holders[i];
            k++;
        }
    }
    if (cand[0].count == VRES_PAGE_NR_HITS) {
        for (i = 0; i < total; i++) {
            if (cand[i].count > 0)
                cand[i].count--;
            else
                break;
        }
    }
    if (k > 0) {
        if (!total || ((cand[total - 1].count > 0) && (total < VRES_PAGE_NR_HOLDERS)))
            j = total;
        else {
            if (cand[total - 1].count > 0)
                for (j = 0; j < total; j++)
                    cand[j].count--;
            if (0 == cand[total - 1].count) {
                for (j = total - 1; j > 0; j--)
                    if (cand[j - 1].count > 0)
                        break;
            } else
                return;
        }
        if (j + k > total)
            page->nr_candidates = min(j + k, VRES_PAGE_NR_CANDIDATES);
        for (i = 0; (i < k) && (j < page->nr_candidates); i++, j++) {
            cand[j].id = nu[i];
            cand[j].count = 1;
        }
    }
}


int vres_page_get_diff(vres_page_t *page, vres_version_t version, int *diff)
{
    int interval = 0;
    const size_t size = VRES_LINE_MAX * sizeof(int);

    if (page->version >= version)
        interval = page->version - version;
    else
        return -EINVAL;

    if (0 == interval)
        memset(diff, 0, size);
    else if (interval <= VRES_PAGE_NR_VERSIONS)
        memcpy(diff, &page->diff[interval - 1], size);
    else {
        int i;

        for (i = 0; i < VRES_LINE_MAX; i++)
            diff[i] = 1;
    }
    return 0;
}


int vres_page_calc_holders(vres_page_t *page)
{
    return page->nr_holders + page->nr_silent_holders;
}


int vres_page_search_holder_list(vres_t *resource, vres_page_t *page, vres_id_t id)
{
    int i;
    int nr_holders = page->nr_holders;
    int nr_silent_holders = page->nr_silent_holders;

    for (i = 0; i < nr_holders; i++)
        if (page->holders[i] == id)
            return 1;
    if (nr_silent_holders > 0) {
        vres_file_entry_t *entry;
        char path[VRES_PATH_MAX];

        vres_get_holder_path(resource, path);
        entry = vres_file_get_entry(path, nr_silent_holders * sizeof(vres_id_t), FILE_RDONLY);
        if (entry) {
            int ret = 0;
            vres_id_t *holders;

            holders = vres_file_get_desc(entry, vres_id_t);
            for (i = 0; i < nr_silent_holders; i++) {
                if (holders[i] == id) {
                    ret = 1;
                    break;
                }
            }
            vres_file_put_entry(entry);
            return ret;
        }
    }
    return 0;
}


int vres_page_update_holder_list(vres_t *resource, vres_page_t *page, vres_id_t *holders, int nr_holders)
{
    int total;
    int ret = 0;
    int nr_silent_holders;

    if (!nr_holders)
        return 0;

    total = page->nr_holders + nr_holders;
    nr_silent_holders = total > VRES_PAGE_NR_HOLDERS ? total - VRES_PAGE_NR_HOLDERS : 0;
    nr_holders -= nr_silent_holders;

    if (nr_holders > 0)
        memcpy(&page->holders[page->nr_holders], holders, nr_holders * sizeof(vres_id_t));
    if (nr_silent_holders > 0) {
        vres_file_t *filp;
        char path[VRES_PATH_MAX];

        vres_get_holder_path(resource, path);
        filp = vres_file_open(path, "r+");
        if (!filp) {
            filp = vres_file_open(path, "w");
            if (!filp) {
                log_resource_err(resource, "no entry");
                return -ENOENT;
            }
        }
        if (vres_file_size(filp) / sizeof(vres_id_t) != page->nr_silent_holders) {
            log_resource_err(resource, "invalid file length");
            ret = -EINVAL;
            goto out;
        }
        vres_file_seek(filp, 0, SEEK_END);
        if (vres_file_write(&holders[nr_holders], sizeof(vres_id_t), nr_silent_holders, filp) != nr_silent_holders) {
            log_resource_err(resource, "failed to update holder list");
            ret = -EIO;
        }
out:
        vres_file_close(filp);
    }
    if (!ret) {
        page->nr_holders += nr_holders;
        page->nr_silent_holders += nr_silent_holders;
    }
    vres_page_update_candidates(resource, page, holders, nr_holders);
    return ret;
}


int vres_page_add_holder(vres_t *resource, vres_page_t *page, vres_id_t id)
{
    return vres_page_update_holder_list(resource, page, &id, 1);
}


void vres_page_clear_holder_list(vres_t *resource, vres_page_t *page)
{
    page->nr_holders = 0;
    if (page->nr_silent_holders > 0) {
        vres_file_t *filp;
        char path[VRES_PATH_MAX];

        vres_get_holder_path(resource, path);
        filp = vres_file_open(path, "r+");
        if (filp) {
            vres_file_truncate(filp, 0);
            vres_file_close(filp);
        }
        page->nr_silent_holders = 0;
    }
}


int vres_page_get_hid(vres_page_t *page, vres_id_t id)
{
    int i;

    for (i = 0; i < page->nr_holders; i++)
        if (id == page->holders[i])
            return i + 1;
    return 0;
}


static int vres_page_wrprotect(vres_t *resource, vres_page_t *page, int pid)
{
    int ret;
    char *buf;

    if (!vres_pg_active(page) || !vres_pg_write(page))
        return 0;
    buf = malloc(PAGE_SIZE);
    if (!buf) {
        log_resource_err(resource, "no memory");
        return -ENOMEM;
    }
    ret = sys_shm_wrprotect(resource->key, pid, vres_get_off(resource), buf);
    if (ret) {
        if (ENOENT == errno) {
            ret = 0;
            goto protect;
        } else
            goto out;
    }
    ret = vres_page_update(page, buf);
    if (ret) {
        log_resource_err(resource, "failed to update");
        goto out;
    }
protect:
    vres_pg_wrprotect(page);
out:
    free(buf);
    return ret;
}


static int vres_page_rdprotect(vres_t *resource, vres_page_t *page, int pid)
{
    int ret = 0;
    char *buf = NULL;

    if (!vres_pg_active(page) || !vres_pg_access(page))
        return 0;
    if (vres_pg_wait(page))
        goto protect;
    if (vres_pg_write(page)) {
        buf = malloc(PAGE_SIZE);
        if (!buf) {
            log_resource_err(resource, "no memory");
            return -ENOMEM;
        }
    }
    ret = sys_shm_rdprotect(resource->key, pid, vres_get_off(resource), buf);
    if (ret) {
        if (ENOENT == errno) {
            ret = 0;
            goto protect;
        } else {
            log_resource_err(resource, "failed to protect (%d)", errno);
            goto out;
        }
    }
    if (buf) {
        ret = vres_page_update(page, buf);
        if (ret) {
            log_resource_err(resource, "failed to update");
            goto out;
        }
    }
protect:
    vres_pg_clractive(page);
    vres_pg_rdprotect(page);
out:
    if (buf)
        free(buf);
    return ret;
}


int vres_page_protect(vres_t *resource, vres_page_t *page)
{
    int ret;
    vres_desc_t desc;
    int flags = vres_get_flags(resource);

    ret = vres_get_peer(resource->owner, &desc);
    if (ret) {
        log_resource_err(resource, "failed to get pid (ret=%s)", log_get_err(ret));
        return -EINVAL;
    }
    if (flags & VRES_RDONLY)
        return vres_page_wrprotect(resource, page, desc.id);
    else if (flags & VRES_RDWR)
        return vres_page_rdprotect(resource, page, desc.id);
    else
        return 0;
}
