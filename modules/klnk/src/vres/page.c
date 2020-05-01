/* page.c
 *
 * Copyright (C) 2019 Yi-Wei Ci
 *
 * Distributed under the terms of the MIT license.
 */

#include "page.h"

int vres_page_lock_stat = 0;
vres_page_lock_group_t vres_page_lock_group[VRES_PAGE_LOCK_GROUP_SIZE];

void vres_page_mkwrite(vres_t *resource, vres_page_t *page)
{
    int off = vres_get_page_off(resource);

    page->flags[off] &= ~VRES_RDONLY;
    page->flags[off] |= VRES_RDWR;
    page->chunks[vres_get_chunk(resource)].writable = true;
}


void vres_page_mkread(vres_t *resource, vres_page_t *page)
{
    int off = vres_get_page_off(resource);

    page->flags[off] &= ~VRES_RDWR;
    page->flags[off] |= VRES_RDONLY;
}


void vres_page_mkpresent(vres_t *resource, vres_page_t *page)
{
    page->flags[vres_get_page_off(resource)] |= VRES_PRESENT;
}


void vres_page_mkdump(vres_t *resource, vres_page_t *page)
{
    page->flags[vres_get_page_off(resource)] |= VRES_DUMP;
}


void vres_page_mkwait(vres_t *resource, vres_page_t *page)
{
    page->chunks[vres_get_chunk(resource)].wait = true;
}


void vres_page_mkcand(vres_t *resource, vres_page_t *page)
{
    page->chunks[vres_get_chunk(resource)].cand = true;
}


void vres_page_mkactive(vres_t *resource, vres_page_t *page)
{
    page->chunks[vres_get_chunk(resource)].active = true;
}


void vres_page_clractive(vres_t *resource, vres_page_t *page)
{
    page->chunks[vres_get_chunk(resource)].active = false;
}


void vres_page_clrwritable(vres_t *resource, vres_page_t *page)
{
    page->chunks[vres_get_chunk(resource)].writable = false;
}


void vres_page_clrwait(vres_t *resource, vres_page_t *page)
{
    page->chunks[vres_get_chunk(resource)].wait = false;
}


void vres_page_clrcand(vres_t *resource, vres_page_t *page)
{
    page->chunks[vres_get_chunk(resource)].cand = false;
}


void vres_page_clrdump(vres_t *resource, vres_page_t *page)
{
    page->flags[vres_get_page_off(resource)] &= ~VRES_DUMP;
}


bool vres_page_active(vres_t *resource, vres_page_t *page)
{
    return page->chunks[vres_get_chunk(resource)].active;
}


bool vres_page_is_writable(vres_t *resource, vres_page_t *page)
{
    return page->chunks[vres_get_chunk(resource)].writable;
}


bool vres_page_cand(vres_t *resource, vres_page_t *page)
{
    return page->chunks[vres_get_chunk(resource)].cand;
}


bool vres_page_wait(vres_t *resource, vres_page_t *page)
{
    return page->chunks[vres_get_chunk(resource)].wait;
}


bool vres_page_dump(vres_t *resource, vres_page_t *page)
{
    return (page->flags[vres_get_page_off(resource)] & VRES_DUMP) != 0;
}


bool vres_page_write(vres_t *resource, vres_page_t *page)
{
    return (page->flags[vres_get_page_off(resource)] & VRES_RDWR) != 0;
}


bool vres_page_present(vres_t *resource, vres_page_t *page)
{
    return (page->flags[vres_get_page_off(resource)] & VRES_PRESENT) != 0;
}


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
    assert(VRES_LINE_SIZE <= PAGE_SIZE);
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
    desc->entry[1] = vres_get_pgno(resource);
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
            log_resource_warning(resource, "no memory");
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
    log_page_lock(resource);
    lock = vres_page_lock_get(resource);
    if (!lock) {
        log_resource_warning(resource, "no entry");
        return -ENOENT;
    }
    if (lock->count > 1)
        pthread_cond_wait(&lock->cond, &lock->mutex);
    pthread_mutex_unlock(&lock->mutex);
    return ret;
}


