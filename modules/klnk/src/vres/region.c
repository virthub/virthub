#include "region.h"

int vres_region_lock_stat = 0;
vres_region_lock_group_t vres_region_lock_group[VRES_REGION_LOCK_GROUP_SIZE];

void vres_region_mkwr(vres_region_t *region, unsigned long off)
{
    assert(off >= 0 && off < VRES_REGION_OFF_MAX);
    region->flags[off] &= ~VRES_RDONLY;
    region->flags[off] |= VRES_RDWR;
}


void vres_region_mkrd(vres_region_t *region, unsigned long off)
{
    assert(off >= 0 && off < VRES_REGION_OFF_MAX);
    region->flags[off] &= ~VRES_RDWR;
    region->flags[off] |= VRES_RDONLY;
}


void vres_region_mkpresent(vres_region_t *region, unsigned long off)
{
    assert(off >= 0 && off < VRES_REGION_OFF_MAX);
    region->flags[off] |= VRES_PRESENT;
}


void vres_region_mkdump(vres_region_t *region, unsigned long off)
{
    assert(off >= 0 && off < VRES_REGION_OFF_MAX);
    region->flags[off] |= VRES_DUMP;
}


void vres_region_clrdump(vres_region_t *region, unsigned long off)
{
    assert(off >= 0 && off < VRES_REGION_OFF_MAX);
    region->flags[off] &= ~VRES_DUMP;
}


void vres_region_clrrd(vres_region_t *region, unsigned long off)
{
    assert(off >= 0 && off < VRES_REGION_OFF_MAX);
    region->flags[off] &= ~VRES_RDWR;
    region->flags[off] &= ~VRES_RDONLY;
}


void vres_region_clrwr(vres_region_t *region, unsigned long off)
{
    assert(off >= 0 && off < VRES_REGION_OFF_MAX);
    region->flags[off] &= ~VRES_RDWR;
    region->flags[off] |= VRES_RDONLY;
}


void vres_region_clrpresent(vres_region_t *region, unsigned long off)
{
    assert(off >= 0 && off < VRES_REGION_OFF_MAX);
    region->flags[off] &= ~VRES_PRESENT;
}


bool vres_region_wr(vres_t *resource, vres_region_t *region)
{
    return (region->flags[vres_get_region_off(resource)] & VRES_RDWR) != 0;
}


bool vres_region_present(vres_t *resource, vres_region_t *region)
{
    return (region->flags[vres_get_region_off(resource)] & VRES_PRESENT) != 0;
}


int vres_region_lock_compare(const void *val0, const void *val1)
{
    vres_region_lock_entry_t *ent0 = ((vres_region_lock_desc_t *)val0)->entry;
    vres_region_lock_entry_t *ent1 = ((vres_region_lock_desc_t *)val1)->entry;
    const size_t size = VRES_REGION_LOCK_ENTRY_SIZE * sizeof(vres_region_lock_entry_t);

    return memcmp(ent0, ent1, size);
}


void vres_region_lock_init()
{
    int i;

    if (vres_region_lock_stat & VRES_STAT_INIT)
        return;
    for (i = 0; i < VRES_REGION_LOCK_GROUP_SIZE; i++) {
        pthread_mutex_init(&vres_region_lock_group[i].mutex, NULL);
        rbtree_new(&vres_region_lock_group[i].tree, vres_region_lock_compare);
    }
    vres_region_lock_stat |= VRES_STAT_INIT;
    assert(VRES_LINE_SIZE <= PAGE_SIZE);
}


static inline unsigned long vres_region_lock_hash(vres_region_lock_desc_t *desc)
{
    vres_region_lock_entry_t *ent = desc->entry;

    assert(VRES_REGION_LOCK_ENTRY_SIZE == 2);
    return (ent[0] ^ ent[1]) % VRES_REGION_LOCK_GROUP_SIZE;
}


static inline void vres_region_lock_get_desc(vres_t *resource, vres_region_lock_desc_t *desc)
{
    desc->entry[0] = resource->key;
    desc->entry[1] = vres_get_region(resource);
}


