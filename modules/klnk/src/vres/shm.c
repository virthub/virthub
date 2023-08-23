#include "shm.h"

#if MANAGER_TYPE == STATIC_MANAGER
#include "mgr/smgr.h"
#elif MANAGER_TYPE == DYNAMIC_MANAGER
#include "mgr/dmgr.h"
#endif

int shm_stat = 0;
int shm_htab[VRES_REGION_NR_PROVIDERS][VRES_LINE_MAX];

static inline char *vres_shm_get_chunk_page(vres_t *resource, vres_chunk_t *chunk)
{
    unsigned long pos = (vres_get_off(resource) - vres_get_chunk_start(resource)) * PAGE_SIZE;

    assert(pos < VRES_CHUNK_SIZE);
    return &chunk->buf[pos];
}


static inline int vres_shm_get_providers(int nr_holders, int flags) 
{
    if (flags & VRES_RDWR)
        return nr_holders;
    else
        return nr_holders > VRES_REGION_NR_PROVIDERS ? VRES_REGION_NR_PROVIDERS : nr_holders;
}


static inline bool vres_shm_chkown(vres_t *resource, vres_region_t *region)
{
    return vres_chkown(resource, region);
}


static inline void vres_shm_mkown(vres_t *resource, vres_slice_t *slice)
{
    slice->owner = resource->owner;
}


static inline void vres_shm_chunk_mkwr(vres_region_t *region, vres_chunk_t *chunk, unsigned long chunk_off)
{
    int i;

    for (i = 0; i < VRES_CHUNK_PAGES; i++) {
        vres_region_mkwr(region, chunk_off);
        chunk_off++;
    }
    chunk->stat.rw = true;
}


static inline void vres_shm_chunk_mkrd(vres_region_t *region, vres_chunk_t *chunk, unsigned long chunk_off)
{
    int i;

    for (i = 0; i < VRES_CHUNK_PAGES; i++) {
        vres_region_mkrd(region, chunk_off);
        chunk_off++;
    }
    chunk->stat.rw = false;
}


static inline unsigned long vres_shm_get_chunk_start(vres_t *resource, int chunk_id)
{
    return vres_get_slice_start(resource) + (chunk_id << VRES_CHUNK_SHIFT);
}


static inline int vres_shm_calc_htab(int *htab, size_t length, int nr_holders)
{
    int i, j;

    if (nr_holders <= 0) {
        log_err("failed, nr_holders=%d", nr_holders);
        return -EINVAL;
    }
    if (nr_holders <= length) {
        for (i = 1; i <= nr_holders; i++)
            for (j = 1; j <= length; j++)
                if (j % i == 0)
                    htab[j - 1] = i;
    } else {
        for (j = 0; j < length; j++)
            htab[j] = j + 1;
    }
    return 0;
}


static inline int vres_shm_get_diff(struct vres_chunk *chunk, vres_version_t version, bool *diff)
{
    int interval = 0;
    const size_t size = VRES_LINE_MAX * sizeof(bool);

    if (chunk->version >= version)
        interval = chunk->version - version;
    else
        return -EINVAL;
    if (0 == interval)
        memset(diff, 0, size);
    else if (interval <= VRES_REGION_NR_VERSIONS)
        memcpy(diff, &chunk->diff[interval - 1], size);
    else {
        int i;

        for (i = 0; i < VRES_LINE_MAX; i++)
            diff[i] = true;
    }
    return 0;
}


static inline bool vres_shm_slice_is_active(vres_t *resource, vres_slice_t *slice)
{   
    return (vres_get_chunk(resource) != slice->last) && (vres_get_time() <= slice->t_access + VRES_SHM_LIVE_TIME);
}


static inline void vres_shm_region_init(vres_region_t *region)
{
    memset((void *)region, 0, sizeof(vres_region_t));
}


int vres_shm_update_holder_list(vres_t *resource, vres_slice_t *slice, vres_chunk_t *chunk, int chunk_id, vres_id_t *holders, int nr_holders)
{
    if (!nr_holders)
        return 0;
    if (chunk->nr_holders > 0) {
        int i;
        int j;

        for (i = 0; i < chunk->nr_holders; i++) {
            for (j = 0; j < nr_holders; j++) {
                if (chunk->holders[i] == holders[j]) {
                    log_shm_show_holders(resource, chunk->holders, chunk->nr_holders, "update holders");
                    log_resource_err(resource, "holder %d exists, chunk_id=%d", holders[j], chunk_id);
                    return -EEXIST;
                }
            }
        }
    }
    log_shm_update_holder_list(resource, "slice_id=%d, chunk_id=%d, nr_holders=%d (old=%d)", vres_get_slice(resource), chunk_id, chunk->nr_holders + nr_holders, chunk->nr_holders);
    if (chunk->nr_holders + nr_holders >= VRES_REGION_HOLDER_MAX) {
        log_resource_err(resource, "too much holders");
        return -EINVAL;
    }
    memcpy(&chunk->holders[chunk->nr_holders], holders, nr_holders * sizeof(vres_id_t));
    chunk->nr_holders += nr_holders;
    return 0;
}