static void vres_page_unlock(vres_t *resource)
{
    bool empty = false;
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
        log_resource_warning(resource, "cannot find the lock");
        pthread_mutex_unlock(&grp->mutex);
        return;
    }
    pthread_mutex_lock(&lock->mutex);
    if (lock->count > 0) {
        lock->count--;
        if (lock->count > 0)
            pthread_cond_signal(&lock->cond);
        else
            empty = true;
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
        if (flags & VRES_CREAT)
            log_resource_warning(resource, "failed to get page, path=%s", path);
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


int vres_page_update(vres_t *resource, vres_page_t *page, unsigned long off, char *buf)
{
    int i, j, n;
    int ret = 0;
    bool diff[VRES_PAGE_LINES];
    vres_digest_t digest[VRES_PAGE_LINES];
    unsigned long pos = vres_get_chunk(resource);
    char *dest = &page->chunks[pos].buf[off << PAGE_SHIFT];
    unsigned long start = off << (VRES_LINE_SHIFT - VRES_CHUNK_SHIFT);

    if (buf) {
        for (i = 0, n = start, j = 0; i < VRES_PAGE_LINES; i++, n++, j += VRES_LINE_SIZE) {
            digest[i] = vres_line_get_digest(&buf[j]);
            if (page->chunks[pos].digest[n] != digest[i]) {
                diff[i] = true;
                memcpy(&dest[j], &buf[j], VRES_LINE_SIZE);
            } else {
                if (!memcmp(&dest[j], &buf[j], VRES_LINE_SIZE))
                    diff[i] = false;
                else {
                    memcpy(&dest[j], &buf[j], VRES_LINE_SIZE);
                    diff[i] = true;
                }
            }
        }
        for (i = VRES_PAGE_NR_VERSIONS - 1; i > 0; i--)
            for (j = 0, n = start; j < VRES_PAGE_LINES; j++, n++)
                page->chunks[pos].diff[i][n] = page->chunks[pos].diff[i - 1][n] | diff[j];
        for (i = 0, n = start; i < VRES_PAGE_LINES; i++, n++) {
            page->chunks[pos].diff[0][n] = diff[i];
            page->chunks[pos].digest[n] = digest[i];
        }
    } else {
        for (i = VRES_PAGE_NR_VERSIONS - 1; i > 0; i--)
            for (n = start; n < start + VRES_PAGE_LINES; n++)
                page->chunks[pos].diff[i][n] = page->chunks[pos].diff[i - 1][n];
        for (n = start; n < start + VRES_PAGE_LINES; n++)
            page->chunks[pos].diff[0][n] = false;
    }
    return ret;
}


vres_index_t vres_page_alloc_index(vres_chunk_t *chunk)
{
    chunk->index++;
    if (VRES_INDEX_MAX == chunk->index)
        chunk->index = 1;
    return chunk->index;
}


void vres_page_update_candidates(vres_t *resource, vres_member_t *candidates, int *nr_candidates, vres_id_t *holders, int nr_holders)
{
    int i, j, k = 0;
    int total = *nr_candidates;
    vres_id_t nu[VRES_PAGE_NR_HOLDERS];

    for (i = 0; i < nr_holders; i++) {
        if (holders[i] == resource->owner)
            continue;
        for (j = 0; j < total; j++) {
            if (holders[i] == candidates[j].id) {
                if (candidates[j].count < VRES_PAGE_NR_HITS) {
                    candidates[j].count++;
                    while ((j > 0) && (candidates[j].count > candidates[j - 1].count)) {
                        vres_member_t tmp = candidates[j - 1];

                        candidates[j - 1] = candidates[j];
                        candidates[j] = tmp;
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
    if (candidates[0].count == VRES_PAGE_NR_HITS) {
        int idle = 0;

        for (i = 0; i < total; i++) {
            if (candidates[i].count > 0)
                candidates[i].count--;
            if (candidates[i].count < 1)
                idle++;
        }
        total -= idle;
        *nr_candidates = total;
    }
    if (k > 0) {
        j = total;
        total = min(j + k, VRES_PAGE_NR_CANDIDATES);
        for (i = 0; (i < k) && (j < total); i++, j++) {
            candidates[j].id = nu[i];
            candidates[j].count = 1;
        }
        *nr_candidates = total;
    }
}


int vres_page_get_diff(vres_chunk_t *chunk, vres_version_t version, bool *diff)
{
    int interval = 0;
    const size_t size = VRES_LINE_MAX * sizeof(bool);

    if (chunk->version >= version)
        interval = chunk->version - version;
    else
        return -EINVAL;
    if (0 == interval)
        memset(diff, 0, size);
    else if (interval <= VRES_PAGE_NR_VERSIONS)
        memcpy(diff, &chunk->diff[interval - 1], size);
    else {
        int i;

        for (i = 0; i < VRES_LINE_MAX; i++)
            diff[i] = true;
    }
    return 0;
}


int vres_page_calc_holders(vres_chunk_t *chunk)
{
    return chunk->nr_holders + chunk->nr_silent_holders;
}


bool vres_page_search_holder_list(vres_t *resource, vres_chunk_t *chunk, vres_id_t id)
{
    int i;
    int nr_holders = chunk->nr_holders;
    int nr_silent_holders = chunk->nr_silent_holders;

    for (i = 0; i < nr_holders; i++)
        if (chunk->holders[i] == id)
            return true;
    
    if (nr_silent_holders > 0) {
        vres_file_entry_t *entry;
        char path[VRES_PATH_MAX];

        vres_get_holder_path(resource, path);
        entry = vres_file_get_entry(path, nr_silent_holders * sizeof(vres_id_t), FILE_RDONLY);
        if (entry) {
            bool ret = false;
            vres_id_t *holders;

            holders = vres_file_get_desc(entry, vres_id_t);
            for (i = 0; i < nr_silent_holders; i++) {
                if (holders[i] == id) {
                    ret = true;
                    break;
                }
            }
            vres_file_put_entry(entry);
            return ret;
        }
    }
    return false;
}


int vres_page_update_holder_list(vres_t *resource, vres_page_t *page, vres_chunk_t *chunk, vres_id_t *holders, int nr_holders)
{
    int total;
    int ret = 0;
    int nr_silent_holders;

    if (!nr_holders)
        return 0;

    total = chunk->nr_holders + nr_holders;
    nr_silent_holders = total > VRES_PAGE_NR_HOLDERS ? total - VRES_PAGE_NR_HOLDERS : 0;
    nr_holders -= nr_silent_holders;

    if (nr_holders > 0)
        memcpy(&chunk->holders[chunk->nr_holders], holders, nr_holders * sizeof(vres_id_t));
    
    if (nr_silent_holders > 0) {
        vres_file_t *filp;
        char path[VRES_PATH_MAX];

        vres_get_holder_path(resource, path);
        filp = vres_file_open(path, "r+");
        if (!filp) {
            filp = vres_file_open(path, "w");
            if (!filp) {
                log_resource_warning(resource, "no entry");
                return -ENOENT;
            }
        }
        if (vres_file_size(filp) / sizeof(vres_id_t) != chunk->nr_silent_holders) {
            log_resource_warning(resource, "invalid file length");
            ret = -EINVAL;
            goto out;
        }
        vres_file_seek(filp, 0, SEEK_END);
        if (vres_file_write(&holders[nr_holders], sizeof(vres_id_t), nr_silent_holders, filp) != nr_silent_holders) {
            log_resource_warning(resource, "failed to update holder list");
            ret = -EIO;
        }
out:
        vres_file_close(filp);
    }
    if (!ret) {
        chunk->nr_holders += nr_holders;
        chunk->nr_silent_holders += nr_silent_holders;
        vres_page_update_candidates(resource, chunk->candidates, &chunk->nr_candidates, holders, nr_holders);
        vres_page_update_candidates(resource, page->candidates, &page->nr_candidates, holders, nr_holders);
        log_page_update_holder_list(resource, "holders=%d, silent_holders=%d, page_candidates=%d, chunk_candidates=%d", chunk->nr_holders, chunk->nr_silent_holders, page->nr_candidates, chunk->nr_candidates);
    }
    return ret;
}


int vres_page_add_holder(vres_t *resource, vres_page_t *page, vres_chunk_t *chunk, vres_id_t id)
{
    return vres_page_update_holder_list(resource, page, chunk, &id, 1);
}


void vres_page_clear_holder_list(vres_t *resource, vres_chunk_t *chunk)
{
    chunk->nr_holders = 0;
    if (chunk->nr_silent_holders > 0) {
        vres_file_t *filp;
        char path[VRES_PATH_MAX];

        vres_get_holder_path(resource, path);
        filp = vres_file_open(path, "r+");
        if (filp) {
            vres_file_truncate(filp, 0);
            vres_file_close(filp);
        }
        chunk->nr_silent_holders = 0;
    }
}


int vres_page_get_hid(vres_chunk_t *chunk, vres_id_t id)
{
    int i;

    for (i = 0; i < chunk->nr_holders; i++)
        if (id == chunk->holders[i])
            return i + 1;
    return 0;
}


static int vres_page_wrprotect(vres_t *resource, vres_page_t *page, int pid)
{
    int i;
    char *buf;
    int ret = 0;
    unsigned long pos = vres_get_chunk_start(resource);
    unsigned long off = pos + vres_get_page_start(resource);
    
    buf = calloc(1, PAGE_SIZE);
    if (!buf) {
        log_resource_warning(resource, "no memory");
        return -ENOMEM;
    }
    for (i = 0; i < VRES_CHUNK_PAGES; i++) {
        char *p = buf;

        if (vres_pg_present(page, pos) && vres_pg_write(page, pos)) {
#ifdef ENABLE_PAGE_CHECK
            ret = vres_page_check(resource, off, VRES_PAGE_CHECK_MAX, VRES_RDONLY);
            if (ret) {
                log_resource_err(resource, "failed to check page");
                goto out;
            }
#endif
            if (sys_shm_wrprotect(resource->key, pid, off, buf)) {
                if (ENOENT != errno) {
                    ret = -errno;
                    goto out;
                }
            }
            vres_pg_clrwrite(page, pos);
        } else
            p = NULL;
        if (vres_page_is_writable(resource, page)) {
            ret = vres_page_update(resource, page, i, p);
            if (ret) {
                log_resource_warning(resource, "failed to update (%d)", ret);
                goto out;
            }
        }
        pos++;
        off++;
    }
    vres_page_clrwritable(resource, page);
out:
    free(buf);
    return ret;
}


static int vres_page_rdprotect(vres_t *resource, vres_page_t *page, int pid)
{
    int i;
    int ret = 0;
    char *buf = NULL;
    vres_key_t key = resource->key;
    unsigned long pos = vres_get_chunk_start(resource);
    unsigned long off = pos + vres_get_page_start(resource);

    buf = calloc(1, PAGE_SIZE);
    if (!buf) {
        log_resource_warning(resource, "no memory");
        return -ENOMEM;
    }
    for (i = 0; i < VRES_CHUNK_PAGES; i++) {
        char *p = buf;

        if (vres_pg_present(page, pos)) {
#ifdef ENABLE_PAGE_CHECK
            ret = vres_page_check(resource, off, VRES_PAGE_CHECK_MAX, VRES_RDONLY);
            if (ret) {
                log_resource_err(resource, "failed to check page");
                goto out;
            }
#endif
            if (sys_shm_rdprotect(key, pid, off, buf)) {
                if (ENOENT != errno) {
                    log_resource_warning(resource, "failed to protect (%d)", errno);
                    ret = -errno;
                    goto out;
                }
            }
            vres_pg_clrpresent(page, pos);
            vres_pg_clrread(page, pos);
        } else
            p = NULL;
        if (vres_page_is_writable(resource, page)) {
            ret = vres_page_update(resource, page, i, p);
            if (ret) {
                log_resource_warning(resource, "failed to update (%d)", ret);
                goto out;
            }
        }
        pos++;
        off++;
    }
    vres_page_clrwritable(resource, page);
    vres_page_clractive(resource, page);
out:
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
        log_resource_warning(resource, "failed to get pid (ret=%s)", log_get_err(ret));
        return -EINVAL;
    }
    if (flags & VRES_RDONLY)
        return vres_page_wrprotect(resource, page, desc.id);
    else if (flags & VRES_RDWR)
        return vres_page_rdprotect(resource, page, desc.id);
    else
        return 0;
}


int vres_page_check(vres_t *resource, unsigned long off, int retry, int flags)
{
    int ret;
    bool present;
    int cnt = retry;
    vres_desc_t desc;
    vres_key_t key = resource->key;

    ret = vres_get_peer(resource->owner, &desc);
    if (ret) {
        log_resource_warning(resource, "failed to get pid (ret=%s)", log_get_err(ret));
        return -EINVAL;
    }
    do {
        present = sys_shm_present(key, desc.id, off, flags);
        if (cnt > 0)
            cnt--;
        if ((cnt != 0) && !present)
            vres_sleep(VRES_PAGE_CHECK_INTV);
    } while (!present && (cnt != 0));
    return 0;
}


vres_chunk_t *vres_page_get_chunk(vres_t *resource, vres_page_t *page)
{
    int pos = vres_get_chunk(resource);

    if ((pos < 0) || (pos >= VRES_CHUNK_MAX))
        log_resource_err(resource, "invalid page chunk (%d)", pos);
    return &page->chunks[pos];
}


bool vres_page_chkown(vres_t *resource, vres_page_t *page)
{
    vres_chunk_t *chunk = vres_page_get_chunk(resource, page);

    return chunk->owner == resource->owner;
}


void vres_page_mkown(vres_t *resource, vres_page_t *page)
{
    vres_chunk_t *chunk = vres_page_get_chunk(resource, page);

    chunk->owner = resource->owner;
}