static inline vres_region_lock_t *vres_region_lock_alloc(vres_region_lock_desc_t *desc)
{
    vres_region_lock_t *lock = (vres_region_lock_t *)malloc(sizeof(vres_region_lock_t));

    if (lock) {
        lock->count = 0;
        memcpy(&lock->desc, desc, sizeof(vres_region_lock_desc_t));
        pthread_mutex_init(&lock->mutex, NULL);
        pthread_cond_init(&lock->cond, NULL);
    }
    return lock;
}


static inline vres_region_lock_t *vres_region_lock_lookup(vres_region_lock_group_t *group, vres_region_lock_desc_t *desc)
{
    rbtree_node_t *node = NULL;

    if (!rbtree_find(&group->tree, desc, &node))
        return tree_entry(node, vres_region_lock_t, node);
    else
        return NULL;
}


static inline void vres_region_lock_insert(vres_region_lock_group_t *group, vres_region_lock_t *lock)
{
    rbtree_insert(&group->tree, &lock->desc, &lock->node);
}


static inline vres_region_lock_t *vres_region_lock_get(vres_t *resource, bool *first)
{
    vres_region_lock_desc_t desc;
    vres_region_lock_group_t *grp;
    vres_region_lock_t *lock = NULL;
#ifndef VRES_REGION_GRP_LOCK
    assert(first);
    *first = false;
#endif
    vres_region_lock_get_desc(resource, &desc);
    grp = &vres_region_lock_group[vres_region_lock_hash(&desc)];
    pthread_mutex_lock(&grp->mutex);
#ifndef VRES_REGION_GRP_LOCK
    lock = vres_region_lock_lookup(grp, &desc);
    if (!lock) {
        lock = vres_region_lock_alloc(&desc);
        if (!lock) {
            log_resource_warning(resource, "no memory");
            goto out;
        }
        vres_region_lock_insert(grp, lock);
        lock->count = 1;
    } else
        lock->count++;
    if (lock->count > 1)
        pthread_mutex_lock(&lock->mutex);
    else
        *first = true;
out:
    pthread_mutex_unlock(&grp->mutex);
#endif
    return lock;
}


static void vres_region_lock_free(vres_region_lock_group_t *group, vres_region_lock_t *lock)
{
    rbtree_remove(&group->tree, &lock->desc);
    pthread_mutex_destroy(&lock->mutex);
    pthread_cond_destroy(&lock->cond);
    free(lock);
}


static int vres_region_lock(vres_t *resource)
{
    int ret = 0;
    bool first = false;
    vres_region_lock_t *lock;

    if (!(vres_region_lock_stat & VRES_STAT_INIT))
        return -EINVAL;
    log_region_lock(resource);
    lock = vres_region_lock_get(resource, &first);
#ifndef VRES_REGION_GRP_LOCK
    if (!lock) {
        log_resource_warning(resource, "no entry");
        return -ENOENT;
    }
    if (!first) {
        pthread_cond_wait(&lock->cond, &lock->mutex);
        pthread_mutex_unlock(&lock->mutex);
    }
#endif
    return ret;
}


static void vres_region_unlock(vres_t *resource)
{
#ifndef VRES_REGION_GRP_LOCK
    bool empty = false;
    vres_region_lock_t *lock;
#endif
    vres_region_lock_desc_t desc;
    vres_region_lock_group_t *grp;

    if (!(vres_region_lock_stat & VRES_STAT_INIT))
        return;
    vres_region_lock_get_desc(resource, &desc);
    grp = &vres_region_lock_group[vres_region_lock_hash(&desc)];
#ifndef VRES_REGION_GRP_LOCK
    pthread_mutex_lock(&grp->mutex);
    lock = vres_region_lock_lookup(grp, &desc);
    if (!lock) {
        log_resource_err(resource, "cannot find the lock");
        pthread_mutex_unlock(&grp->mutex);
        return;
    }
    assert(lock->count > 0);
    lock->count--;
    if (lock->count > 0) {
        pthread_mutex_lock(&lock->mutex);
        pthread_cond_signal(&lock->cond);
    } else {
        empty = true;
        vres_region_lock_free(grp, lock);
    }
#endif
    pthread_mutex_unlock(&grp->mutex);
#ifndef VRES_REGION_GRP_LOCK
    if (!empty)
        pthread_mutex_unlock(&lock->mutex);
#endif
    log_region_unlock(resource);
}