void vres_shm_update_candidates(vres_t *resource, vres_member_t *candidates, int *nr_candidates, vres_id_t *holders, int nr_holders)
{
    int i, j, k = 0;
    int total = *nr_candidates;
    vres_id_t nu[VRES_REGION_HOLDER_MAX];

    for (i = 0; i < nr_holders; i++) {
        if (holders[i] == resource->owner)
            continue;
        for (j = 0; j < total; j++) {
            if (holders[i] == candidates[j].id) {
                if (candidates[j].count < VRES_SHM_ACCESS_MAX) {
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
    if (candidates[0].count == VRES_SHM_ACCESS_MAX) {
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
        total = min(j + k, VRES_REGION_NR_CANDIDATES);
        for (i = 0; (i < k) && (j < total); i++, j++) {
            candidates[j].id = nu[i];
            candidates[j].count = 1;
        }
        *nr_candidates = total;
    }
}

int vres_shm_add_holders(vres_t *resource, vres_region_t *region, vres_slice_t *slice, vres_chunk_t *chunk, int chunk_id, vres_id_t *holders, int total, int flags)
{
    int ret = vres_shm_update_holder_list(resource, slice, chunk, chunk_id, holders, total);

    if (!ret) {
        log_shm_add_holders(resource, "update, slice_id=%d, chunk_id=%d, new_holders=%d (nr_holders=%d, nr_candidates=%d)", vres_get_slice(resource), chunk_id, total, chunk->nr_holders, slice->cand.nr_candidates);
        vres_shm_update_candidates(resource, slice->cand.candidates, &slice->cand.nr_candidates, holders, total);
    } else
        log_resource_err(resource, "failed to update (ret=%d), slice_id=%d, chunk_id=%d, new_holders=%d", ret, vres_get_slice(resource), chunk_id, total);
    return ret;
}


int vres_shm_calc_holders(struct vres_chunk *chunk)
{
    return chunk->nr_holders;
}


void vres_shm_clear_holder_list(vres_t *resource, struct vres_chunk *chunk)
{
    chunk->nr_holders = 0;
}


bool vres_shm_find_holder(vres_t *resource, struct vres_chunk *chunk, int chunk_id, vres_id_t hld)
{
    int i;

    for (i = 0; i < chunk->nr_holders; i++)
        if (chunk->holders[i] == hld)
            return true;
    return false;
}


int vres_shm_get_hid(vres_t *resource, struct vres_chunk *chunk, vres_id_t id)
{
    int i;

    for (i = 0; i < chunk->nr_holders; i++)
        if (id == chunk->holders[i])
            return i + 1;
    return 0;
}


void vres_shm_init()
{
    if (shm_stat)
        return;
    else {
        int i;

        if (VRES_LINE_MAX < VRES_REGION_NR_PROVIDERS)
            log_warning("too much providers");
        for (i = 0; i < VRES_REGION_NR_PROVIDERS; i++)
            vres_shm_calc_htab(shm_htab[i], VRES_LINE_MAX, i + 1);
        shm_stat = VRES_STAT_INIT;
    }
}


int vres_shm_check(vres_t *resource, struct shmid_ds *shp)
{
    vres_file_t *filp;
    char path[VRES_PATH_MAX];

    vres_get_state_path(resource, path);
    filp = vres_file_open(path, "r");
    if (filp) {
        int ret = 0;

        if (vres_file_read(shp, sizeof(struct shmid_ds), 1, filp) != 1)
            ret = -EIO;
        vres_file_close(filp);
        return ret;
    } else
        return -ENOENT;
}


bool vres_shm_is_owner(vres_t *resource)
{
#if MANAGER_TYPE == AREA_MANAGER && defined(ENABLE_DYNAMIC_OWNER)
    return vres_is_owner(resource);
#else
    return vres_is_initial_owner(resource);
#endif
}


int vres_shm_ipc_init(vres_t *resource)
{
    int ret = 0;
#ifdef ENABLE_PRIORITY
    if (!vres_is_owner(resource))
        ret = vres_prio_create(resource, true);
    else
        ret = vres_prio_create(resource, false);
#endif
    return ret;
}


int vres_shm_create(vres_t *resource)
{
    vres_file_t *filp;
    struct shmid_ds shmid_ds;
    char path[VRES_PATH_MAX];
    size_t size = vres_entry_val1(resource->entry);

    if ((size < VRES_SHMMIN) || (size > VRES_SHMMAX)) {
        log_resource_err(resource, "invalid shm size %ld", size);
        return -EINVAL;
    }
    memset(&shmid_ds, 0, sizeof(struct shmid_ds));
    shmid_ds.shm_perm.__key = resource->key;
    shmid_ds.shm_perm.mode = vres_get_flags(resource) & S_IRWXUGO;
    shmid_ds.shm_cpid = vres_get_id(resource);
    shmid_ds.shm_ctime = time(0);
    shmid_ds.shm_segsz = size;
    vres_get_state_path(resource, path);
    log_shm_create(resource, "path=%s", path);
    filp = vres_file_open(path, "w");
    if (!filp) {
        log_resource_err(resource, "failed to create shm");
        return -ENOENT;
    }
    if (vres_file_write((char *)&shmid_ds, sizeof(struct shmid_ds), 1, filp) != 1) {
        vres_file_close(filp);
        return -EIO;
    }
    vres_file_close(filp);
    return 0;
}


static inline void vres_shm_pack_diff(bool (*diff)[VRES_LINE_MAX], char *out)
{
    int i, j;
    int ver = 0, line = 0;

    for (i = 0; i < VRES_REGION_DIFF_SIZE; i++) {
        char ch = 0;

        for (j = 0; j < BYTE_WIDTH; j++) {
            ch = ch << 1 | diff[ver][line];
            line++;
            if (VRES_LINE_MAX == line) {
                line = 0;
                ver++;
            }
        }
        out[i] = ch;
    }
}


static inline void vres_shm_unpack_diff(bool (*dest)[VRES_LINE_MAX], char *src)
{
    int i, j;
    int ver = 0, line = 0;

    for (i = 0; i < VRES_REGION_DIFF_SIZE; i++) {
        for (j = BYTE_WIDTH - 1; j >= 0; j--) {
            dest[ver][line] = (src[i] >> j) & 1;
            line++;
            if (VRES_LINE_MAX == line) {
                line = 0;
                ver++;
            }
        }
    }
}


static inline void vres_shm_get_record_path(vres_t *resource, int chunk_id, char *path)
{
    vres_t res = *resource;

    vres_set_off(&res, vres_shm_get_chunk_start(resource, chunk_id));
    vres_get_temp_path(&res, path);
}


static inline bool vres_shm_check_record(vres_chunk_t *chunk, int chunk_id, vres_req_t *req)
{
    bool ret = false;
    char path[VRES_PATH_MAX];
    vres_file_entry_t *entry;
    vres_t *resource = &req->resource;
    vres_shm_req_t *shm_req = vres_shm_req_convert(req);

    vres_shm_get_record_path(resource, chunk_id, path);
    entry = vres_file_get_entry(path, sizeof(vres_shm_record_t), FILE_RDONLY);
    if (entry) {
        int i;
        vres_id_t id = vres_get_id(resource);
        int count = vres_file_items_count(entry, sizeof(vres_shm_record_t));
        vres_shm_record_t *records = vres_file_get_desc(entry, vres_shm_record_t);

        for (i = 0; i < count; i++) {
            if ((records[i].id == id) && (records[i].index == shm_req->index) && (records[i].version >= shm_req->shadow_ver[chunk_id])) {
                log_shm_record(resource, path, chunk, chunk_id, shm_req);
                ret = true;
                break;
            }
        }
        vres_file_put_entry(entry);
    }
    return ret;
}


static inline bool vres_shm_has_record(vres_region_t *region, vres_req_t *req, int flags)
{
    bool ret = false;
    vres_t *resource = &req->resource;
    vres_shm_req_t *shm_req = vres_shm_req_convert(req);

    if (vres_get_flags(resource) & VRES_CHUNK) {
        vres_chunk_t *chunk = vres_region_get_chunk(resource, region);

        ret = vres_shm_check_record(chunk, vres_get_chunk(resource), req);
        if (ret) {
            if (flags & VRES_REDO) {
                assert(chunk->redo > 0);
                chunk->redo--;
            }
            log_shm_debug(resource, ">> detect a repeated chunk request <<, chunk_id=%d, redo=%d <idx:%d>", vres_get_chunk(resource), chunk->redo, shm_req->index);
        }
    } else if (vres_get_flags(resource) & VRES_SLICE) {
        int i;
        int rec_cnt = 0;
        int chunk_valids = 0;
        vres_slice_t *slice = vres_region_get_slice(resource, region);

        for (i = 0; i < VRES_CHUNK_MAX; i++) {
            if (shm_req->chunk_valids[i]) {
                vres_chunk_t *chunk = &slice->chunks[i];

                chunk_valids++;
                if (vres_shm_check_record(chunk, i, req)) {
                    shm_req->chunk_valids[i] = false;
                    if (flags & VRES_REDO) {
                        assert(chunk->redo > 0);
                        assert(1 == chunk_valids);
                        chunk->redo--;
                    }
                    rec_cnt++;
                    log_shm_debug(resource, ">> detect a repeated slice request <<, chunk_id=%d, redo=%d <idx:%d>", i, chunk->redo, shm_req->index);
                }
            }
        }
        if (chunk_valids == rec_cnt)
            ret = true;
    } else
        log_resource_err(resource, "invalid request type");
    return ret;
}


static inline int vres_shm_record(vres_chunk_t *chunk, int chunk_id, vres_req_t *req)
{
    int ret = 0;
    int count = 0;
    vres_shm_record_t rec;
    vres_file_entry_t *entry;
    char path[VRES_PATH_MAX];
    vres_t *resource = &req->resource;
    vres_shm_req_t *shm_req = vres_shm_req_convert(req);

    vres_shm_get_record_path(resource, chunk_id, path);
    entry = vres_file_get_entry(path, 0, FILE_RDWR);
    if (!entry) {
        entry = vres_file_get_entry(path, 0, FILE_RDWR | FILE_CREAT);
        if (!entry) {
            log_resource_err(resource, "failed to create, path=%s", path);
            return -ENOENT;
        }
    } else
        count = vres_file_items_count(entry, sizeof(vres_shm_record_t));
    if (count > 0) {
        int i;
        vres_id_t id = vres_get_id(resource);
        vres_shm_record_t *records = vres_file_get_desc(entry, vres_shm_record_t);

        if (count > VRES_SHM_RECORD_MAX) {
            log_resource_err(resource, "too much records");
            return -EINVAL;
        }
        for (i = 0; i < count; i++) {
            if (records[i].id == id) {
                records[i].index = shm_req->index;
                records[i].version = shm_req->shadow_ver[chunk_id];
                goto out;
            }
        }
    }
    rec.id = vres_get_id(resource);
    rec.index = shm_req->index;
    rec.version = shm_req->shadow_ver[chunk_id];
    ret = vres_file_append(entry, &rec, sizeof(vres_shm_record_t));
out:
    vres_file_put_entry(entry);
    log_shm_record(resource, path, chunk, chunk_id, shm_req);
    return ret;
}


static inline bool vres_shm_can_cover(vres_id_t id, int nr_holders, int line)
{
    int n = nr_holders > VRES_REGION_NR_PROVIDERS ? VRES_REGION_NR_PROVIDERS - 1 : nr_holders - 1;

    if ((n >= 0) && (line >= 0) && (line < VRES_LINE_MAX))
        return shm_htab[n][line] == id;
    else
        return false;
}


void vres_shm_do_check_reply(vres_t *resource, vres_slice_t *slice, vres_chunk_t *chunk, vres_id_t hid, int nr_peers, bool diff[], int *nr_lines, int *total, bool *head, bool *reply)
{
    int i;
    int cnt = 0;
    int lines = 0;
    int flags = vres_get_flags(resource);
    vres_id_t src = vres_get_id(resource);

    *total = 0;
    *nr_lines = 0;
    *head = false;
    *reply = false;
    for (i = 0; i < VRES_LINE_MAX; i++) {
        if (diff[i]) {
            cnt++;
            if (vres_shm_can_cover(hid, nr_peers, i)) {
                lines++;
                if (1 == cnt)
                    *head = true;
            }
        }
    }
    *total = cnt;
    *nr_lines = lines;
    if (flags & VRES_RDWR)
        *reply = true;
    else
        *reply = lines > 0;
    assert(slice->owner);
    if (!cnt) {
        if (chunk->holders[0] != src)
            *head = hid == 1;
        else {
            assert(chunk->nr_holders >= 2);
            *head = hid == 2;
        }
    }
}


void vres_shm_check_reply(vres_req_t *req, vres_slice_t *slice, int chunk_id, vres_id_t hid, int nr_peers, bool diff[], int *nr_lines, int *total, bool *head, bool *reply)
{
    if (hid) {
        vres_t *resource = &req->resource;
        vres_chunk_t *chunk = &slice->chunks[chunk_id];
        vres_shm_req_t *shm_req = vres_shm_req_convert(req);

        vres_shm_do_check_reply(resource, slice, chunk, hid, nr_peers, diff, nr_lines, total, head, reply);
        if (chunk->nr_holders == 1)
            log_shm_check_reply(resource, "owner=%d, src=%d, chunk_id=%d, hid=%d, nr_holders=%d (holders[0]=%d), nr_lines=%d (total=%d), head=%d, reply=%d <idx:%d>", slice->owner, vres_get_id(resource), chunk_id, hid, chunk->nr_holders, chunk->holders[0], *nr_lines, *total, *head, *reply, shm_req->index);
        else if (chunk->nr_holders > 1)
            log_shm_check_reply(resource, "owner=%d, src=%d, chunk_id=%d, hid=%d, nr_holders=%d (holders[0]=%d, holders[1]=%d), nr_lines=%d (total=%d), head=%d, reply=%d <idx:%d>", slice->owner, vres_get_id(resource), chunk_id, hid, chunk->nr_holders, chunk->holders[0], chunk->holders[1], *nr_lines, *total, *head, *reply, shm_req->index);
        else
            log_resource_err(resource, "no holders, owner=%d, src=%d, chunk_id=%d, hid=%d <idx:%d>", slice->owner, vres_get_id(resource), chunk_id, hid, shm_req->index);
    }
}


static inline bool vres_shm_is_refl(vres_t *resource, vres_chunk_t *chunk, int chunk_id, vres_req_t *req)
{
    if (chunk->nr_holders == 1 && chunk->holders[0] == vres_get_id(resource)) {
        vres_shm_req_t *shm_req = vres_shm_req_convert(req);

        log_shm_is_refl(resource, "chunk_id=%d <idx:%d>", chunk_id, shm_req->index);
        return true;
    } else
        return false;
}


static inline int vres_shm_get_spec_peers(vres_chunk_t *chunk, vres_req_t *req, int *hid, int *nr_peers)
{
    vres_t *resource = &req->resource;
    int nr_holders = vres_shm_get_providers(chunk->nr_holders, vres_get_flags(resource));

    if (nr_holders > 0) {
        int i, j;
        int cnt = 0;
        vres_id_t peers[VRES_REGION_HOLDER_MAX];
        vres_shm_req_t *shm_req = vres_shm_req_convert(req);

        *hid = 0;
        *nr_peers = 0;
        for (i = 0; i < nr_holders; i++) {
            for (j = 0; j < shm_req->peers.total; j++) {
                if (shm_req->peers.list[j] == chunk->holders[i]) {
                    peers[cnt] = chunk->holders[i];
                    cnt++;
                    break;
                }
            }
        }
        if (cnt < 1)
            return -EAGAIN;
        *nr_peers = cnt;
        for (i = 0; i < cnt; i++) {
            if (peers[i] == resource->owner) {
                *hid = i + 1;
                break;
            }
        }
        log_get_spec_peers(resource, *hid, peers, cnt);
        return 0;
    } else {
        log_shm_debug(resource, "the number of holders is not enough, holders=%d (spec mode)", chunk->nr_holders);
        return -EAGAIN;
    }
}


static inline int vres_shm_get_peer_info(vres_t *resource, vres_chunk_t *chunk, int chunk_id, vres_shm_chunk_info_t *info)
{
    int flags = vres_get_flags(resource);

    if (flags & VRES_RDWR) {
        vres_id_t src = vres_get_id(resource);
        int overlap = vres_shm_find_holder(resource, chunk, chunk_id, src);

        info->nr_holders = vres_shm_calc_holders(chunk) - overlap;
    } else if (flags & VRES_RDONLY) {
        int i;

        for (i = 0; i < chunk->nr_holders; i++)
            info->list[i] = chunk->holders[i];
        info->nr_holders = chunk->nr_holders;
    }
    log_shm_get_peer_info(resource, info);
    return 0;
}


static inline int vres_shm_get_spec_peer_info(vres_t *resource, vres_chunk_t *chunk, int chunk_id, vres_shm_chunk_info_t *info)
{
    int flags = vres_get_flags(resource);
    int ret = vres_shm_get_peer_info(resource, chunk, chunk_id, info);

    if (!ret && (flags & VRES_RDWR))
        if (!vres_shm_find_holder(resource, chunk, chunk_id, resource->owner))
            info->nr_holders++;
    return ret;
}


int vres_shm_get_holder_info(vres_t *resource, vres_region_t *region, vres_chunk_t *chunk, int chunk_id, vres_req_t *req, bool spec, int *hid, int *nr_peers)
{
    vres_shm_req_t *shm_req = vres_shm_req_convert(req);

    if (!spec) {
        *hid = chunk->hid;
        *nr_peers = vres_shm_get_providers(chunk->nr_holders, vres_get_flags(resource));
        log_shm_get_holder_info(resource, region, chunk_id, shm_req, hid, nr_peers);
    } else {
        if (vres_shm_check_record(chunk, chunk_id, req)) {
            log_resource_debug(resource, "this request has been processed, chunk_id=%d, spec=1", chunk_id);
            return -EOK;
        } else {
            int ret;

            if (chunk->stat.rw) {
                log_resource_debug(resource, "retry is required for an updated chunk, chunk_id=%d, spec=1", chunk_id);
                assert(chunk->nr_holders == 1);
                return -EAGAIN;
            }
            ret = vres_shm_get_spec_peers(chunk, req, hid, nr_peers);
            if (ret) {
                log_resource_warning(resource, "cannot get peers, ret=%s, chunk_id=%d, spec=1", log_get_err(ret), chunk_id);
                return ret;
            }
            log_shm_get_holder_info(resource, region, chunk_id, shm_req, hid, nr_peers);
        }
    }
    return 0;
}


int vres_shm_pack_rsp(vres_region_t *region, vres_req_t *req, vres_slice_t *slice, int chunk_id, vres_shm_rsp_t *rsp, int flags)
{
    int i;
    int ret = 0;
    int total = 0;
    int nr_lines = 0;
    int nr_peers = 0;
    vres_id_t hid = 0;
    bool tail = false;
    bool head = false;
    bool reply = false;
    bool diff[VRES_LINE_MAX];
    bool own = flags & VRES_OWN;
    bool spec = flags & VRES_SPEC;
    vres_t *resource = &req->resource;
    vres_chunk_t *chunk = &slice->chunks[chunk_id];
    vres_shm_req_t *shm_req = vres_shm_req_convert(req);
    vres_version_t *chunk_ver = &shm_req->chunk_ver[chunk_id];

    if (rsp->size >= VRES_SHM_RSP_SIZE) {
        log_resource_err(resource, "invalid rsp size, chunk_id=%d", chunk_id);
        return -EINVAL;
    }
    if (!chunk->stat.active) {
        if (!own) {
            if (spec) {
                log_resource_debug(resource, "retry for an inactive chunk, chunk_id=%d, spec=1", chunk_id);
                return -EAGAIN;
            } else {
                log_resource_debug(resource, "no response for an inactive chunk, chunk_id=%d, spec=0", chunk_id);
                *chunk_ver = 0;
                return 0;
            }
        }
    } else {
        ret = vres_shm_get_holder_info(resource, region, chunk, chunk_id, req, spec, &hid, &nr_peers);
        if (ret) {
            if (-EOK == ret) {
                *chunk_ver = 0;
                return 0;
            } else
                return ret;
        }
        if (vres_shm_get_diff(chunk, *chunk_ver, diff)) {
            log_resource_err(resource, "failed to get diff, chunk_id=%d, chunk_ver=%lu (req_ver=%lu)", chunk_id, chunk->version, *chunk_ver);
            return -EFAULT;
        }
        vres_shm_check_reply(req, slice, chunk_id, hid, nr_peers, diff, &nr_lines, &total, &head, &reply);
        if (spec && !total) {
            log_resource_debug(resource, "no new content, chunk_id=%d, spec=1", chunk_id);
            return -EAGAIN;
        }
    }
    tail = own;
    log_shm_pack_rsp(resource, "active=%d, tail=%d, chunk_id=%d, spec=%d", chunk->stat.active, tail, chunk_id, spec);
    if (reply || head || tail) {
        char *ptr = (void *)rsp;
        vres_shm_chunk_lines_t *lines = (vres_shm_chunk_lines_t *)&ptr[rsp->size];

        log_shm_pack_rsp(resource, "pack@%d (lines)", rsp->size);
        if (nr_lines > VRES_LINE_MAX) {
            log_resource_err(resource, "too much lines");
            return -EINVAL;
        }
        if (nr_lines > 0) {
            int j = 0;

            for (i = 0; i < VRES_LINE_MAX; i++) {
                if (diff[i] && vres_shm_can_cover(hid, nr_peers, i)) {
                    vres_line_t *line = &lines->lines[j];

                    line->num = i;
                    line->digest = chunk->digest[i];
                    memcpy(line->buf, &chunk->buf[i * VRES_LINE_SIZE], VRES_LINE_SIZE);
                    j++;
                    log_region_line(resource, region, chunk, i, &chunk->buf[i * VRES_LINE_SIZE]);
                }
            }
            assert(j == nr_lines);
        }
        lines->flags = 0;
        lines->total = total;
        lines->nr_lines = nr_lines;
        lines->chunk_id = chunk_id;
        if (chunk->stat.active) {
            lines->flags |= VRES_ACTIVE;
            *chunk_ver = chunk->version;
        } else {
            assert(!total && !nr_lines);
            *chunk_ver = chunk->shadow_version;
        }
        ptr = (char *)&lines[1] + nr_lines * sizeof(vres_line_t);
        rsp->size += sizeof(vres_shm_chunk_lines_t) + nr_lines * sizeof(vres_line_t);
        if (rsp->size >= VRES_SHM_RSP_SIZE) {
            log_resource_err(resource, "invalid rsp, chunk_id=%d, chunk_ver=%lu", chunk_id, chunk->version);
            return -EINVAL;
        }
        if (head) {
            assert(chunk->stat.active);
            log_shm_pack_rsp(resource, "pack@%d (diff)", rsp->size);
            vres_shm_pack_diff(chunk->diff, ptr);
            ptr += VRES_REGION_DIFF_SIZE;
            lines->flags |= VRES_DIFF;
            rsp->size += VRES_REGION_DIFF_SIZE;
            if (rsp->size >= VRES_SHM_RSP_SIZE) {
                log_resource_err(resource, "invalid rsp, head=1, chunk_id=%d, chunk_ver=%lu", chunk_id, chunk->version);
                return -EINVAL;
            }
            log_region_diff(chunk->diff);
        }
        if (tail) {
            if (vres_shm_is_refl(resource, chunk, chunk_id, req)) {
                assert(!chunk->stat.active);
                lines->flags |= VRES_REFL;
            }
            log_shm_pack_rsp(resource, "pack@%d (chunk info)", rsp->size);
            if (!spec)
                ret = vres_shm_get_peer_info(resource, chunk, chunk_id, (vres_shm_chunk_info_t *) ptr);
            else
                ret = vres_shm_get_spec_peer_info(resource, chunk, chunk_id, (vres_shm_chunk_info_t *)ptr);
            if (ret) {
                log_resource_err(resource, "failed to get peer info, chunk_id=%d, ret=%s", chunk_id, log_get_err(ret));
                return ret;
            }
            lines->flags |= VRES_CRIT;
            rsp->size += sizeof(vres_shm_chunk_info_t);
            if (vres_get_flags(resource) & VRES_RDONLY) {
                rsp->size += chunk->nr_holders * sizeof(vres_id_t);
                log_shm_pack_rsp(resource, "add holder info");
            }
            if (rsp->size >= VRES_SHM_RSP_SIZE) {
                log_resource_err(resource, "invalid rsp, tail=1, chunk_id=%d, chunk_ver=%lu", chunk_id, chunk->version);
                return -EINVAL;
            }
        }
        ret = vres_shm_record(chunk, chunk_id, req);
        if (ret) {
            log_resource_err(resource, "failed to record, chunk_id=%d, ret=%s", chunk_id, log_get_err(ret));
            return ret;
        }
        log_shm_pack_rsp(resource, "nr_lines=%d (total=%d), head=%d, tail=%d, chunk_id=%d, chunk_ver=%lu, rsp_size=%d, spec=%d", lines->nr_lines, lines->total, head, tail, lines->chunk_id, chunk->version, rsp->size, spec);
    } else {
        log_shm_pack_rsp(resource, "no response, chunk_id=%d, spec=%d", chunk_id, spec);
        *chunk_ver = 0;
    }
    if (flags & VRES_REDO) {
        assert(!ret && (chunk->redo > 0));
        chunk->redo--;
        log_shm_pack_rsp(resource, ">> clear redo <<, redo=%d, chunk_id=%d", chunk->redo, chunk_id);
    }
    return ret;
}


int vres_shm_save_req(vres_region_t *region, vres_req_t *req, int chunk_id)
{
    int ret;
    unsigned long off;
    bool split = false;
    vres_index_t index = 0;
    char path[VRES_PATH_MAX];
    vres_chunk_t *chunk = NULL;
    bool chunk_valids[VRES_CHUNK_MAX];
    vres_t *resource = &req->resource;
    vres_shm_req_t *shm_req = vres_shm_req_convert(req);
    vres_slice_t *slice = vres_region_get_slice(resource, region);
    
    if (chunk_id >= 0) {
        assert(chunk_id < VRES_CHUNK_MAX);
        chunk = &slice->chunks[chunk_id];
        memcpy(chunk_valids, shm_req->chunk_valids, sizeof(chunk_valids));
        memset(shm_req->chunk_valids, 0, sizeof(chunk_valids));
        shm_req->chunk_valids[chunk_id] = true;
        if (chunk_id != vres_get_chunk(resource)) {
            unsigned long start = vres_get_chunk_start_by_id(resource, chunk_id);

            split = true;
            off = vres_get_off(resource);
            vres_set_off(resource, start);
            vres_set_flags(resource, VRES_SPLIT);
        }
    } else
        chunk = vres_region_get_chunk(resource, region);
    vres_get_record_path(resource, path);
    ret = vres_record_save(path, req, &index);
    if (split) {
        vres_set_off(resource, off);
        vres_clear_flags(resource, VRES_SPLIT);
    }
    if (chunk_id >= 0)
        memcpy(shm_req->chunk_valids, chunk_valids, sizeof(chunk_valids));
    if (ret) {
        log_resource_err(resource, "failed to save request");
        return ret;
    }
    chunk->redo++;
    log_shm_save_req(region, req, chunk, chunk_id);
    return 0;
}


void vres_shm_info(struct shminfo *shminfo)
{
    memset(shminfo, 0, sizeof(struct shminfo));
    shminfo->shmmax = VRES_SHMMAX;
    shminfo->shmmin = VRES_SHMMIN;
    shminfo->shmmni = VRES_SHMMNI;
    shminfo->shmseg = VRES_SHMSEG;
    shminfo->shmall = VRES_SHMALL;
}


int vres_shm_update(vres_t *resource, struct shmid_ds *shp)
{
    int ret = 0;
    vres_file_t *filp;
    char path[VRES_PATH_MAX];

    vres_get_state_path(resource, path);
    filp = vres_file_open(path, "w");
    if (!filp)
        return -ENOENT;
    if (vres_file_write(shp, sizeof(struct shmid_ds), 1, filp) != 1)
        ret = -EIO;
    vres_file_close(filp);
    return ret;
}


int vres_shm_get_rsp(vres_region_t *region, vres_req_t *req, vres_shm_rsp_t *rsp, int flags)
{
    int ret = 0;
    vres_t *resource = &req->resource;
    vres_shm_req_t *shm_req = vres_shm_req_convert(req);
    vres_slice_t *slice = vres_region_get_slice(resource, region);
    bool own = (flags & VRES_OWN) && (shm_req->cmd == VRES_SHM_CHK_OWNER);
    bool hld = shm_req->cmd != VRES_SHM_CHK_OWNER;
    bool spec = flags & VRES_SPEC;
    bool retry = (own || hld) && spec;

    rsp->size = sizeof(vres_shm_rsp_t);
    rsp->responder = vres_shm_get_responder(resource);
    if (vres_get_flags(resource) & VRES_CHUNK) {
        int chunk_id = vres_get_chunk(resource);

        log_shm_get_rsp(resource, "a chunk request, slice_id=%d, chunk_id=%d, spec=%d", vres_get_slice(resource), chunk_id, spec);
        ret = vres_shm_pack_rsp(region, req, slice, chunk_id, rsp, flags);
        if (ret < 0) {
            if ((-EAGAIN == ret) && retry) {
                ret = vres_region_protect(resource, region, chunk_id);
                if (ret) {
                    log_resource_err(resource, "failed to protect chunk, chunk_id=%d", chunk_id);
                    goto out;
                }
                ret = vres_shm_pack_rsp(region, req, slice, chunk_id, rsp, flags & ~VRES_SPEC);
            }
            if (ret < 0)
                goto out;
        }
    } else if (vres_get_flags(resource) & VRES_SLICE) {
        int i;

        for (i = 0; i < VRES_CHUNK_MAX; i++) {
            if (shm_req->chunk_valids[i]) {
                log_shm_get_rsp(resource, "a slice request, slice_id=%d, chunk_id=%d, spec=%d", vres_get_slice(resource), i, spec);
                ret = vres_shm_pack_rsp(region, req, slice, i, rsp, flags);
                if (ret < 0) {
                    if (-EAGAIN == ret) {
                        if (retry) {
                            ret = vres_region_protect(resource, region, i);
                            if (ret) {
                                log_resource_err(resource, "failed to protect slice, chunk_id=%d", i);
                                goto out;
                            }
                            ret = vres_shm_pack_rsp(region, req, slice, i, rsp, flags & ~VRES_SPEC);
                        } else
                            ret = 0;
                    }
                    if (ret < 0)
                        goto out;
                }
            }
        }
    } else {
        log_resource_err(resource, "invalid request, spec=%d", spec);
        goto out;
    }
    if (rsp->size > sizeof(vres_shm_rsp_t)) {
        assert(rsp->size < VRES_SHM_RSP_SIZE);
        memcpy(&rsp->req, shm_req, sizeof(vres_shm_req_t));
        rsp->req.cmd = VRES_SHM_NOTIFY_PROPOSER;
        rsp->req.owner = slice->owner;
        return 0;
    } else {
        log_shm_get_rsp(resource, "no response, spec=%d", spec);
        ret = -EAGAIN;
    }
out:
    log_resource_warning(resource, "cannot pack rsp, spec=%d, ret=%s", spec, log_get_err(ret));
    rsp->size = -1;
    return ret;
}


void vres_shm_deactivate(vres_t *resource, vres_region_t *region, vres_req_t *req)
{
    int flags = vres_get_flags(resource);

    if (flags & VRES_RDWR) {
        vres_shm_req_t *shm_req = vres_shm_req_convert(req);

        if (flags & VRES_CHUNK) {
            int chunk_id = vres_get_chunk(resource);
            vres_chunk_t *chunk = vres_region_get_chunk(resource, region);

            assert(shm_req->chunk_valids[chunk_id]);
            chunk->stat.active = false;
            log_shm_deactive(resource, "chunk_id=%d, >> deactivate <<", chunk_id);
        } else if (flags & VRES_SLICE) {
            int i;
            vres_slice_t *slice = vres_region_get_slice(resource, region);

            for (i = 0; i < VRES_CHUNK_MAX; i++) {
                vres_chunk_t *chunk = &slice->chunks[i];

                if (shm_req->chunk_valids[i]) {
                    chunk->stat.active = false;
                    log_shm_deactive(resource, "chunk_id=%d, >> deactivate <<", i);
                }
            }
        }
    }
}


static inline bool vres_shm_need_update_holder_list(vres_t *resource, vres_chunk_t *chunk, int flags)
{
    bool own = flags & VRES_OWN;
    bool chown = own && (vres_get_flags(resource) & VRES_CHOWN);

    return !chown && (own || chunk->stat.active);
}


static inline bool vres_shm_need_clear_holder_list(vres_t *resource, int flags)
{
    bool chown = (flags & VRES_OWN) && (vres_get_flags(resource) & VRES_CHOWN);

    return chown || (vres_get_flags(resource) & VRES_RDWR);
}


inline bool vres_shm_need_update_shadow_version(vres_t *resource, vres_chunk_t *chunk, int flags)
{
    bool own = flags & VRES_OWN;
    bool wr = vres_get_flags(resource) & VRES_RDWR;
    bool chown = vres_get_flags(resource) & VRES_CHOWN;
    bool bypass = (chunk->nr_holders == 1) && (chunk->holders[0] == vres_get_id(resource));

    if (!(chown || !own || !wr || !bypass))
        log_resource_err(resource, "failed to update shadow version");
    return !chown && own && wr;
}


int vres_shm_do_update_holder(vres_t *resource, vres_region_t *region, vres_slice_t *slice, vres_chunk_t *chunk, int chunk_id, int flags)
{
    if (vres_shm_need_update_shadow_version(resource, chunk, flags)) {
        log_shm_do_update_holder(resource, "shadow_ver=%lu (old=%lu), chunk_id=%d (0x%lx), >> owner side changes <<", chunk->shadow_version + 1, chunk->shadow_version, chunk_id, (unsigned long)chunk);
        chunk->shadow_version = chunk->shadow_version + 1;
    }
    if (vres_shm_need_clear_holder_list(resource, flags))
        vres_shm_clear_holder_list(resource, chunk);
    if (vres_shm_need_update_holder_list(resource, chunk, flags)) {
        vres_id_t src = vres_get_id(resource);
        int ret = vres_shm_add_holders(resource, region, slice, chunk, chunk_id, &src, 1, flags);

        if (ret) {
            log_resource_err(resource, "failed to add holder, ret=%s", log_get_err(ret));
            return ret;
        }
        if (vres_get_flags(resource) & VRES_RDWR)
            assert(chunk->nr_holders == 1);
    }
    return 0;
}


int vres_shm_update_holder(vres_region_t *region, vres_req_t *req, int flags)
{
    int ret;
    vres_t *resource = &req->resource;
    bool chown = vres_get_flags(resource) & VRES_CHOWN;
    vres_shm_req_t *shm_req = vres_shm_req_convert(req);
    vres_slice_t *slice = vres_region_get_slice(resource, region);

    if (vres_get_flags(resource) & VRES_CHUNK) {
        int chunk_id = vres_get_chunk(resource);
        vres_chunk_t *chunk = vres_region_get_chunk(resource, region);

        assert(shm_req->chunk_valids[chunk_id]);
        ret = vres_shm_do_update_holder(resource, region, slice, chunk, chunk_id, flags);
        if (ret) {
            log_resource_err(resource, "failed to update holder");
            return ret;
        }
        log_shm_show_chunk_holders(resource, chunk, "update chunk holders, chunk_id=%d", chunk_id);
    } else if (vres_get_flags(resource) & VRES_SLICE) {
        int i;

        for (i = 0; i < VRES_CHUNK_MAX; i++) {
            if (shm_req->chunk_valids[i]) {
                vres_chunk_t *chunk = &slice->chunks[i];

                ret = vres_shm_do_update_holder(resource, region, slice, chunk, i, flags);
                if (ret) {
                    log_resource_err(resource, "failed to update holder");
                    return ret;
                }
                log_shm_show_chunk_holders(resource, chunk, "update slice holders, chunk_id=%d", i);
            }
        }
    } else {
        log_resource_err(resource, "invalid request type");
        return -EINVAL;
    }
    if (chown) {
        log_shm_update_holder(resource, "the owner is changed from %d to %d", slice->owner, vres_get_id(resource));
        slice->owner = vres_get_id(resource);
        if (flags & VRES_OWN) {
            int i;
            
            for (i = 0; i < VRES_CHUNK_MAX; i++) {
                vres_chunk_t *chunk = &slice->chunks[i];
                
                vres_shm_clear_holder_list(resource, chunk);
            }
        }
    }
    return 0;
}


int vres_shm_do_request_holders(vres_slice_t *slice, vres_chunk_t *chunk, int chunk_id, vres_req_t *req, vres_id_t *recv_list, int *recv_cnt)
{
    int i;
    int ret = 0;
    int cnt = 0;
    int nr_holders = 0;
    int hid = chunk->hid;
    pthread_t *thread = NULL;
    vres_id_t *holders = NULL;
    vres_t *resource = &req->resource;
    int flags = vres_get_flags(resource);
    vres_id_t src = vres_get_id(resource);
    vres_req_t *requests[VRES_REGION_HOLDER_MAX];
    vres_shm_req_t *request = vres_shm_req_convert(req);
    
    holders = chunk->holders;
    nr_holders = vres_shm_get_providers(chunk->nr_holders, flags);
    assert(nr_holders <= VRES_REGION_HOLDER_MAX);
    thread = malloc(nr_holders * sizeof(pthread_t));
    if (!thread) {
        log_resource_err(resource, "no memory");
        return -ENOMEM;
    }
    for (i = 0; i < nr_holders; i++) {
        int j;
        bool enable = true;
        int total = *recv_cnt;
        vres_id_t dest = holders[i];

        for (j = 0; j < total; j++) {
            assert(dest);
            if (dest == recv_list[j]) {
                enable = false;
                break;
            }
        }
        if (enable && (dest != src) && (dest != resource->owner)) {
            vres_req_t *preq;
            vres_shm_req_t *shm_req;
        
            vres_shm_req_clone(requests[cnt], req);
            preq = requests[cnt];
            shm_req = vres_shm_req_convert(preq);
            memset(shm_req->chunk_valids, 0, sizeof(shm_req->chunk_valids));
            shm_req->chunk_valids[chunk_id] = true;
            shm_req->shadow_ver[chunk_id] = chunk->shadow_version;
            if (flags & VRES_SLICE) {
                for (j = chunk_id + 1; j < VRES_CHUNK_MAX; j++) {
                    if (request->chunk_valids[j]) {
                        int k;
                        int len;
                        vres_id_t *hld;
                        vres_chunk_t *p = &slice->chunks[j];
                    
                        hld = p->holders;
                        len = vres_shm_get_providers(p->nr_holders, flags);
                        assert(len <= VRES_REGION_HOLDER_MAX);
                        for (k = 0; k < len; k++) {
                            if (dest == hld[k]) {
                                shm_req->chunk_valids[j] = true;
                                shm_req->shadow_ver[j] = p->shadow_version;
                                break;
                            }
                        }
                    }
                }
            }
            ret = klnk_io_async(resource, preq->buf, preq->length, NULL, 0, dest, &thread[cnt]);
            if (ret) {
                vres_shm_req_release(preq);
                log_resource_err(resource, "failed to notify");
                break;
            }
            recv_list[total] = dest;
            *recv_cnt = total + 1;
            cnt++;
        }
    }
    for (i = 0; i < cnt; i++) {
        pthread_join(thread[i], NULL);
        vres_shm_req_release(requests[i]);
    }
    free(thread);
    return ret;
}


int vres_shm_request_holders(vres_region_t *region, vres_req_t *req)
{
    int ret = 0;
    int recv_cnt = 0;
    vres_req_t *preq;
    vres_shm_req_t *shm_req;
    vres_t *resource = &req->resource;
    vres_slice_t *slice = vres_region_get_slice(resource, region);
    vres_id_t recv_list[VRES_REGION_HOLDER_MAX * VRES_CHUNK_MAX] = {0};
    
    if (req->length > VRES_IO_MAX) {
        log_resource_err(resource, "invalid request");
        return -EINVAL;
    }
    assert(slice->owner);
    shm_req = vres_shm_req_convert(req);
    shm_req->epoch = slice->epoch;
    shm_req->owner = slice->owner;
    vres_shm_req_clone(preq, req);
    shm_req = vres_shm_req_convert(preq);
    shm_req->cmd = VRES_SHM_NOTIFY_HOLDER;
    if (vres_get_flags(resource) & VRES_CHUNK) {
        int chunk_id = vres_get_chunk(resource);
        vres_chunk_t *chunk = vres_region_get_chunk(resource, region);

        assert(shm_req->chunk_valids[chunk_id]);
        ret = vres_shm_do_request_holders(slice, chunk, chunk_id, preq, recv_list, &recv_cnt);
        if (ret < 0)
            log_resource_err(resource, "failed to request holders, ret=%s", log_get_err(ret));
        log_shm_request_holders(resource, region, chunk_id, shm_req);
    } else if (vres_get_flags(resource) & VRES_SLICE) {
        int i;

        for (i = 0; i < VRES_CHUNK_MAX; i++) {
            if (shm_req->chunk_valids[i]) {
                vres_chunk_t *chunk = &slice->chunks[i];

                ret = vres_shm_do_request_holders(slice, chunk, i, preq, recv_list, &recv_cnt);
                if (ret < 0) {
                    log_resource_err(resource, "failed to request holders, ret=%s", log_get_err(ret));
                    break;
                }
                log_shm_request_holders(resource, region, i, shm_req);
            }
        }
    } else {
        log_resource_err(resource, "invalid request type");
        ret = -EINVAL;
    }
    vres_shm_req_release(preq);
    return ret;
}


int vres_shm_do_check_holder(vres_region_t *region, vres_req_t *req, int flags)
{
    int ret;
    char buf[VRES_SHM_RSP_SIZE];
    vres_t *resource = &req->resource;
    vres_shm_rsp_t *rsp = (vres_shm_rsp_t *)buf;

    if (vres_region_protect(resource, region, -1))
        log_resource_warning(resource, "failed to protect");
    ret = vres_shm_get_rsp(region, req, rsp, flags);
    if (!ret) {
        assert(rsp->size < VRES_SHM_RSP_SIZE);
        ret = klnk_io_sync(resource, (char *)rsp, rsp->size, NULL, 0, vres_get_id(resource));
        if (ret) {
            log_resource_err(resource, "failed to send rsp, ret=%s", log_get_err(ret));
            ret = -EFAULT;
        }
        vres_shm_deactivate(resource, region, req);
        log_shm_do_check_holder(region, req, rsp);
    }
    return ret;
}


static inline bool vres_shm_can_preempt(vres_t *resource, vres_region_t *region)
{
    if (vres_get_flags(resource) & VRES_PRIO)
        return true;
    else {
#ifdef VRES_SHM_PREEMPT
        vres_time_t t_now = vres_get_time();
        int slice_id = vres_get_slice(resource);
        vres_time_t t_access = region->t_access + VRES_SHM_PREEMPT_INTV;

        if ((slice_id != region->last) && (t_now < t_access)) {
            log_shm_preempt(resource, ">> preempt <<");
            return true;
        }
#endif
        return false;
    }
}


static inline bool vres_shm_check_sched(vres_t *resource, vres_region_t *region)
{
    if (vres_shm_can_preempt(resource, region)) {
#ifdef ENABLE_PREEMPT_COUNT
        region->preempt_count++;
#endif
        return false;
    } else if (vres_shm_chkown(resource, region)) {
        if (vres_get_flags(resource) & VRES_RDWR)
            return true;
        else {
            vres_chunk_t *chunk;
            int flags = vres_get_flags(resource);

            if (flags & VRES_CHUNK) {
                chunk = vres_region_get_chunk(resource, region);
                return chunk->stat.rw && chunk->stat.active;
            } else {
                int i;
                vres_slice_t *slice = vres_region_get_slice(resource, region);

                for (i = 0; i < VRES_CHUNK_MAX; i++) {
                    chunk = &slice->chunks[i];
                    if (chunk->stat.rw && chunk->stat.active)
                        return true;
                }
            }
        }
    }
    return false;
}


static inline bool vres_shm_need_sched(vres_t *resource, vres_region_t *region)
{
#if MANAGER_TYPE == DYNAMIC_MANAGER
    return vres_dmgr_check_sched(resource, region);
#elif MANAGER_TYPE == STATIC_MANAGER
    return vres_smgr_check_sched(resource, region);
#elif defined(ENABLE_PRIORITY)
    return vres_shm_check_sched(resource, region);
#else
    return false;
#endif
}


static inline bool vres_shm_need_priority(vres_t *resource, vres_region_t *region)
{
#ifdef ENABLE_PRIORITY
    return vres_shm_need_sched(resource, region);
#else
    return false;
#endif
}


static inline int vres_shm_update_owner(vres_t *resource, vres_region_t *region)
{
#if MANAGER_TYPE == DYNAMIC_MANAGER
    return vres_dmgr_update_owner(resource, region);
#else
    return 0;
#endif
}


static inline bool vres_shm_can_spec(vres_t *resource)
{
#ifdef ENABLE_SPEC
    return !(vres_get_flags(resource) & VRES_RDWR);
#else
    return false;
#endif
}


int vres_shm_do_check_owner(vres_region_t *region, vres_req_t *req, int flags)
{
    int ret = 0;
    bool need_priority = false;
    bool own = flags & VRES_OWN;
    vres_t *resource = &req->resource;
    
    if (vres_shm_need_priority(resource, region)) {
        ret = vres_prio_set_busy(resource);
        if (ret) {
            log_resource_err(resource, "failed to update priority, ret=%s", log_get_err(ret));
            return ret;
        }
        need_priority = true;
    }
    if (own) {
        ret = vres_shm_request_holders(region, req);
        if (ret) {
            log_resource_err(resource, "failed to send requests to holders, ret=%s", log_get_err(ret));
            goto out;
        }
    }
    if (vres_shm_can_spec(resource)) {
        if (!vres_shm_has_record(region, req, flags))
            ret = vres_shm_do_check_holder(region, req, flags | VRES_SPEC);
    } else if (own)
        ret = vres_shm_do_check_holder(region, req, flags);
    if (ret)
        goto out;
    if (own) {
        ret = vres_shm_update_holder(region, req, flags);
        if (ret) {
            log_resource_err(resource, "failed to update holder, ret=%s", log_get_err(ret));
            goto out;
        }
    }
    ret = vres_shm_update_owner(resource, region);
    if (ret) {
        log_resource_err(resource, "failed to update owner, ret=%s", log_get_err(ret));
        goto out;
    }
out:
    if (need_priority) {
        if (vres_prio_set_idle(resource)) {
            log_resource_err(resource, "failed to update priority");
            ret = -EINVAL;
        }
    }
    if (!ret)
        log_shm_do_check_owner(region, req);
    return -EAGAIN == ret ? 0 : ret;
}


void vres_shm_clear_updates(vres_t *resource, vres_chunk_t *chunk, int chunk_id)
{
    if (chunk->stat.update) {
        vres_file_t *filp;
        char path[VRES_PATH_MAX];

        vres_get_update_path(resource, chunk_id, path);
        log_shm_clear_updates(resource, "path=%s, chunk_id=%d", path, chunk_id);
        filp = vres_file_open(path, "r+");
        if (filp) {
            vres_file_truncate(filp, 0);
            vres_file_close(filp);
        }
        memset(chunk->collect, 0, sizeof(chunk->collect));
        chunk->stat.update = false;
    }
}


int vres_shm_save_updates(vres_t *resource, vres_chunk_t *chunk, vres_shm_chunk_lines_t *chunk_lines)
{
    int i;
    int ret = 0;
    vres_file_entry_t *entry;
    char path[VRES_PATH_MAX];
    int chunk_id = chunk_lines->chunk_id;
    int nr_lines = chunk_lines->nr_lines;
    vres_line_t *lines = chunk_lines->lines;
    bool active = chunk_lines->flags & VRES_ACTIVE;
    
    if (!lines || (nr_lines < 0) || (nr_lines > VRES_LINE_MAX)) {
        log_resource_err(resource, "cannot update");
        return -EINVAL;
    }
    vres_get_update_path(resource, chunk_id, path);
    entry = vres_file_get_entry(path, 0, FILE_RDWR | FILE_CREAT);
    if (!entry) {
        log_resource_err(resource, "no entry");
        return -ENOENT;
    }
    ret = vres_file_append(entry, lines, sizeof(vres_line_t) * nr_lines);
    vres_file_put_entry(entry);
    if (ret) {
        log_resource_err(resource, "failed to save updates");
        return -EFAULT;
    }
    if (active) {
        for (i = 0; i < nr_lines; i++) {
            int n = lines[i].num;

            assert((n >= 0) && (n < VRES_LINE_MAX));
            chunk->collect[n] = true;
        }
        chunk->stat.update = true;
        log_shm_save_updates(resource, chunk_lines);
    }
    for (i = 0; i < VRES_LINE_MAX; i++)
        if (chunk->collect[i])
            ret++;
    if (active && chunk_lines->total && (ret > chunk_lines->total)) {
        log_resource_err(resource, "invalid updates, total=%d, collected=%d", chunk_lines->total, ret);
        ret = -EINVAL;
    }
    return ret;
}


void vres_shm_load_updates(vres_t *resource, vres_region_t *region, vres_chunk_t *chunk, int chunk_id)
{
    int i;
    int total;
    vres_line_t *lines;
    vres_file_entry_t *entry;
    char path[VRES_PATH_MAX];

    if (!chunk->stat.update)
        return;
    vres_get_update_path(resource, chunk_id, path);
    entry = vres_file_get_entry(path, 0, FILE_RDONLY);
    if (!entry) {
        log_resource_err(resource, "failed to load updates, path=%s", path);
        return;
    }
    lines = vres_file_get_desc(entry, vres_line_t);
    total = vres_file_items_count(entry, sizeof(vres_line_t));
    log_shm_load_updates(resource, "path=%s, total=%d, chunk_id=%d", path, total, chunk_id);
    for (i = 0; i < total; i++) {
        vres_line_t *line = &lines[i];
        int pos = line->num * VRES_LINE_SIZE;

        assert(line->num < VRES_LINE_MAX);
        chunk->digest[line->num] = line->digest;
        memcpy(&chunk->buf[pos], line->buf, VRES_LINE_SIZE);
        log_region_line(resource, region, chunk, line->num, &chunk->buf[pos]);
    }
    vres_file_put_entry(entry);
}


int vres_shm_do_change_owner(vres_region_t *region, vres_req_t *req)
{
    int ret;
    bool chown = false;
    vres_member_t member;
    int active_members = 0;
    vres_t *resource = &req->resource;
    int flags = vres_get_flags(resource);
    vres_id_t src = vres_get_id(resource);

    ret = vres_member_lookup(resource, &member, &active_members);
    if (ret) {
        log_resource_err(resource, "invalid member");
        return ret;
    }
    if ((flags & VRES_RDWR) && (flags & VRES_SLICE) && ((member.count > 0) || (active_members < VRES_SHM_AREA_MAX - 1))) {
        vres_slice_t *slice = vres_region_get_slice(resource, region);
        vres_member_t *cand = slice->cand.candidates;
        int total = slice->cand.nr_candidates;
        int i;

        for (i = 0; i < total; i++) {
            if (cand[i].id == src) {
                if (cand[i].count >= VRES_SHM_ACCESS_THR)
                    chown = true;
                break;
            }
        }
    }
    if (chown) {
        member.count++;
        ret = vres_member_update(resource, &member);
        if (ret) {
            log_resource_err(resource, "failed to update member");
            return ret;
        }
        vres_set_flags(resource, VRES_CHOWN);
        log_shm_change_owner(resource, "the owner is updated to %d", src);
    }
    return 0;
}


int vres_shm_change_owner(vres_region_t *region, vres_req_t *req)
{
#if (MANAGER_TYPE == AREA_MANAGER) && defined(ENABLE_DYNAMIC_OWNER)
    return vres_shm_do_change_owner(region, req);
#else
    return 0;
#endif
}


static inline bool vres_shm_is_younger_request(vres_slice_t *slice, vres_chunk_t *chunk, int chunk_id, vres_shm_req_t *shm_req)
{
    assert((chunk_id >= 0) && (chunk_id < VRES_CHUNK_MAX));
    return (shm_req->epoch > slice->epoch) || (shm_req->shadow_ver[chunk_id] > chunk->version);
}


static inline bool vres_shm_is_busy(vres_t *resource, vres_region_t *region, vres_chunk_t *chunk, int chunk_id, vres_shm_req_t *shm_req)
{
    bool own = vres_shm_chkown(resource, region);
    vres_slice_t *slice = vres_region_get_slice(resource, region);
    bool younger = vres_shm_is_younger_request(slice, chunk, chunk_id, shm_req);

    return shm_req->chunk_valids[chunk_id] && chunk->stat.wait && (own || younger);
}


int vres_shm_check_active(vres_region_t *region, vres_req_t *req, int flags)
{
    int ret = 0;
    vres_t *resource = &req->resource;
    vres_shm_req_t *shm_req = vres_shm_req_convert(req);
    vres_slice_t *slice = vres_region_get_slice(resource, region);

    if (vres_get_flags(resource) & VRES_CHUNK) {
        int chunk_id = vres_get_chunk(resource);
        vres_chunk_t *chunk = vres_region_get_chunk(resource, region);

        assert(shm_req->chunk_valids[chunk_id]);
        if (vres_shm_is_busy(resource, region, chunk, chunk_id, shm_req)) {
            log_shm_check_active(resource, "the requested chunk is busy, chunk_id=%d (0x%lx), chunk_ver=%lu (req_ver=%lu, shadow_ver=%lu), chunk_index=%d, epoch=%d (req_epoch=%d), active=%d, wait=%d", chunk_id, (unsigned long)chunk, chunk->version, shm_req->chunk_ver[chunk_id], shm_req->shadow_ver[chunk_id], chunk->index, slice->epoch, shm_req->epoch, chunk->stat.active, chunk->stat.wait);
            if (!(flags & VRES_REDO)) {
                ret = vres_shm_save_req(region, req, chunk_id);
                if (ret) {
                    log_resource_err(resource, "failed to save chunk request");
                    return ret;
                }
            }
            ret = -EAGAIN;
        }
        log_shm_check_active(resource, "a chunk request, chunk_id=%d, chunk_ver=%lu (req_ver=%lu, shadow_ver=%lu), chunk_index=%d, ret=%s", chunk_id, chunk->version, shm_req->chunk_ver[chunk_id], shm_req->shadow_ver[chunk_id], chunk->index, log_get_err(ret));
    } else if (vres_get_flags(resource) & VRES_SLICE) {
        int i;
        int cnt = 0;
        vres_slice_t *slice = vres_region_get_slice(resource, region);

        for (i = 0; i < VRES_CHUNK_MAX; i++) {
            vres_chunk_t *chunk = &slice->chunks[i];

            if (vres_shm_is_busy(resource, region, chunk, i, shm_req)) {
                log_shm_check_active(resource, "the requested chunk is busy, chunk_id=%d (0x%lx), chunk_ver=%lu (req_ver=%lu, shadow_ver=%lu), chunk_index=%d, epoch=%d (req_epoch=%d), active=%d, wait=%d", i, (unsigned long)chunk, chunk->version, shm_req->chunk_ver[i], shm_req->shadow_ver[i], chunk->index, slice->epoch, shm_req->epoch, chunk->stat.active, chunk->stat.wait);
                vres_set_flags(resource, VRES_INPROG);
                ret = vres_shm_save_req(region, req, i);
                if (ret) {
                    log_resource_err(resource, "failed to save slice request, ret=%s", log_get_err(ret));
                    return ret;
                }
                shm_req->chunk_valids[i] = false;
                cnt++;
            } else if (!shm_req->chunk_valids[i])
                cnt++;
        }
        if (VRES_CHUNK_MAX == cnt)
            ret = -EAGAIN;
        log_shm_check_active(resource, "a slice request, ret=%s", log_get_err(ret));
    } else {
        log_resource_err(resource, "invalid request type");
        ret = -EINVAL;
    }
    return ret;
}


int vres_shm_check_holder(vres_req_t *req, int flags)
{
    int ret = 0;
    vres_region_t *region;
    vres_t *resource = &req->resource;

    region = vres_region_get(resource, VRES_RDWR);
    if (!region) {
        log_resource_err(resource, "no entry");
        return -ENOENT;
    }
    ret = vres_shm_check_active(region, req, flags);
    if (ret) {
        if (-EAGAIN == ret) {
            assert(!(flags & VRES_REDO));
            ret = 0;
        }
        goto out;
    }
    ret = vres_shm_do_check_holder(region, req, flags);
    if (ret) {
        log_resource_err(resource, "failed to check active holder");
        goto out;
    }
    ret = vres_shm_update_holder(region, req, flags);
    if (ret) {
        log_resource_err(resource, "failed to update holder");
        goto out;
    }
    log_shm_check_holder(region, req);
out:
    vres_region_put(resource, region);
    return ret;
}


int vres_shm_notify_holder(vres_req_t *req, int flags)
{
    int ret = 0;
    vres_region_t *region;
    vres_t *resource = &req->resource;

    region = vres_region_get(resource, VRES_RDWR);
    if (!region) {
        log_resource_err(resource, "no entry");
        return -ENOENT;
    }
    if (vres_shm_has_record(region, req, flags))
        goto out;
    ret = vres_shm_check_active(region, req, flags);
    if (ret) {
        if (-EAGAIN == ret) {
            assert(!(flags & VRES_REDO));
            ret = 0;
        }
        goto out;
    }
    if (vres_shm_can_spec(resource))
        ret = vres_shm_do_check_holder(region, req, flags | VRES_SPEC);
    else
        ret = vres_shm_do_check_holder(region, req, flags);
    log_shm_notify_holder(region, req);
    if (ret) {
        if (-EAGAIN == ret) {
            log_resource_warning(resource, "no content");
            ret = 0;
        } else
            log_resource_err(resource, "failed to check holder, ret=%s", log_get_err(ret));
        goto out;
    }
    ret = vres_shm_update_holder(region, req, flags);
    if (ret) {
        log_resource_err(resource, "failed to update holder");
        goto out;
    }
out:
    vres_region_put(resource, region);
    return ret;
}


static inline int vres_shm_wakeup(vres_t *resource, vres_region_t *region, vres_chunk_t *chunk, int chunk_id)
{
    int ret;
    vres_t res = *resource;
    vres_shm_result_t result;
    char *chunk_page = vres_shm_get_chunk_page(resource, chunk);
    vres_slice_t *slice = vres_region_get_slice(resource, region);

    result.retval = 0;
    memcpy(result.buf, chunk_page, PAGE_SIZE);
    vres_set_off(&res, vres_get_chunk_start_by_id(resource, chunk_id)); // this is required for setting chunk event
    ret = vres_event_set(&res, (char *)&result, sizeof(vres_shm_result_t));
    if (ret) {
        log_resource_err(resource, "failed to wakeup");
        return ret;
    }
#ifdef ENABLE_MONITOR
    vres_mon_req_put(&res);
#endif
    log_shm_wakeup(resource, region, chunk, chunk_id, chunk_page);
    return ret;
}


static inline bool vres_shm_need_wakeup(vres_t *resource, vres_chunk_t *chunk, int chunk_id)
{
    int flags = vres_get_flags(resource);

    return ((vres_get_chunk(resource) == chunk_id) && !(flags & VRES_SPLIT)) || chunk->stat.wakeup;
}


static inline void vres_shm_update_epoch(vres_slice_t *slice, vres_shm_req_t *shm_req)
{
#if MANAGER_TYPE != DYNAMIC_MANAGER
    slice->epoch = shm_req->epoch + 1;
#endif
}


int vres_shm_do_deliver(vres_t *resource, vres_region_t *region, vres_shm_req_t *shm_req, int chunk_id, unsigned long chunk_off)
{
    int ret;
    bool update_holders = true;
    int flags = vres_get_flags(resource);
    vres_id_t src = vres_get_id(resource);
    int slice_id = vres_get_slice(resource);
    vres_slice_t *slice = vres_region_get_slice(resource, region);
    vres_chunk_t *chunk = &slice->chunks[chunk_id];

    assert(chunk_id < VRES_CHUNK_MAX);
    assert(shm_req->chunk_valids[chunk_id]);
    if (src != resource->owner) {
        log_resource_err(resource, "invalid source %d", src);
        return -EINVAL;
    }
    vres_shm_load_updates(resource, region, chunk, chunk_id);
    if (vres_shm_need_wakeup(resource, chunk, chunk_id)) {
        ret = vres_shm_wakeup(resource, region, chunk, chunk_id);
        if (ret) {
            log_resource_err(resource, "failed to wake up");
            return ret;
        }
        vres_region_mkpresent(region, vres_get_region_off(resource));
    }
    if (flags & VRES_RDWR) {
        if (!chunk->stat.active || !chunk->stat.rw) {
            chunk->shadow_version++;
            vres_shm_clear_holder_list(resource, chunk);
            vres_shm_chunk_mkwr(region, chunk, chunk_off);
        } else {
            log_shm_deliver(resource, "this chunk is already writable, slice_id=%d, chunk_id=%d, shadow_ver=%lu", slice_id, chunk_id, chunk->shadow_version);
            update_holders = false;
        }
    } else if (flags & VRES_RDONLY)
        vres_shm_chunk_mkrd(region, chunk, chunk_off);
    if (update_holders) {
        ret = vres_shm_add_holders(resource, region, slice, chunk, chunk_id, &src, 1, 0);
        if (ret) {
            log_resource_err(resource, "failed to add holder");
            return ret;
        }
        log_shm_show_chunk_holders(resource, chunk, "update holders, chunk_id=%d", chunk_id);
    }
    vres_shm_clear_updates(resource, chunk, chunk_id);
    chunk->hid = vres_shm_get_hid(resource, chunk, src);
    if (slice->epoch <= shm_req->epoch) {
        if (chunk->stat.chown) {
            vres_shm_mkown(resource, slice);
            vres_shm_update_epoch(slice, shm_req);
            log_shm_deliver(resource, "the owner changes to %d (epoch=%d) !!!", slice->owner, slice->epoch);
        } else
            slice->epoch = shm_req->epoch;
    } else
        log_shm_deliver(resource, "detect an old epoch %d (current=%d)", shm_req->epoch, slice->epoch);
    chunk->stat.chown = false;
    chunk->stat.diff = false;
    chunk->stat.cmpl = false;
    chunk->stat.wait = false;
    chunk->stat.ready = false;
    chunk->stat.wakeup = false;
    chunk->stat.protect = false;
    chunk->stat.active = true;
    chunk->version = chunk->shadow_version;
    
    slice->last = chunk_id;
    slice->t_access = vres_get_time();
    
    region->last = slice_id;
    region->t_access = slice->t_access;
    log_shm_deliver(resource, "epoch=%d (req_epoch=%d), chunk=0x%lx, chunk_id=%d, chunk_ver=%lu, hid=%d, delivered !!! <idx:%d>", slice->epoch, shm_req->epoch, (unsigned long)chunk, chunk_id, chunk->version, chunk->hid, shm_req->index);
    return 0;
}


static inline int vres_shm_redo(vres_t *resource, int chunk_id)
{
    vres_t res = *resource;

    vres_set_off(&res, vres_shm_get_chunk_start(resource, chunk_id));
    return vres_redo(&res, 0);
}


static inline bool vres_shm_can_deliver(vres_chunk_t *chunk)
{
    return chunk->stat.ready && chunk->stat.cmpl && chunk->stat.diff && chunk->stat.wait;
}


int vres_shm_deliver(vres_region_t *region, vres_req_t *req, bool *redo)
{
    int ret = 0;
    vres_t *resource = &req->resource;
    int flags = vres_get_flags(resource);
    vres_slice_t *slice = vres_region_get_slice(resource, region);
    vres_shm_rsp_t *rsp = vres_shm_rsp_convert(req);
    vres_shm_req_t *shm_req = &rsp->req;

    *redo = false;
    if (flags & VRES_CHUNK) {
        int chunk_id = vres_get_chunk(resource);
        unsigned long chunk_off = vres_get_chunk_off(resource);
        vres_chunk_t *chunk = vres_region_get_chunk(resource, region);

        if (rsp->req.index != chunk->index) {
            log_resource_warning(resource, ">> ignore a chunk rsp <<, responder=%d, chunk_id=%d (0x%lx), chunk_index=%d (rsp_index=%d), wait=%d", rsp->responder, chunk_id, (unsigned long)chunk, chunk->index, rsp->req.index, chunk->stat.wait);
            return 0;
        }
        if (vres_shm_can_deliver(chunk)) {
            ret = vres_shm_do_deliver(resource, region, shm_req, chunk_id, chunk_off);
            if (ret) {
                log_resource_err(resource, "failed to deliver, chunk_id=%d, chunk_off=%lu", chunk_id, chunk_off);
                return ret;
            }
            if (chunk->redo) {
                log_shm_deliver(resource, "start to redo for a chunk, chunk_id=%d", chunk_id);
                vres_region_put(resource, region);
                ret = vres_shm_redo(resource, chunk_id);
                if (ret) {
                    log_resource_err(resource, "failed to redo");
                    return ret;
                }
                *redo = true;
            }
        } else if (chunk->stat.wait)
            log_shm_deliver(resource, ">> waiting for chunk results <<, chunk_index=%d, chunk_id=%d, chunk_off=%lu, cmpl=%d, update=%d", chunk->index, chunk_id, chunk_off, chunk->stat.cmpl, chunk->stat.update);
    } else if (flags & VRES_SLICE) {
        int i;
        bool need_redo = false;
        bool redo_list[VRES_CHUNK_MAX];
        unsigned long chunk_off = vres_get_slice_off(resource);

        for (i = 0; i < VRES_CHUNK_MAX; i++) {
            redo_list[i] = false;
            if (shm_req->chunk_valids[i]) {
                vres_chunk_t *chunk = &slice->chunks[i];

                if (rsp->req.index != chunk->index)
                    log_resource_warning(resource, ">> ignore a slice rsp <<, responder=%d, chunk_id=%d (0x%lx), chunk_index=%d (rsp_index=%d), wait=%d", rsp->responder, i, (unsigned long)chunk, chunk->index, rsp->req.index, chunk->stat.wait);
                else {
                    if (vres_shm_can_deliver(chunk)) {
                        ret = vres_shm_do_deliver(resource, region, shm_req, i, chunk_off);
                        if (ret) {
                            log_resource_err(resource, "failed to deliver, chunk_id=%d, chunk_off=%lu", i, chunk_off);
                            return ret;
                        }
                        if (chunk->redo) {
                            need_redo = true;
                            redo_list[i] = true;
                        }
                    } else if (chunk->stat.wait)
                        log_shm_deliver(resource, ">> waiting for slice results <<, chunk_index=%d, chunk_id=%d, chunk_off=%lu, cmpl=%d, update=%d", chunk->index, i, chunk_off, chunk->stat.cmpl, chunk->stat.update);
                }
            }
            chunk_off += 1 << VRES_CHUNK_SHIFT;
        }
        if (need_redo) {
            log_shm_deliver(resource, "start to redo slice requests");
            vres_region_put(resource, region);
            *redo = true;
            for (i = 0; i < VRES_CHUNK_MAX; i++) {
                if (redo_list[i]) {
                    ret = vres_shm_redo(resource, i);
                    if (ret) {
                        log_resource_err(resource, "failed to redo slice requests");
                        return ret;
                    }
                }
            }
        }
    }
#ifdef ENABLE_TIME_SYNC
    ret = vres_prio_sync_time(resource);
    if (ret) {
        log_resource_err(resource, "failed to sync time");
        return ret;
    }
#endif
    return ret;
}


int vres_shm_update_peers(vres_t *resource, vres_region_t *region, vres_slice_t *slice, vres_chunk_t *chunk, int chunk_id, vres_shm_chunk_info_t *info)
{
    int flags = vres_get_flags(resource);

    if (flags & VRES_RDONLY) {
        int ret;

        if ((info->nr_holders > VRES_REGION_HOLDER_MAX) || (info->nr_holders < 0)) {
            log_resource_err(resource, "invalid peer info, nr_holders=%d", info->nr_holders);
            return -EINVAL;
        }
        ret = vres_shm_add_holders(resource, region, slice, chunk, chunk_id, info->list, info->nr_holders, 0);
        if (ret) {
            log_resource_err(resource, "failed to add holder");
            return ret;
        }
        log_shm_update_peers(resource, chunk, "update peers (total=%d), chunk_id=%d", info->nr_holders, chunk_id);
    }
    return 0;
}


int vres_shm_check_version(vres_t *resource, vres_req_t *req, vres_chunk_t *chunk, int chunk_id)
{
    vres_shm_req_t *shm_req = vres_shm_req_convert(req);
    vres_version_t version = shm_req->chunk_ver[chunk_id];

    if (version > chunk->shadow_version) {
        vres_shm_clear_updates(resource, chunk, chunk_id);
        chunk->stat.cmpl = false;
        chunk->shadow_version = version;
    } else if ((version > 0) && (version < chunk->shadow_version)) {
        vres_shm_rsp_t *rsp = vres_shm_rsp_convert(req);

        log_shm_check_version(resource, ">> find an expired response <<, responder=%d, shadow_ver=%lu (req_ver=%lu), chunk_id=%d  <idx:%d>", rsp->responder, chunk->shadow_version, version, chunk_id, shm_req->index);
        return -EAGAIN;
    }
    return 0;
}


int vres_shm_unpack_rsp(vres_region_t *region, vres_req_t *req)
{
    char *head = NULL;
    char *tail = NULL;
    vres_t *resource = &req->resource;
    int flags = vres_get_flags(resource);
    vres_slice_t *slice = vres_region_get_slice(resource, region);
    vres_shm_rsp_t *rsp = vres_shm_rsp_convert(req);
    vres_shm_req_t *shm_req = &rsp->req;

    if (!(flags & VRES_RDWR) && !(flags & VRES_RDONLY)) {
       log_resource_err(resource, "invalid request type");
       return -EINVAL;
    }
    if (!rsp->size || (rsp->size >= VRES_SHM_RSP_SIZE)) {
        log_resource_err(resource, "invalid request size %d", rsp->size);
        return -EINVAL;
    }
    log_shm_unpack_rsp(resource, "responder=%d, slice_id=%d, rsp_size=%d", rsp->responder, vres_get_slice(resource), rsp->size);
    head = &((char *)&rsp[1])[0];
    tail = head + rsp->size - sizeof(vres_shm_rsp_t);
    while (head < tail) {
        vres_shm_chunk_lines_t *lines = (vres_shm_chunk_lines_t *)head;
        int chunk_id = lines->chunk_id;
        vres_chunk_t *chunk = &slice->chunks[chunk_id];
        bool wait = (rsp->req.index == chunk->index) && chunk->stat.wait;
        char *ptr = (char *)&lines[1];
        int total = -1;

        log_shm_unpack_rsp(resource, "unpack@%ld (lines), slice_id=%d, pos=%ld, nr_lines=%d, chunk_id=%d, chunk_index=%d (req_index=%d), flags=0x%x, >> wait=%d <<", head - req->buf, vres_get_slice(resource), head - (char *)rsp, lines->nr_lines, chunk_id, chunk->index, rsp->req.index, lines->flags, chunk->stat.wait);
        if ((lines->nr_lines > VRES_LINE_MAX) || (lines->nr_lines < 0)) {
            log_resource_err(resource, "invalid number of lines, chunk_id=%d", chunk_id);
            return -EINVAL;
        }
        if ((chunk_id > VRES_CHUNK_MAX) || (chunk_id < 0)) {
            log_resource_err(resource, "invalid chunk, chunk_id=%d", chunk_id);
            return -EINVAL;
        }
        assert(shm_req->chunk_valids[chunk_id]);
        if (vres_shm_check_version(resource, req, chunk, chunk_id) < 0)
            wait = false;
        if (wait) {
            total = vres_shm_save_updates(resource, chunk, lines);
            if (total < 0) {
                log_resource_err(resource, "failed to save updates");
                return -EINVAL;
            }
        }
        ptr += lines->nr_lines * sizeof(vres_line_t);
        if (lines->flags & VRES_DIFF) {
            log_shm_unpack_rsp(resource, "unpack@%ld (diff)", ptr - req->buf);
            if (wait) {
                vres_shm_unpack_diff(chunk->diff, ptr);
                log_region_diff(chunk->diff);
                chunk->stat.diff = true;
            }
            ptr += VRES_REGION_DIFF_SIZE;
        }
        if ((lines->flags & VRES_CRIT) || (lines->flags & VRES_REFL)) {
            int ret;
            vres_shm_chunk_info_t *info = (vres_shm_chunk_info_t *)ptr;
            
            if (wait) {
                log_shm_unpack_rsp(resource, "crit rsp (nr_holders=%d), unpack@%ld (chunk_info)", info->nr_holders, ptr - req->buf);
                if (lines->flags & VRES_REFL)
                    chunk->stat.diff = true;
                if (!vres_shm_chkown(resource, region)) {
                    log_shm_unpack_rsp(resource, "the current owner is %d, nr_holders=%d", rsp->req.owner, info->nr_holders);
                    slice->owner = rsp->req.owner;
                }
                ret = vres_shm_update_peers(resource, region, slice, chunk, lines->chunk_id, info);
                if (ret) {
                    log_resource_err(resource, "failed to update peers");
                    return ret;
                }
                if (flags & VRES_CHOWN) {
                    log_shm_unpack_rsp(resource, "the owner will be changed from %d to %d, chunk_id=%d", slice->owner, resource->owner, lines->chunk_id);
                    chunk->stat.chown = true;
                }
                if (flags & VRES_RDWR)
                    chunk->count -= info->nr_holders;
                else if (flags & VRES_RDONLY)
                    chunk->stat.ready = true;
            }
            ptr += sizeof(vres_shm_chunk_info_t);
            if (flags & VRES_RDONLY)
                ptr += info->nr_holders * sizeof(vres_id_t);
        }
        if (wait) {
            if (flags & VRES_RDWR) {
                if (lines->flags & VRES_ACTIVE)
                    chunk->count++;
                if (0 == chunk->count) {
                    chunk->stat.ready = true;
                    chunk->stat.cmpl = true;
                }
            } else if (flags & VRES_RDONLY) {
                if ((lines->flags & VRES_ACTIVE) && (total == lines->total) && chunk->stat.update)
                    chunk->stat.cmpl = true;
            } else
                assert(0);
        }
        log_shm_unpack_rsp(resource, "chunk_id=%d (remains=%d, ready=%d, cmpl=%d, diff=%d, wait=%d, update=%d), lines_total=%d (collected=%d, flags=0x%x)", lines->chunk_id, chunk->count, chunk->stat.ready, chunk->stat.cmpl, chunk->stat.diff, chunk->stat.wait, chunk->stat.update, lines->total, total, lines->flags);
        assert(ptr > head);
        head = ptr;
    }
    if (head != tail) {
        log_resource_err(resource, "incomplete response");
        return -EINVAL;
    }
    return 0;
}


int vres_shm_notify_proposer(vres_req_t *req)
{
    int ret;
    bool redo = false;
    vres_region_t *region;
    vres_t *resource = &req->resource;
    int chunk_id = vres_get_chunk(resource);

    region = vres_region_get(resource, VRES_RDWR);
    if (!region) {
        log_resource_err(resource, "no entry");
        return -ENOENT;
    }
    ret = vres_shm_unpack_rsp(region, req);
    if (!ret)
        ret = vres_shm_deliver(region, req, &redo);
    log_shm_notify_proposer(region, req, false);
    if (!redo)
        vres_region_put(resource, region);
    return ret;
}


int vres_shm_send_retry(vres_region_t *region, vres_req_t *req)
{
    int ret;
    vres_shm_rsp_t rsp;
    vres_t *resource = &req->resource;
    vres_shm_req_t *shm_req = vres_shm_req_convert(req);
    vres_slice_t *slice = vres_region_get_slice(resource, region);

    assert(vres_chkown(resource, region));
    log_shm_send_retry(resource, "send a retry, epoch=%d (req_epoch=%d)", slice->epoch, shm_req->epoch);
    rsp.req = *shm_req;
    rsp.req.epoch = slice->epoch;
    rsp.req.cmd = VRES_SHM_RETRY;
    rsp.size = sizeof(vres_shm_rsp_t);
    rsp.responder = vres_shm_get_responder(resource);
    ret = klnk_io_sync(resource, (char *)&rsp, rsp.size, NULL, 0, vres_get_id(resource));
    if (ret) {
        log_resource_err(resource, "failed to send rsp, ret=%s", log_get_err(ret));
        ret = -EFAULT;
    }
    return ret;
}


int vres_shm_call(vres_arg_t *arg)
{
    int ret;
    bool prio = false;
    vres_t *resource = &arg->resource;
    vres_region_t *region = (vres_region_t *)arg->ptr;

    if (vres_shm_need_priority(resource, region))
        prio = true;
    vres_region_put(resource, region);
    arg->ptr = NULL;
    if (prio) {
        ret = vres_prio_set_busy(resource);
        if (ret) {
            log_resource_err(resource, "failed to set priority, ret=%s", log_get_err(ret));
            goto out;
        }
        ret = klnk_rpc_send_to_peers(arg);
        vres_prio_set_idle(resource);
    } else
        ret = klnk_rpc_send_to_peers(arg);
out:
    free(arg->in);
    if (ret)
        log_resource_err(resource, "failed to send");
    return ret;
}


int vres_shm_handle_zeropage(vres_t *resource, vres_region_t *region, vres_req_t *req)
{
    int n;
    int ret = 0;
    vres_shm_req_t *shm_req = NULL;
    int flags = vres_get_flags(resource);
    vres_id_t src = vres_get_id(resource);
    int chunk_id = vres_get_chunk(resource);
    int slice_id = vres_get_slice(resource);

    assert(!(flags & VRES_SLICE));
    ret = vres_shm_update_owner(resource, region);
    if (ret) {
        log_resource_err(resource, "failed to update owner, ret=%s", log_get_err(ret));
        return ret;
    }
    if (vres_shm_is_local_request(resource)) {
        vres_region_mkpresent(region, vres_get_region_off(resource));
    } else {
        vres_shm_rsp_t *rsp;
        vres_t res = *resource;
        vres_shm_chunk_lines_t *lines;
        size_t size = sizeof(vres_shm_rsp_t) + sizeof(vres_shm_chunk_lines_t) + sizeof(vres_shm_chunk_info_t);

        assert(req);
        shm_req = vres_shm_req_convert(req);
        rsp = (vres_shm_rsp_t *)malloc(size);
        if (!rsp) {
            log_resource_err(resource, "no memory");
            return -ENOMEM;
        }
        memset(rsp, 0, size);
        lines = rsp->lines;
        rsp->size = size;
        rsp->req.index = shm_req->index;
        if (vres_get_flags(resource) & VRES_CHOWN) {
            vres_slice_t *slice = vres_region_get_slice(resource, region);
            
            rsp->req.owner = slice->owner;
        } else
            rsp->req.owner = resource->owner;
        rsp->req.chunk_valids[chunk_id] = true;
        rsp->req.cmd = VRES_SHM_NOTIFY_PROPOSER;
        rsp->responder = vres_shm_get_responder(resource);
        if (flags & VRES_RDONLY) {
            rsp->req.chunk_ver[chunk_id] = 1;
            lines->flags |= VRES_REFL | VRES_ACTIVE; // avoid sending diff
        } else
            lines->flags |= VRES_REFL; // avoid sending diff
        lines->chunk_id = chunk_id;
        ret = klnk_io_sync(&res, (char *)rsp, size, NULL, 0, src);
        free(rsp);
        if (ret) {
            log_resource_err(resource, "failed to send rsp, ret=%s", log_get_err(ret));
            return -EFAULT;
        }
    }
    for (n = 0; n < VRES_SLICE_MAX; n++) {
        int i;
        vres_slice_t *slice = &region->slices[n];
        
        if (!(vres_get_flags(resource) & VRES_CHOWN) || (slice_id != n))
            vres_shm_mkown(resource, slice);
        
        for (i = 0; i < VRES_CHUNK_MAX; i++) {
            vres_chunk_t *chunk = &slice->chunks[i];
            unsigned long chunk_off = vres_get_off_by_id(n, i);

            if ((slice_id != n) || (chunk_id != i))
                ret = vres_shm_add_holders(resource, region, slice, chunk, i, &resource->owner, 1, 0);
            else
                ret = vres_shm_add_holders(resource, region, slice, chunk, i, &src, 1, 0);
            if (ret) {
                log_resource_err(resource, "failed to add holder");
                return ret;
            }
            if (((slice_id != n) || (chunk_id != i)) || vres_shm_is_local_request(resource)) {
                chunk->hid = vres_shm_get_hid(resource, chunk, resource->owner);
                chunk->stat.active = true;
                chunk->version = 1;
                if (flags & VRES_RDWR)
                    vres_shm_chunk_mkwr(region, chunk, chunk_off);
                else
                    vres_shm_chunk_mkrd(region, chunk, chunk_off);
            } else
                chunk->stat.active = false;
            chunk->shadow_version = 1;
        }
    }
    log_shm_handle_zeropage(resource, region, req);
    return ret;
}


static inline int vres_shm_request_owner(vres_region_t *region, vres_req_t *req, vres_id_t dest)
{
    int ret;
    vres_t *resource = &req->resource;
    vres_shm_req_t *shm_req = vres_shm_req_convert(req);
    int cmd = shm_req->cmd;

    log_shm_request_owner(resource, cmd, dest);
    shm_req->owner = dest;
    shm_req->cmd = VRES_SHM_NOTIFY_OWNER;
    ret = klnk_io_sync(resource, req->buf, req->length, NULL, 0, dest);
    shm_req->cmd = cmd;
    return ret;
}


int vres_shm_forward(vres_region_t *region, vres_req_t *req)
{
#if (MANAGER_TYPE == AREA_MANAGER) && defined(ENABLE_DYNAMIC_OWNER)
    vres_t *resource = &req->resource;
    vres_slice_t *slice = vres_region_get_slice(resource, region);
    vres_id_t dest = vres_shm_is_owner(resource) ? slice->owner : -1;

    return vres_shm_request_owner(region, req, dest);
#elif (MANAGER_TYPE == DYNAMIC_MANAGER)
    return vres_dmgr_forward(region, req);
#else
    return 0;
#endif
}


static inline bool vres_shm_can_forward(vres_region_t *region, vres_req_t *req, int flags)
{
#if (MANAGER_TYPE == AREA_MANAGER) && defined(ENABLE_DYNAMIC_OWNER)
    vres_t *resource = &req->resource;
    vres_shm_req_t *shm_req = vres_shm_req_convert(req);
   
    return !(flags & VRES_OWN) && ((shm_req->cmd != VRES_SHM_CHK_OWNER) || (shm_req->owner == resource->owner));
#elif (MANAGER_TYPE == DYNAMIC_MANAGER)
    return vres_dmgr_can_forward(region, req, flags);
#else
    return false;
#endif
}


static inline int vres_shm_check_priority(vres_region_t *region, vres_req_t *req, int flags)
{
#ifdef ENABLE_PRIORITY
    vres_t *resource = &req->resource;

    if (vres_shm_need_priority(resource, region)) {
        int ret = vres_prio_check(req, flags);

        if ((ret == -EAGAIN) && (flags & VRES_REDO)) {
            int chunk_id = vres_get_chunk(resource);
            vres_chunk_t *chunk = vres_region_get_chunk(resource, region);

            log_shm_check_priority(resource, "chunk_id=%d (0x%lx), redo=%d, >> pending <<", chunk_id, (unsigned long)chunk, chunk->redo);
            assert(chunk->redo > 0);
            chunk->redo--;
        }
        return ret;
    } else
#endif
        return 0;
}


static inline int vres_shm_check_request(vres_region_t *region, vres_req_t *req)
{
    int ret = 0;
    vres_t *resource = &req->resource;
    vres_shm_req_t *shm_req = vres_shm_req_convert(req);
    vres_slice_t *slice = vres_region_get_slice(resource, region);

    assert(shm_req->epoch >= 0);
    if (shm_req->epoch < slice->epoch) {
        ret = vres_shm_send_retry(region, req);
        if (!ret)
            ret = -EAGAIN;
        else
            log_resource_err(resource, "failed to check request, ret=%s", log_get_err(ret));
    }
    return ret;
}


static inline bool vres_shm_can_change_owner(vres_region_t *region, vres_req_t *req, int flags)
{
#if (MANAGER_TYPE == AREA_MANAGER) && defined(ENABLE_DYNAMIC_OWNER)
    vres_t *resource = &req->resource;

    return !(vres_get_flags(resource) & VRES_INPROG) && (flags & VRES_OWN) && vres_shm_is_owner(resource);
#else
    return true;
#endif
}


int vres_shm_check_owner(vres_req_t *req, int flags)
{
    int ret = 0;
    vres_region_t *region;
    vres_t *resource = &req->resource;

    region = vres_region_get(resource, VRES_RDWR);
    if (vres_shm_is_owner(resource)) {
        if (!region) {
            region = vres_region_get(resource, VRES_RDWR | VRES_CREAT);
            if (!region) {
                log_resource_err(resource, "no entry");
                return -ENOENT;
            }
            vres_shm_region_init(region);
            ret = vres_shm_handle_zeropage(resource, region, req);
            goto out;
        }
    } else if (!region) {
        log_shm_debug(resource, "*owner mismatch*");
        return 0;
    }
    if (vres_shm_has_record(region, req, flags))
        goto out;
    ret = vres_shm_check_priority(region, req, flags);
    if (ret)
        goto out;
    if (vres_shm_chkown(resource, region)) {
        flags |= VRES_OWN;
        ret = vres_shm_check_request(region, req);
        if (ret) {
            if (-EAGAIN == ret) {
                log_shm_debug(resource, "this request needs to be resent");
                ret = 0;
            }
            goto out;
        }
        ret = vres_shm_check_active(region, req, flags);
        if (ret) {
            if (-EAGAIN == ret) {
                if (flags & VRES_PRIO) {
                    ret = 0;
                    log_shm_debug(resource, "inactive, >> retry << (prio)");
                } else if (flags & VRES_REDO) {
                    flags |= VRES_PRIO; // perform as a prioritized request
                    log_shm_debug(resource, "inactive, >> retry << (redo)");
                } else
                    log_shm_debug(resource, "inactive, >> retry <<");
            }
            goto out;
        }
    }
    if (vres_shm_can_forward(region, req, flags)) {
        ret = vres_shm_forward(region, req);
        if (ret) {
            if (-EAGAIN == ret)
                ret = vres_shm_save_req(region, req, -1);
            else
                log_resource_err(resource, "failed to send to owner, ret=%s", log_get_err(ret));
            goto out;
        }
    }
    if (vres_shm_can_change_owner(region, req, flags)) {
        ret = vres_shm_change_owner(region, req);
        if (ret) {
            log_resource_err(resource, "failed to change owner, ret=%s", log_get_err(ret));
            goto out;
        }
    }
    ret = vres_shm_do_check_owner(region, req, flags);
    log_shm_check_owner(region, req);
out:
    vres_region_put(resource, region);
    if (!(flags & VRES_PRIO))
        return -EAGAIN == ret ? 0 : ret;
    else
        return ret;
}


int vres_shm_notify_owner(vres_req_t *req, int flags)
{
    int ret = 0;
    vres_region_t *region;
    vres_t *resource = &req->resource;

    region = vres_region_get(resource, VRES_RDWR | VRES_CREAT);
    if (!region) {
        log_resource_err(resource, "no entry");
        return -ENOENT;
    }
    if (vres_shm_has_record(region, req, flags))
        goto out;
    ret = vres_shm_check_priority(region, req, flags);
    if (ret)
        goto out;
    if (vres_shm_chkown(resource, region)) {
        flags |= VRES_OWN;
        ret = vres_shm_check_request(region, req);
        if (ret) {
            if (-EAGAIN == ret)
                ret = 0;
            goto out;
        }
        ret = vres_shm_check_active(region, req, flags);
        if (ret) {
            if (-EAGAIN == ret) {
                log_shm_debug(resource, ">> retry <<");
                assert(!(flags & VRES_REDO));
                ret = 0;
            }
            goto out;
        }
    }
    if (vres_shm_can_forward(region, req, flags)) {
        ret = vres_shm_forward(region, req);
        if (ret) {
            if (-EAGAIN == ret)
                ret = vres_shm_save_req(region, req, -1);
            else
                log_resource_err(resource, "failed to send to owner");
        }
        goto out;
    }
    ret = vres_shm_do_check_owner(region, req, flags);
    log_shm_notify_owner(region, req);
out:
    vres_region_put(resource, region);
    if (!(flags & VRES_PRIO))
        return -EAGAIN == ret ? 0 : ret;
    else
        return ret;
}


static inline void vres_shm_handle_err(vres_t *resource, int err)
{
    vres_t res = *resource;
    vres_shm_result_t result;
    vres_id_t src = vres_get_id(resource);

    result.retval = err;
    vres_set_op(&res, VRES_OP_REPLY);
    klnk_io_sync(&res, (char *)&result, sizeof(vres_shm_result_t), NULL, 0, src);
}


#ifdef ENABLE_TTL
int vres_shm_check_ttl(vres_req_t *req)
{
    vres_t *resource = &req->resource;
    vres_shm_req_t *shm_req = vres_shm_req_convert(req);

    shm_req->ttl += 1;
    if (shm_req->ttl > VRES_TTL_MAX) {
        log_resource_err(resource, "cmd=%s, TTL is out of range", log_get_shm_cmd(shm_req->cmd));
        return -EINVAL;
    }
    return 0;
}
#endif


int vres_shm_propose(vres_req_t *req, int flags)
{
    int ret;
    vres_arg_t arg;
    vres_t resource = req->resource;
    vres_t *res = &resource;

    memset(&arg, 0, sizeof(vres_arg_t));
    arg.dest = -1;
    arg.resource = req->resource;
    ret = vres_shm_get_arg(res, &arg, flags);
    if (ret) {
        if (-EOK == ret)
            return 0;
        else {
            if (-EAGAIN == ret) {
                log_resource_warning(res, ">> retry <<");
                if (VRES_SHM_DELAY > 0)
                    vres_sleep(VRES_SHM_DELAY);
            } else
                log_resource_err(res, "failed to get argument");
            return ret;
        }
    }
    return vres_shm_call(&arg);
}


int vres_shm_retry(vres_req_t *req)
{
    vres_slice_t *slice;
    vres_region_t *region;
    vres_t *resource = &req->resource;
    int flags = vres_get_flags(resource);
    vres_shm_rsp_t *rsp = vres_shm_rsp_convert(req);
    vres_shm_req_t *shm_req = vres_shm_req_convert(req);

    region = vres_region_get(resource, VRES_RDWR);
    if (!region) {
        log_resource_err(resource, "no entry");
        return -ENOENT;
    }
    slice = vres_region_get_slice(resource, region);
    log_shm_retry(resource, slice, shm_req);
    assert(shm_req->epoch > slice->epoch);
    slice->epoch = shm_req->epoch;
    if (flags & VRES_CHUNK) {
        vres_chunk_t *chunk = vres_region_get_chunk(resource, region);

        assert(chunk->stat.wait && chunk->index == shm_req->index);
        chunk->stat.wait = false;
    } else if (flags & VRES_SLICE) {
        int i;

        for (i = 0; i < VRES_CHUNK_MAX; i++) {
            vres_chunk_t *chunk = &slice->chunks[i];

            if (shm_req->chunk_valids[i]) {
                assert(chunk->stat.wait && chunk->index == shm_req->index);
                chunk->stat.wait = false;
            }
        }
    } else {
        log_resource_err(resource, "invalid request");
        vres_region_put(resource, region);
        return -EINVAL;
    }
    vres_region_put(resource, region);
    vres_set_flags(resource, VRES_RETRY);
    return vres_shm_propose(req, 0);
}


vres_reply_t *vres_shm_fault(vres_req_t *req, int flags)
{
    int ret = 0;
    vres_reply_t *reply = NULL;
    vres_t *resource = &req->resource;
    vres_shm_req_t *shm_req = vres_shm_req_convert(req);

#ifdef ENABLE_TTL
    if (!(flags & VRES_REDO)) {
        ret = vres_shm_check_ttl(req);
        if (ret) {
            vres_shm_handle_err(resource, ret);
            goto out;
        }
    }
#endif
    switch (shm_req->cmd) {
    case VRES_SHM_PROPOSE:
        ret = vres_shm_propose(req, flags);
        break;
    case VRES_SHM_CHK_OWNER:
        ret = vres_shm_check_owner(req, flags);
        break;
    case VRES_SHM_CHK_HOLDER:
        ret = vres_shm_check_holder(req, flags);
        break;
    case VRES_SHM_NOTIFY_OWNER:
        ret = vres_shm_notify_owner(req, flags);
        break;
    case VRES_SHM_NOTIFY_HOLDER:
        ret = vres_shm_notify_holder(req, flags);
        break;
    case VRES_SHM_NOTIFY_PROPOSER:
        ret = vres_shm_notify_proposer(req);
        break;
    case VRES_SHM_RETRY:
        ret = vres_shm_retry(req);
        break;
    default:
        log_resource_warning(resource, "%s is not supported", log_get_shm_cmd(shm_req->cmd));
        break;
    }
    if (ret) {
        if ((flags & VRES_REDO) || (flags & VRES_PRIO))
            reply = vres_reply_err(ret);
        else {
            vres_shm_handle_err(resource, ret);
            log_resource_err(resource, "failed to handle, cmd=%s, ret=%s", log_get_shm_cmd(shm_req->cmd), log_get_err(ret));
        }
    }
out:
    return reply;
}


int vres_shm_rmid(vres_req_t *req)
{
    bool active;
    int ret = 0;
    vres_t *resource = &req->resource;

    if (vres_is_owner(resource)) {
        ret = vres_member_notify(req);
        if (ret) {
            log_resource_err(resource, "failed to notify members");
            return ret;
        }
    }
    active = vres_member_is_active(resource);
    vres_remove(resource);
    if (!active)
        ret = vres_tsk_put(resource);
    return ret;
}


int vres_shm_destroy(vres_t *resource)
{
    int ret;
    char buf[sizeof(vres_req_t) + sizeof(vres_shmctl_req_t)];
    vres_req_t *req = (vres_req_t *)buf;
    vres_shmctl_req_t *shmctl_req = vres_shmctl_req_convert(req);

    log_shm_destroy(resource);
    shmctl_req->cmd = IPC_RMID;
    req->resource = *resource;
    req->length = sizeof(vres_shmctl_req_t);
    vres_set_op(&req->resource, VRES_OP_SHMCTL);
    vres_set_id(&req->resource, resource->owner);
    return vres_shm_rmid(req);
}


vres_reply_t *vres_shm_shmctl(vres_req_t *req, int flags)
{
    int ret = 0;
    vres_reply_t *reply = NULL;
    vres_t *resource = &req->resource;
    vres_shmctl_result_t *result = NULL;
    int outlen = sizeof(vres_shmctl_result_t);
    vres_shmctl_req_t *shmctl_req = vres_shmctl_req_convert(req);
    int cmd = shmctl_req->cmd;

    switch (cmd) {
    case IPC_INFO:
        outlen += sizeof(struct shminfo);
        break;
    case SHM_INFO:
        outlen += sizeof(struct shm_info);
        break;
    case SHM_STAT:
    case IPC_STAT:
        outlen += sizeof(struct shmid_ds);
        break;
    case IPC_RMID:
        outlen = 0;
        break;
    default:
        break;
    }
    if (outlen) {
        reply = vres_reply_alloc(outlen);
        if (!reply)
            return vres_reply_err(-ENOMEM);
        result = vres_result_check(reply, vres_shmctl_result_t);
    }
    switch (cmd) {
    case IPC_INFO:
        vres_shm_info((struct shminfo *)&result[1]);
        break;
    case SHM_INFO:
    {
        //TODO: global information of shm is not available
        struct shmid_ds shmid;
        struct shm_info *shm_info;

        ret = vres_shm_check(resource, &shmid);
        if (ret)
            goto out;
        shm_info = (struct shm_info *)&result[1];
        memset(shm_info, 0, sizeof(struct shm_info));
        shm_info->used_ids = 1;
        shm_info->shm_rss = (shmid.shm_segsz + PAGE_SIZE - 1) / PAGE_SIZE;
        shm_info->shm_tot = shm_info->shm_rss;
        break;
    }
    case SHM_STAT:
    case IPC_STAT:
    {
        struct shmid_ds shmid_ds;

        ret = vres_shm_check(resource, &shmid_ds);
        if (ret)
            goto out;
        memcpy((struct shmid_ds *)&result[1], &shmid_ds, sizeof(struct shmid_ds));
        if (SHM_STAT == cmd)
            ret = resource->key;
        break;
    }
    case SHM_LOCK:
    case SHM_UNLOCK:
        break;
    case IPC_RMID:
        ret = vres_shm_rmid(req);
        if (ret)
            return vres_reply_err(-ERMID);
        break;
    case IPC_SET:
    {
        struct shmid_ds shmid;
        struct shmid_ds *buf = (struct shmid_ds *)&shmctl_req[1];

        ret = vres_shm_check(resource, &shmid);
        if (ret)
            goto out;
        memcpy(&shmid.shm_perm, &buf->shm_perm, sizeof(struct ipc_perm));
        shmid.shm_ctime = time(0);
        ret = vres_shm_update(resource, &shmid);
        break;
    }
    default:
        ret = -EINVAL;
        break;
    }
out:
    if (result)
        result->retval = ret;
    return reply;
}


int vres_shm_do_get_peers(vres_t *resource, vres_region_t *region, vres_peers_t *peers)
{
    vres_slice_t *slice = vres_region_get_slice(resource, region);

    if (!vres_shm_chkown(resource, region)) {
        int i;
        int cnt = 1;
#ifdef ENABLE_DYNAMIC_OWNER
        vres_id_t owner = slice->owner;

        if (!owner) {
            vres_desc_t desc;

            if (vres_lookup(resource, &desc)) {
                log_resource_err(resource, "failed to get owner");
                return -EINVAL;
            }
            slice->owner = desc.id;
            owner = desc.id;
        }
#else
        vres_id_t owner = vres_get_initial_owner(resource);
#endif
#ifdef ENABLE_SPEC
        if (vres_get_flags(resource) & VRES_RDONLY) {
            int total = slice->cand.nr_candidates;

            for (i = 0; i < total; i++) {
                vres_id_t id = slice->cand.candidates[i].id;

                if ((id != resource->owner) && (id != owner)) {
                    peers->list[cnt] = id;
                    cnt++;
                    if (cnt == VRES_REGION_NR_PROVIDERS)
                        break;
                }
            }
        }
#endif
        peers->list[0] = owner;
        peers->total = cnt;
        log_shm_get_peers(resource, "a peer is an owner, owner=%d", owner);
    } else
        peers->total = 0;
    return 0;
}


int vres_shm_get_peers(vres_t *resource, vres_region_t *region, vres_peers_t *peers)
{
#if MANAGER_TYPE == STATIC_MANAGER
    return vres_smgr_get_peers(resource, region, peers);
#elif MANAGER_TYPE == DYNAMIC_MANAGER
    return vres_dmgr_get_peers(resource, region, peers);
#else
    return vres_shm_do_get_peers(resource, region, peers);
#endif
}


static inline bool vres_shm_need_wait(vres_t *resource, vres_chunk_t *chunk, int flags)
{
    return !chunk->stat.active || (((chunk->holders[0] != vres_get_id(resource)) || (chunk->nr_holders != 1)) && (flags & VRES_RDWR));
}


int vres_shm_complete(vres_t *resource, vres_region_t *region, vres_arg_t *arg)
{
    int flags = vres_get_flags(resource);
    vres_id_t src = vres_get_id(resource);
    vres_chunk_t *chunk = vres_region_get_chunk(resource, region);

    if (chunk->stat.wait) {
        log_shm_debug(resource, ">> incomplete << (detect a waiting chunk 0x%lx)", (unsigned long)chunk);
        chunk->stat.wakeup = true;
        return -EAGAIN;
    } else if (!vres_shm_need_wait(resource, chunk, flags)) {
        int chunk_id = vres_get_chunk(resource);
        int slice_id = vres_get_slice(resource);
        unsigned long chunk_off = vres_get_chunk_off(resource);
        unsigned long region_off = vres_get_region_off(resource);
        vres_shm_result_t *result = (vres_shm_result_t *)arg->out;
        char *chunk_page = vres_shm_get_chunk_page(resource, chunk);
        vres_slice_t *slice = vres_region_get_slice(resource, region);
        
        result->retval = 0;
        memcpy(result->buf, chunk_page, PAGE_SIZE);
        if ((flags & VRES_RDWR) && (!chunk->stat.rw)) // Note that there is no need to update the version of a non-blocking chunk
            vres_shm_chunk_mkwr(region, chunk, chunk_off);
        vres_region_mkpresent(region, region_off);
        
        slice->last = chunk_id;
        slice->t_access = vres_get_time();
        
        region->last = slice_id;
        region->t_access = slice->t_access;
        log_shm_complete(resource, region, chunk_page);
        return -EOK;
    } else
        return 0;
}


void vres_shm_do_mkwait(vres_t *resource, vres_region_t *region, vres_slice_t *slice, vres_chunk_t *chunk, int chunk_id, int flags)
{
    chunk->count = 0;
    chunk->stat.rw = false;
    chunk->stat.wait = true;
    chunk->stat.cmpl = false;
    chunk->stat.ready = false;
    chunk->index = slice->index;
    if (flags & VRES_OWN) {
        if (vres_get_flags(resource) & VRES_RDWR) {
            int overlap = vres_shm_find_holder(resource, chunk, chunk_id, vres_get_id(resource));

            chunk->count = overlap - vres_shm_calc_holders(chunk);
        } else
            chunk->stat.ready = true;
    }
}


static inline bool vres_shm_is_chunk_busy(vres_chunk_t *chunk)
{
    return chunk->stat.wait || chunk->redo;
}


int vres_shm_mkwait(vres_t *resource, vres_region_t *region, vres_shm_req_t *shm_req, bool retry)
{
    int flags = vres_get_flags(resource);
    vres_slice_t *slice = vres_region_get_slice(resource, region);

    if (vres_shm_chkown(resource, region))
        flags |= VRES_OWN;
    
    if (flags & VRES_CHUNK) {
        int chunk_id = vres_get_chunk(resource);
        vres_chunk_t *chunk = vres_region_get_chunk(resource, region);

        assert(!chunk->stat.wait);
        assert(vres_shm_need_wait(resource, chunk, flags));
        log_shm_mkwait(resource, "wait (chunk request), slice_id=%d, chunk_id=%d <idx:%d>", vres_get_slice(resource), chunk_id, shm_req->index);
        vres_shm_do_mkwait(resource, region, slice, chunk, chunk_id, flags);
        shm_req->chunk_valids[chunk_id] = true;
    } else if (flags & VRES_SLICE) {
        int i;
        vres_id_t src = vres_get_id(resource);
        
        for (i = 0; i < VRES_CHUNK_MAX; i++) {
            vres_chunk_t *chunk = &slice->chunks[i];
            bool need_wait = vres_shm_need_wait(resource, chunk, flags);

            if (need_wait && (!vres_shm_is_chunk_busy(chunk) || (retry && !chunk->stat.wait))) {
                log_shm_mkwait(resource, "wait (slice request), slice_id=%d, chunk_id=%d <idx:%d>", vres_get_slice(resource), i, shm_req->index);
                vres_shm_do_mkwait(resource, region, slice, chunk, i, flags);
                shm_req->chunk_valids[i] = true;
            }
        }
    } else {
        log_resource_err(resource, "invalid request");
        return -EINVAL;
    }
    return 0;
}


vres_req_t *vres_shm_get_req(vres_t *resource)
{
    vres_req_t *req;
    vres_shm_req_t *shm_req;
    size_t size = sizeof(vres_req_t) + sizeof(vres_shm_req_t);

    req = (vres_req_t *)malloc(size);
    if (!req) {
        log_resource_err(resource, "no memory");
        return NULL;
    }
    memset(req, 0, size);
    req->resource = *resource;
    req->length = sizeof(vres_shm_req_t);
    shm_req = (vres_shm_req_t *)req->buf;
    shm_req->cmd = VRES_SHM_PROPOSE;
    return req;
}


void vres_shm_put_req(vres_req_t *req)
{
    free(req);
}


int vres_shm_delay(vres_t *resource, vres_region_t *region)
{
    int ret;
    vres_req_t *req = vres_shm_get_req(resource);

    vres_prio_lock(resource);
    ret = vres_prio_poll(req);
    vres_prio_unlock(resource);
    return ret;
}


int vres_shm_check_arg(vres_arg_t *arg)
{
    int ret = 0;
    bool init = false;
    vres_region_t *region;
    vres_chunk_t *chunk;
    vres_t *resource = &arg->resource;

    if (vres_get_op(resource) != VRES_OP_SHMFAULT)
        return 0;
#ifdef ENABLE_MONITOR
    vres_mon_req_get(resource);
#endif
    region = vres_region_get(resource, VRES_RDWR);
    if (!region) {
        region = vres_region_get(resource, VRES_RDWR | VRES_CREAT);
        if (!region) {
            log_resource_err(resource, "no entry, op=%s", log_get_op(vres_get_op(resource)));
            return -ENOENT;
        }
        vres_shm_region_init(region);
        if (vres_shm_is_owner(resource)) {
            ret = vres_shm_handle_zeropage(resource, region, NULL);
            if (!ret)
                ret = -EOK;
            goto out;
        }
    }
    arg->in = NULL;
    arg->index = vres_get_chunk_start(resource); // this is required for setting event
    chunk = vres_region_get_chunk(resource, region);
    ret = vres_shm_complete(resource, region, arg);
    if (ret)
        goto out;
    chunk->retry_times = 0;
    if (vres_shm_need_priority(resource, region)) {
        vres_req_t *req = vres_shm_get_req(resource);

        if (!req) {
            log_resource_err(resource, "failed to get request");
            ret = -EFAULT;
            goto out;
        }
        ret = vres_prio_check(req, 0);
        vres_shm_put_req(req);
    }
out:
    log_shm_check_arg(resource, "chunk_start=%ld, ret=%s", vres_get_chunk_start(resource), log_get_err(ret));
    if (ret) {
        if (-EOK == ret) {
            vres_shm_result_t *result = (vres_shm_result_t *)arg->out;

            result->retval = 0;
#ifdef ENABLE_MONITOR
            vres_mon_req_put(resource);
#endif
        }
        vres_region_put(resource, region);
        return ret;
    }
    arg->ptr = (void *)region;
    return 0;
}


static inline bool vres_shm_need_delay(vres_t *resource, vres_region_t *region)
{
    vres_chunk_t *chunk = vres_region_get_chunk(resource, region);

    return vres_shm_is_chunk_busy(chunk);
}


int vres_shm_send_owner_request(vres_t *resource, vres_region_t *region, char *buf, size_t size)
{
    int ret;
    vres_req_t *req = vres_alloc_request(resource, buf, size);

    if (!req) {
        log_resource_err(resource, "failed to allocate request");
        return -ENOMEM;
    }
    ret = vres_shm_request_holders(region, req);
    log_shm_send_owner_request(region, req);
    free(req);
    return ret;
}


int vres_shm_get_arg(vres_t *resource, vres_arg_t *arg, int flags)
{
    int i;
    int ret = 0;
    vres_slice_t *slice;
    vres_chunk_t *chunk;
    vres_region_t *region;
    vres_shm_req_t *shm_req = NULL;
    const size_t size = sizeof(vres_shm_req_t) + VRES_REGION_HOLDER_MAX * sizeof(vres_id_t);
    bool retry = vres_get_flags(resource) & VRES_RETRY;
    bool prio = flags & VRES_PRIO;
    bool redo = flags & VRES_REDO;

    if (vres_get_op(resource) != VRES_OP_SHMFAULT)
        return 0;
    vres_clear_flags(resource, VRES_PRIO | VRES_RETRY);
    if (!arg->ptr) {
        region = vres_region_get(resource, VRES_RDWR | VRES_CREAT);
        arg->ptr = (void *)region;
        if (!region) {
            log_resource_err(resource, "no entry");
            return -ENOENT;
        }
    } else
        region = (vres_region_t *)arg->ptr;
    assert(region);
    chunk = vres_region_get_chunk(resource, region);
    slice = vres_region_get_slice(resource, region);
    if (!retry) {      
        if (vres_shm_need_delay(resource, region)) {
            log_resource_warning(resource, ">> delay <<");
            if (!prio && !redo) {
                if (vres_shm_delay(resource, region)) {
                    log_resource_err(resource, "failed to delay");
                    ret = -EINVAL;
                    goto out;
                }
            }
            chunk->retry_times++;
            if (chunk->retry_times >= VRES_REGION_RETRY_MAX) {
                log_resource_err(resource, "too much retries for a chunk (0x%lx)", (unsigned long)chunk);
                ret = -EINVAL;
                goto out;
            }
            ret = -EAGAIN;
            goto out;
        } else {
            if ((chunk->index > 0) && vres_shm_slice_is_active(resource, slice))
                vres_set_flags(resource, VRES_SLICE);
            else
                vres_set_flags(resource, VRES_CHUNK);
        }
    }
    chunk->retry_times = 0;
    slice->index = vres_idx_inc(slice->index);
    shm_req = (vres_shm_req_t *)malloc(size);
    if (!shm_req) {
        log_resource_err(resource, "no memory");
        ret = -ENOMEM;
        goto out;
    }
    memset(shm_req, 0, size);
    shm_req->epoch = slice->epoch;
    shm_req->index = slice->index;
    if (vres_shm_chkown(resource, region))
        shm_req->cmd = VRES_SHM_CHK_HOLDER;
    else
        shm_req->cmd = VRES_SHM_CHK_OWNER;
    ret = vres_shm_get_peers(resource, region, &shm_req->peers);
    if (ret) {
        log_resource_err(resource, "failed to get peers");
        goto out;
    }
    shm_req->owner = slice->owner;
    ret = vres_shm_mkwait(resource, region, shm_req, retry);
    if (ret) {
        log_resource_err(resource, "failed to wait");
        goto out;
    }
    for (i = 0; i < VRES_CHUNK_MAX; i++) {
        chunk = &slice->chunks[i];
        shm_req->chunk_ver[i] = chunk->version;
    }
    arg->inlen = size;
    arg->in = (char *)shm_req;
    arg->resource = *resource;
    if (shm_req->peers.total > 0)
        arg->peers = &shm_req->peers;
    chunk = vres_region_get_chunk(resource, region);
    log_shm_get_arg(resource, slice, chunk, arg);
    if (vres_shm_chkown(resource, region)) {
        ret = vres_shm_send_owner_request(resource, region, (char *)shm_req, size);
        if (ret) {
            log_resource_err(resource, "failed to send owner request");
            goto out;
        }
        arg->in = NULL;
        arg->inlen = 0;
        if (!prio && !retry)
            ret = -EAGAIN;
        else
            ret = -EOK;
    }
out:
    if (ret) {
        if (shm_req)
            free(shm_req);
        vres_region_put(resource, region);
    }
    return ret;
}


int vres_shm_save_page(vres_t *resource, char *buf, size_t size)
{
    int ret = 0;
    vres_region_t *region;

    assert(size == PAGE_SIZE);
    region = vres_region_get(resource, VRES_RDWR);
    if (vres_shm_chkown(resource, region)) {
        vres_chunk_t *chunk = vres_region_get_chunk(resource, region);
        char *chunk_page = vres_shm_get_chunk_page(resource, chunk);

        memcpy(chunk_page, buf, size);
        vres_region_mkdump(region, vres_get_chunk_off(resource));
        log_shm_save_page(resource, "save a page, chunk_id=%d", vres_get_chunk(resource));
    }
    vres_region_put(resource, region);
    return ret;
}