vres_slice_t *vres_region_get_slice(vres_t *resource, vres_region_t *region)
{
    int pos = vres_get_slice(resource);

    if ((pos < 0) || (pos >= VRES_SLICE_MAX)) {
        log_resource_err(resource, "invalid page, slice=%d", pos);
        return NULL;
    } else
        return &region->slices[pos];
}


vres_chunk_t *vres_region_get_chunk(vres_t *resource, vres_region_t *region)
{
    int i = vres_get_slice(resource);
    int j = vres_get_chunk(resource);

    if ((i >= VRES_SLICE_MAX) || (j >= VRES_CHUNK_MAX)) {
        log_resource_err(resource, "invalid page, slice_id=%d, chunk_id=%d", i, j);
        return NULL;
    } else
        return &region->slices[i].chunks[j];
}


vres_region_t *vres_region_get(vres_t *resource, int flags)
{
    vres_region_t *region;
    vres_file_entry_t *entry;
    char path[VRES_PATH_MAX];
    
    vres_get_region_path(resource, path);
    vres_region_lock(resource);
    entry = vres_file_get_entry(path, sizeof(vres_region_t), flags & (VRES_RDWR | VRES_RDONLY | VRES_CREAT));
    if (!entry) {
        if (flags & VRES_CREAT)
            log_resource_warning(resource, "failed to get region, path=%s", path);
        vres_region_unlock(resource);
        return NULL;
    }
    region = vres_file_get_desc(entry, vres_region_t);
    region->entry = (void *)entry;
    log_region_get(resource, path);
    return region;
}


void vres_region_put(vres_t *resource, vres_region_t *region)
{
    vres_file_put_entry((vres_file_entry_t *)region->entry);
    vres_region_unlock(resource);
    log_region_put(resource);
}


int vres_region_check(vres_t *resource, unsigned long pos, int retry, int flags, vres_desc_t desc)
{
    bool present;
    int cnt = retry;
    vres_key_t key = resource->key;

    do {
        present = sys_shm_present(key, desc.id, pos, flags);
        if (cnt > 0)
            cnt--;
        if ((cnt != 0) && !present)
            vres_sleep(VRES_REGION_CHECK_INTV);
    } while (!present && (cnt != 0));
    return 0;
}


int vres_region_update_chunk(vres_t *resource, vres_region_t *region, struct vres_chunk *chunk, char *buf, bool *page_valids)
{
    int i;
    int j;
    int ln_cnt = 0;
    int pg_cnt = 0;
    char *dest = chunk->buf;
    bool diff[VRES_LINE_MAX];
    vres_digest_t digest[VRES_LINE_MAX];

    assert(buf);
    for (i = 0, j = 0; i < VRES_LINE_MAX; i++, j += VRES_LINE_SIZE) {
        if (page_valids[pg_cnt]) {
            digest[i] = vres_line_get_digest(&buf[j]);
            if (chunk->digest[i] != digest[i]) {
                memcpy(&dest[j], &buf[j], VRES_LINE_SIZE);
                diff[i] = true;
                log_region_line(resource, page, chunk, i, &dest[j]);
            } else {
                if (!memcmp(&dest[j], &buf[j], VRES_LINE_SIZE))
                    diff[i] = false;
                else {
                    memcpy(&dest[j], &buf[j], VRES_LINE_SIZE);
                    diff[i] = true;
                    log_region_line(resource, page, chunk, i, &dest[j]);
                }
            }
        } else {
            diff[i] = false;
            digest[i] = chunk->digest[i];
        }
        ln_cnt++;
        if (VRES_PAGE_LINES == ln_cnt) {
            ln_cnt = 0;
            pg_cnt++;
        }
    }
    for (i = VRES_REGION_NR_VERSIONS - 1; i > 0; i--)
        for (j = 0; j < VRES_LINE_MAX; j++)
            chunk->diff[i][j] = chunk->diff[i - 1][j] | diff[j];
    for (i = 0; i < VRES_LINE_MAX; i++) {
        chunk->diff[0][i] = diff[i];
        chunk->digest[i] = digest[i];
    }
    log_region_diff(chunk->diff);
    return 0;
}


int vres_region_wrprotect(vres_t *resource, vres_region_t *region, int chunk_id, vres_desc_t desc)
{
    int i;
    int ret = 0;
    int cnt = 0;
    int total = 0;
    char *buf = NULL;
    unsigned long pos = 0;
    unsigned long off = 0;
    vres_chunk_t *chunk = NULL;
    bool page_valids[VRES_CHUNK_PAGES];
    int flags = vres_get_flags(resource);
    int slice_id = vres_get_slice(resource);
    vres_slice_t *slice = vres_region_get_slice(resource, region);

    if (flags & VRES_CHUNK) {
        total = VRES_CHUNK_PAGES;
        if (chunk_id < 0) {
            off = vres_get_chunk_off(resource);
            pos = vres_get_chunk_start(resource);
            chunk = vres_region_get_chunk(resource, region);
            chunk_id = vres_get_chunk(resource);
        } else {
            assert(chunk_id < VRES_CHUNK_MAX);
            off = vres_get_chunk_off_by_id(resource, chunk_id);
            pos = vres_get_chunk_start_by_id(resource, chunk_id);
            chunk = &slice->chunks[chunk_id];
        }
        log_region_wrprotect(resource, "write protect for a chunk, slice_id=%d, chunk_id=%d, chunk_start=%lu, chunk_off=%lu", slice_id, chunk_id, pos, off);
    } else if (flags & VRES_SLICE) {
        total = VRES_SLICE_PAGES;
        off = vres_get_slice_off(resource);
        pos = vres_get_slice_start(resource);
        chunk = &slice->chunks[0];
        chunk_id = 0;
        log_region_wrprotect(resource, "write protect for a slice, slice_id=%d, slice_start=%lu, slice_off=%lu", slice_id, pos, off);
    } else {
        log_resource_err(resource, "invalid request type");
        return -EINVAL;
    }
    buf = calloc(1, VRES_CHUNK_SIZE);
    if (!buf) {
        log_resource_err(resource, "no memory");
        return -ENOMEM;
    }
    for (i = 0; i < total; i++) {
        char *p = &buf[cnt * PAGE_SIZE];
        bool rw = region->flags[off] & VRES_RDWR;
        bool present = region->flags[off] & VRES_PRESENT;

        assert(cnt * PAGE_SIZE < VRES_CHUNK_SIZE);
        if (rw && present) {
            log_region_wrprotect(resource, "perform wrprotect, off=%lu", off);
#ifdef VRES_REGION_CHECK_PRESENT
            if (chunk->stat.rw) {
                ret = vres_region_check(resource, pos, VRES_REGION_CHECK_MAX, VRES_RDONLY, desc);
                if (ret) {
                    log_resource_err(resource, "failed to check page");
                    goto out;
                }
            }
#endif
            if (sys_shm_wrprotect(resource->key, desc.id, pos, p)) {
                if (ENOENT != errno) {
                    ret = -errno;
                    goto out;
                }
            }
            page_valids[cnt] = true;
        } else
            page_valids[cnt] = false;
        vres_region_clrwr(region, off);
        pos++;
        off++;
        cnt++;
        if (VRES_CHUNK_PAGES == cnt) {
            if (chunk->stat.rw && !chunk->stat.protect) {
                ret = vres_region_update_chunk(resource, region, chunk, buf, page_valids);
                if (ret) {
                    log_resource_warning(resource, "failed to update, ret=%s", log_get_err(ret));
                    goto out;
                }
                chunk->stat.protect = true;
            }
            if (i != total - 1) {
                chunk_id++;
                assert(chunk_id < VRES_CHUNK_MAX);
                chunk = &slice->chunks[chunk_id];
                cnt = 0;
            }
        }
    }
out:
    free(buf);
    return ret;
}


int vres_region_rdprotect(vres_t *resource, vres_region_t *region, int chunk_id, vres_desc_t desc)
{
    int i;
    int ret = 0;
    int cnt = 0;
    int total = 0;
    char *buf = NULL;
    unsigned long pos = 0;
    unsigned long off = 0;
    vres_chunk_t *chunk = NULL;
    vres_key_t key = resource->key;
    bool page_valids[VRES_CHUNK_PAGES];
    int flags = vres_get_flags(resource);
    vres_slice_t *slice = vres_region_get_slice(resource, region);

    if ((flags & VRES_CHUNK) || (chunk_id >= 0)) {
        total = VRES_CHUNK_PAGES;
        if (chunk_id < 0) {
            off = vres_get_chunk_off(resource);
            pos = vres_get_chunk_start(resource);
            chunk = vres_region_get_chunk(resource, region);
        } else {
            assert(chunk_id < VRES_CHUNK_MAX);
            off = vres_get_chunk_off_by_id(resource, chunk_id);
            pos = vres_get_chunk_start_by_id(resource, chunk_id);
            chunk = &slice->chunks[chunk_id];
        }
        log_region_rdprotect(resource, "read protect for a chunk, chunk_start=%lu, chunk_off=%lu", pos, off);
    } else if (flags & VRES_SLICE) {
        total = VRES_SLICE_PAGES;
        chunk = slice->chunks;
        off = vres_get_slice_off(resource);
        pos = vres_get_slice_start(resource);
        log_region_rdprotect(resource, "read protect for a slice, slice_start=%lu, slice_off=%lu", pos, off);
    } else {
        log_resource_err(resource, "invalid request type");
        return -EINVAL;
    }
    buf = calloc(1, VRES_CHUNK_SIZE);
    if (!buf) {
        log_resource_err(resource, "no memory");
        return -ENOMEM;
    }
    for (i = 0; i < total; i++) {
        char *p = &buf[cnt * PAGE_SIZE];
        bool present = region->flags[off] & VRES_PRESENT;
        
        assert(cnt * PAGE_SIZE < VRES_CHUNK_SIZE);
        if (present) {
            log_region_rdprotect(resource, "perform rdprotect, off=%lu", off);
#ifdef VRES_REGION_CHECK_PRESENT
            if (chunk->stat.rw) {
                ret = vres_region_check(resource, pos, VRES_REGION_CHECK_MAX, VRES_RDONLY, desc);
                if (ret) {
                    log_resource_err(resource, "failed to check page");
                    goto out;
                }
            }
#endif
            if (sys_shm_rdprotect(key, desc.id, pos, p)) {
                if (ENOENT != errno) {
                    log_resource_warning(resource, "failed to protect, ret=%s", log_get_err(errno));
                    ret = -errno;
                    goto out;
                }
            }
            page_valids[cnt] = true;
        } else
            page_valids[cnt] = false;
        vres_region_clrpresent(region, off);
        vres_region_clrrd(region, off);
        pos++;
        off++;
        cnt++;
        if (VRES_CHUNK_PAGES == cnt) {
            if (chunk->stat.rw && !chunk->stat.protect) {
                ret = vres_region_update_chunk(resource, region, chunk, buf, page_valids);
                if (ret) {
                    log_resource_warning(resource, "failed to update, ret=%s", log_get_err(ret));
                    goto out;
                }
                chunk->stat.protect = true;
            }
            chunk++;
            cnt = 0;
        }
    }
out:
    free(buf);
    return ret;
}


inline int vres_region_access_protect(vres_t *resource, vres_region_t *region) 
{
    vres_time_t t_now = vres_get_time();
#ifdef VRES_REGION_SLICE_ACCESS_PROTECT
    vres_slice_t *slice = vres_region_get_slice(resource, region);
    vres_time_t t_access = slice->t_access + VRES_REGION_ACCESS_TIME;
#else
    vres_time_t t_access = region->t_access + VRES_REGION_ACCESS_TIME;
#endif
    if (t_now < t_access) {
        log_region_access_protect(resource, ">> access protect <<");
        vres_sleep(t_access - t_now);
    }
    return 0;
}


int vres_region_protect(vres_t *resource, vres_region_t *region, int chunk_id)
{
    int ret;
    vres_desc_t desc;
    int flags = vres_get_flags(resource);
#ifdef ENABLE_ACCESS_PROTECT
    ret = vres_region_access_protect(resource, region);
    if (ret) 
        return ret;
#endif
    ret = vres_get_peer(resource->owner, &desc);
    if (ret) {
        log_resource_warning(resource, "failed to get pid, ret=%s", log_get_err(ret));
        return -EINVAL;
    }
    if (flags & VRES_RDONLY)
        return vres_region_wrprotect(resource, region, chunk_id, desc);
    else if (flags & VRES_RDWR)
        return vres_region_rdprotect(resource, region, chunk_id, desc);
    else
        return 0;
}
