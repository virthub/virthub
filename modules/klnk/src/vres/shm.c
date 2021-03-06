/* shm.c
 *
 * Copyright (C) 2019 Yi-Wei Ci
 *
 * Distributed under the terms of the MIT license.
 */

#include "shm.h"

#if MANAGER_TYPE == STATIC_MANAGER
#include "mgr/smgr.h"
#elif MANAGER_TYPE == DYNAMIC_MANAGER
#include "mgr/dmgr.h"
#endif

int shm_stat = 0;
int shm_htab[VRES_PAGE_NR_HOLDERS][VRES_LINE_MAX];

inline bool vres_shm_can_own(vres_t *resource, vres_page_t *page)
{
#if MANAGER_TYPE != DYNAMIC_MANAGER
    return vres_page_chkown(resource, page);
#else
    return vres_dmgr_can_own(resource, page);
#endif
}


inline bool vres_shm_chkown(vres_t *resource, vres_page_t *page)
{
    return vres_page_chkown(resource, page);
}


inline void vres_shm_mkown(vres_t *resource, vres_page_t *page)
{
    vres_page_mkown(resource, page);
}


inline int vres_shm_calc_htab(int *htab, size_t length, int nr_holders)
{
    int i, j;

    if (nr_holders <= 0) {
        log_err("failed, nr_holders=%d", nr_holders);
        return -EINVAL;
    }
    if (nr_holders <= length) {
        for (i = 1; i <= nr_holders; i++)
            for (j = 0; j < length; j++)
                if (j % i == 0)
                    htab[j] = i;
    } else {
        for (j = 0; j < length; j++)
            htab[j] = j + 1;
    }
    return 0;
}


void vres_shm_init()
{
    int i;

    if (shm_stat)
        return;
    assert(VRES_SHM_NR_PEERS < VRES_PAGE_NR_HOLDERS);
    for (i = 0; i < VRES_PAGE_NR_HOLDERS; i++)
        vres_shm_calc_htab(shm_htab[i], VRES_LINE_MAX, i + 1);
    shm_stat = VRES_STAT_INIT;
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

    if ((size < VRES_SHMMIN) || (size > VRES_SHMMAX))
        return -EINVAL;
    memset(&shmid_ds, 0, sizeof(struct shmid_ds));
    shmid_ds.shm_perm.__key = resource->key;
    shmid_ds.shm_perm.mode = vres_get_flags(resource) & S_IRWXUGO;
    shmid_ds.shm_cpid = vres_get_id(resource);
    shmid_ds.shm_ctime = time(0);
    shmid_ds.shm_segsz = size;
    vres_get_state_path(resource, path);
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


inline void vres_shm_pack_diff(bool (*diff)[VRES_LINE_MAX], char *out)
{
    int i, j;
    int ver = 0, line = 0;

    for (i = 0; i < VRES_PAGE_DIFF_SIZE; i++) {
        char ch = 0;

        for (j = 0; j < BITS_PER_BYTE; j++) {
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


inline void vres_shm_unpack_diff(bool (*dest)[VRES_LINE_MAX], char *src)
{
    int i, j;
    int ver = 0, line = 0;

    for (i = 0; i < VRES_PAGE_DIFF_SIZE; i++) {
        for (j = BITS_PER_BYTE - 1; j >= 0; j--) {
            dest[ver][line] = (src[i] >> j) & 1;
            line++;
            if (VRES_LINE_MAX == line) {
                line = 0;
                ver++;
            }
        }
    }
}


inline bool vres_shm_has_req(vres_t *resource)
{
    vres_index_t index;
    char path[VRES_PATH_MAX];

    vres_get_record_path(resource, path);
    return !vres_record_head(path, &index);
}


int vres_shm_save_req(vres_page_t *page, vres_req_t *req)
{
    int ret;
    vres_index_t index = 0;
    char path[VRES_PATH_MAX];
    vres_t *resource = &req->resource;

    vres_get_record_path(resource, path);
    ret = vres_record_save(path, req, &index);
    if (ret) {
        log_resource_err(resource, "failed to save request");
        return ret;
    }
    log_shm_save_req(page, req);
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


inline int vres_shm_get_peer_info(vres_t *resource, vres_chunk_t *chunk, vres_shm_info_t *info)
{
    int flags = vres_get_flags(resource);

    if (flags & VRES_RDWR) {
        vres_id_t src = vres_get_id(resource);
        int overlap = vres_page_search_holder_list(resource, chunk, src);

        info->total = vres_page_calc_holders(chunk) - overlap;
    } else if (flags & VRES_RDONLY) {
        int i;

        for (i = 0; i < chunk->nr_holders; i++)
            info->list[i] = chunk->holders[i];
        info->total = chunk->nr_holders;
    }
    log_shm_get_peer_info(resource, info);
    return 0;
}


#ifdef ENABLE_SPEC
inline int vres_shm_get_spec_info(vres_t *resource, vres_chunk_t *chunk, vres_shm_info_t *info)
{
    int flags = vres_get_flags(resource);
    int ret = vres_shm_get_peer_info(resource, chunk, info);

    if (!ret && (flags & VRES_RDWR))
        if (!vres_page_search_holder_list(resource, chunk, resource->owner))
            info->total++;
    return ret;
}


inline int vres_shm_record(vres_chunk_t *chunk, vres_req_t *req)
{
    int i;
    int ret = 0;
    int count = 0;
    vres_shm_record_t rec;
    vres_file_entry_t *entry;
    char path[VRES_PATH_MAX];
    vres_t *resource = &req->resource;
    vres_id_t id = vres_get_id(resource);
    vres_shm_req_t *shm_req = (vres_shm_req_t *)req->buf;

    vres_shm_get_record_path(resource, path);
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
        vres_shm_record_t *records = vres_file_get_desc(entry, vres_shm_record_t);

        for (i = 0; i < count; i++) {
            if (records[i].id == id) {
                records[i].index = shm_req->index;
                records[i].version = chunk->version;
                goto out;
            }
        }
    }
    rec.id = id;
    rec.index = shm_req->index;
    rec.version = chunk->version;
    ret = vres_file_append(entry, &rec, sizeof(vres_shm_record_t));
out:
    vres_file_put_entry(entry);
    log_shm_record(resource, path, id, shm_req->index, chunk->version);
    return ret;
}


inline bool vres_shm_has_record(vres_page_t *page, vres_req_t *req)
{
    bool ret = false;
    char path[VRES_PATH_MAX];
    vres_file_entry_t *entry;
    vres_t *resource = &req->resource;

    vres_shm_get_record_path(resource, path);
    entry = vres_file_get_entry(path, sizeof(vres_shm_record_t), FILE_RDONLY);
    if (entry) {
        int i;
        vres_id_t id = vres_get_id(resource);
        vres_shm_req_t *shm_req = (vres_shm_req_t *)req->buf;
        int count = vres_file_items_count(entry, sizeof(vres_shm_record_t));
        vres_shm_record_t *records = vres_file_get_desc(entry, vres_shm_record_t);
        vres_chunk_t *chunk = vres_page_get_chunk(resource, page);

        for (i = 0; i < count; i++) {
            if ((records[i].id == id) && (records[i].index == shm_req->index) && (records[i].version >= chunk->version)) {
                log_shm_record(resource, path, id, shm_req->index, chunk->version);
                ret = true;
                break;
            }
        }
        vres_file_put_entry(entry);
    }
    return ret;
}


inline int vres_shm_check_spec_reply(vres_chunk_t *chunk, vres_req_t *req, int *hid, int *nr_peers)
{
    if (chunk->nr_holders > 1) { // for a shared chunk, nr_holders > 1
        int i, j;
        int cnt = 0;
        vres_t *resource = &req->resource;
        vres_id_t peers[VRES_PAGE_NR_HOLDERS];
        vres_shm_req_t *shm_req = (vres_shm_req_t *)req->buf;

        *hid = 0;
        *nr_peers = 0;
        for (i = 0; i < chunk->nr_holders; i++) {
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
        log_check_spec_reply(resource, *hid, peers, cnt);
        return 0;
    } else
        return -EAGAIN;
}


int vres_shm_spec_reply(vres_page_t *page, vres_req_t *req)
{
    int i;
    int clk;
    int hid = 0;
    int ret = 0;
    int total = -1;
    int *htab = NULL;
    int nr_lines = 0;
    int nr_peers = 0;
    bool head = false;
    bool tail = false;
    bool reply = false;
    bool diff[VRES_LINE_MAX];
    vres_t *resource = &req->resource;
    int flags = vres_get_flags(resource);
    vres_shm_req_t *shm_req = (vres_shm_req_t *)req->buf;
    vres_chunk_t *chunk = vres_page_get_chunk(resource, page);

    if (flags & VRES_RDWR)
        return -EAGAIN;
    if (!vres_shm_chkown(resource, page)) {
        if (!vres_page_active(resource, page))
            return 0;
        clk = chunk->version;
    } else {
        tail = true;
        clk = chunk->clk;
    }
    ret = vres_shm_check_spec_reply(chunk, req, &hid, &nr_peers);
    if (ret)
        goto record;
    if (vres_page_active(resource, page) && (hid > 0)) {
        memset(diff, 0, sizeof(diff));
        if (vres_page_get_diff(chunk, shm_req->version, diff)) {
            log_resource_err(resource, "failed to get diff (pg->ver=%d, req->ver=%d)", (int)chunk->version, (int)shm_req->version);
            return -EINVAL;
        }
        assert((nr_peers > 0) && (nr_peers <= VRES_PAGE_NR_HOLDERS));
        htab = shm_htab[nr_peers - 1];
        total = 0;
        for (i = 0; i < VRES_LINE_MAX; i++) {
            if (diff[i]) {
                total++;
                if (htab[i] == hid) {
                    nr_lines++;
                    if (1 == total)
                        head = true;
                }
            }
        }
        if (!total && !reply)
            reply = hid == 1;
    }
    if ((nr_lines > 0) || reply || head || tail) {
        char *ptr;
        vres_shm_rsp_t *rsp;
        vres_t res = *resource;
        vres_id_t src = vres_get_id(resource);
        size_t size = sizeof(vres_shm_rsp_t) + nr_lines * sizeof(vres_line_t);

        if (head)
            size += VRES_PAGE_DIFF_SIZE;
        if (tail) {
            size += sizeof(vres_shm_info_t);
            if (flags & VRES_RDONLY)
                size += chunk->nr_holders * sizeof(vres_id_t);
        }
        rsp = (vres_shm_rsp_t *)malloc(size);
        if (!rsp) {
            log_resource_err(resource, "no memory");
            return -ENOMEM;
        }
        memset(rsp, 0, size);
        if (nr_lines > 0) {
            int j = 0;

            for (i = 0; i < VRES_LINE_MAX; i++) {
                if (diff[i] && (htab[i] == hid)) {
                    vres_line_t *line = &rsp->lines[j];

                    assert(j < nr_lines);
                    line->num = i;
                    line->digest = chunk->digest[i];
                    memcpy(line->buf, &chunk->buf[i * VRES_LINE_SIZE], VRES_LINE_SIZE);
                    j++;
                }
            }
        }
        memcpy(&rsp->req, shm_req, sizeof(vres_shm_req_t));
        rsp->req.cmd = VRES_SHM_NOTIFY_PROPOSER;
        rsp->req.clk = clk;
        rsp->nr_lines = nr_lines;
        rsp->total = total;
        ptr = (char *)&rsp[1] + nr_lines * sizeof(vres_line_t);
        if (head) {
            vres_shm_pack_diff(chunk->diff, ptr);
            ptr += VRES_PAGE_DIFF_SIZE;
            vres_set_flags(&res, VRES_DIFF);
            assert((unsigned long)ptr - (unsigned long)rsp <= size);
        }
        if (tail) {
            ret = vres_shm_get_spec_info(resource, chunk, (vres_shm_info_t *)ptr);
            if (ret) {
                log_resource_err(resource, "failed to get peer info");
                goto out;
            }
            rsp->req.owner = page->owner;
            vres_set_flags(&res, VRES_CRIT);
            assert((unsigned long)ptr + sizeof(vres_shm_info_t) - (unsigned long)rsp <= size);
        }
        ret = klnk_io_sync(&res, (char *)rsp, size, NULL, 0, src);
        if (ret) {
            log_resource_err(resource, "failed to send rsp, ret=%s", log_get_err(ret));
            ret = -EFAULT;
        }
out:
        log_shm_lines(resource, page, rsp->lines, nr_lines, total);
        free(rsp);
        if (ret)
            return ret;
    }
    log_shm_spec_reply(resource, hid, nr_peers);
record:
    if (vres_shm_chkown(resource, page) || (hid > 0) || (VRES_SHM_NOTIFY_HOLDER == shm_req->cmd)) {
        if (vres_shm_record(chunk, req)) {
            log_resource_err(resource, "failed to add record");
            ret = -EFAULT;
        }
    }
    return ret;
}
#endif


int vres_shm_do_request_holders(vres_chunk_t *chunk, vres_req_t *req)
{
    int i;
    int total;
    int ret = 0;
    int cnt = 0;
    vres_t *resource = &req->resource;
    int flags = vres_get_flags(resource);
    vres_id_t src = vres_get_id(resource);
    pthread_t *threads = malloc(chunk->nr_holders * sizeof(pthread_t));

    if (!threads) {
        log_resource_err(resource, "no memory");
        return -ENOMEM;
    }
    for (i = 0; i < chunk->nr_holders; i++) {
        if ((chunk->holders[i] != src) && (chunk->holders[i] != resource->owner)) {
            ret = klnk_io_async(resource, req->buf, req->length, NULL, 0, chunk->holders[i], &threads[cnt]);
            if (ret) {
                log_resource_err(resource, "failed to send request");
                break;
            }
            cnt++;
        }
    }
    for (i = 0; i < cnt; i++)
        pthread_join(threads[i], NULL);
    free(threads);
    return ret;
}


int vres_shm_request_holders(vres_page_t *page, vres_req_t *req)
{
    int ret;
    vres_t *resource = &req->resource;
    vres_shm_req_t *shm_req = (vres_shm_req_t *)req->buf;
    vres_chunk_t *chunk = vres_page_get_chunk(resource, page);
    int cmd = shm_req->cmd;

    if (req->length > VRES_IO_MAX) {
        log_resource_err(resource, "invalid request length");
        return -EINVAL;
    }
    assert(page->owner);
    shm_req->clk = chunk->clk; // the clk piggybacked on req must be updated.
    shm_req->owner = page->owner;
    shm_req->cmd = VRES_SHM_NOTIFY_HOLDER;
    ret = vres_shm_do_request_holders(chunk, req);
    shm_req->cmd = cmd;
    log_shm_request_holders(resource, page);
    return ret;
}


int vres_shm_update_holder(vres_page_t *page, vres_req_t *req)
{
    int ret;
    vres_t *resource = &req->resource;
    int flags = vres_get_flags(resource);
    vres_id_t src = vres_get_id(resource);
    vres_chunk_t *chunk = vres_page_get_chunk(resource, page);

    if (flags & VRES_RDONLY) {
        ret = vres_page_add_holder(resource, page, chunk, src);
        if (ret) {
            log_resource_err(resource, "failed to add holder");
            return ret;
        }
    } else if (flags & VRES_RDWR) {
        vres_page_clear_holder_list(resource, chunk);

        if (vres_shm_chkown(resource, page)) {
            ret = vres_page_add_holder(resource, page, chunk, src);
            if (ret) {
                log_resource_err(resource, "failed to add holder");
                return ret;
            }
            chunk->clk += 1;
        }
    }
    if (!vres_page_dump(resource, page))
        if (vres_page_protect(resource, page))
            log_resource_warning(resource, "failed to protect");
    if (flags & VRES_CHOWN) {
        if (vres_shm_chkown(resource, page) && !vres_page_active(resource, page))
            vres_page_clear_holder_list(resource, chunk);
        page->owner = src;
        log_shm_owner(resource, "new_owner=%d", src);
    }
    log_shm_update_holder(resource, page);
    return 0;
}


int vres_shm_request_silent_holders(vres_page_t *page, vres_req_t *req)
{
    int ret = 0;
    vres_t *resource = &req->resource;
    int flags = vres_get_flags(resource);
    vres_shm_req_t *shm_req = (vres_shm_req_t *)req->buf;
    vres_chunk_t *chunk = vres_page_get_chunk(resource, page);

    if (req->length > VRES_IO_MAX) {
        log_resource_err(resource, "invalid request length");
        return -EINVAL;
    }
    if ((flags & VRES_RDWR) && !vres_shm_is_silent_holder(chunk) && vres_page_active(resource, page)) {
        if (chunk->nr_silent_holders > 0) {
            int i;
            int count = 0;
            int *htab = NULL;
            pthread_t *thread;
            int hid = chunk->hid;
            int cmd = shm_req->cmd;
            vres_file_entry_t *entry;
            char path[VRES_PATH_MAX];
            vres_id_t *silent_holders;
            int nr_sources = chunk->nr_holders;
            vres_id_t src = vres_get_id(resource);
            int nr_silent_holders = chunk->nr_silent_holders;

            vres_get_holder_path(resource, path);
            entry = vres_file_get_entry(path, nr_silent_holders * sizeof(vres_id_t), FILE_RDONLY);
            if (!entry) {
                log_resource_err(resource, "no entry");
                return -ENOENT;
            }
            silent_holders = vres_file_get_desc(entry, vres_id_t);
            htab = (int *)malloc(sizeof(int) * nr_silent_holders);
            if (!htab) {
                log_resource_err(resource, "no memory");
                vres_file_put_entry(entry);
                return -ENOMEM;
            }
            for (i = 0; i < nr_sources; i++) {
                if (chunk->holders[i] == src) {
                    nr_sources -= 1;
                    if (hid > i + 1)
                        hid -= 1;
                    break;
                }
            }
            ret = vres_shm_calc_htab(htab, nr_silent_holders, nr_sources);
            if (ret) {
                log_resource_err(resource, "failed to get htab");
                goto out;
            }
            thread = malloc(nr_silent_holders * sizeof(pthread_t));
            if (!thread) {
                log_resource_err(resource, "no memory");
                ret = -ENOMEM;
                goto out;
            }
            shm_req->cmd = VRES_SHM_NOTIFY_HOLDER;
            for (i = 0; i < nr_silent_holders; i++) {
                if ((htab[i] == hid) && (silent_holders[i] != src) && (silent_holders[i] != shm_req->owner)) {
                    ret = klnk_io_async(resource, req->buf, req->length, NULL, 0, silent_holders[i], &thread[count]);
                    if (ret) {
                        log_resource_err(resource, "failed to notify");
                        break;
                    }
                    count++;
                }
            }
            for (i = 0; i < count; i++)
                pthread_join(thread[i], NULL);
            shm_req->cmd = cmd;
            free(thread);
out:
            free(htab);
            vres_file_put_entry(entry);
            log_shm_request_silent_holders(resource);
        }
    }
    return ret;
}


bool vres_shm_check_coverage(vres_chunk_t *chunk, int line)
{
    int n = chunk->nr_holders - 1;

    if ((n >= 0) && (n < VRES_PAGE_NR_HOLDERS)
        && (line >= 0) && (line < VRES_LINE_MAX))
        return shm_htab[n][line] == chunk->hid;
    else
        return false;
}


void vres_shm_do_check_reply(vres_t *resource, vres_page_t *page, vres_chunk_t *chunk, int total, bool *head, bool *tail, bool *reply)
{
    int flags = vres_get_flags(resource);
    vres_id_t src = vres_get_id(resource);

    if (flags & VRES_RDWR)
        *reply = true;
    else
        *reply = false;
    assert(page->owner);
    if (src != page->owner) {
        if (!total) {
            if (chunk->holders[0] != src)
                *tail = chunk->hid == 1;
            else {
                if (chunk->nr_holders < 2)
                    log_resource_err(resource, "invalid holder number (holders=%d)", chunk->nr_holders);
                *tail = chunk->hid == 2;
            }
        } else
            *tail = *head;
    } else if (!total && !*reply) {
        if (chunk->holders[0] != src)
            *reply = chunk->hid == 1;
        else
            *reply = chunk->hid == 2;
    }
}


void vres_shm_check_reply(vres_t *resource, vres_page_t *page, vres_chunk_t *chunk, int total, bool *head, bool *tail, bool *reply)
{
    vres_shm_do_check_reply(resource, page, chunk, total, head, tail, reply);
    log_shm_check_reply(resource, total, head, tail, reply);
}


int vres_shm_do_check_holder(vres_page_t *page, vres_req_t *req)
{
    int i;
    int ret = 0;
    int total = 0;
    int nr_lines = 0;
    bool tail = false;
    bool head = false;
    bool reply = false;
    bool diff[VRES_LINE_MAX];
    vres_t *resource = &req->resource;
    int flags = vres_get_flags(resource);
    vres_id_t src = vres_get_id(resource);
    vres_shm_req_t *shm_req = (vres_shm_req_t *)req->buf;
    vres_chunk_t *chunk = vres_page_get_chunk(resource, page);

    if (!vres_page_active(resource, page)) {
        if (vres_shm_chkown(resource, page) && (chunk->nr_holders == 1) && (chunk->holders[0] == src)) {
            tail = true;
            goto reply;
        } else
            return 0;
    }
    if (!vres_page_dump(resource, page)) {
        if (vres_page_protect(resource, page))
            log_resource_warning(resource, "failed to protect");
    }
    if (vres_page_get_diff(chunk, shm_req->version, diff)) {
        log_resource_err(resource, "failed to get diff (pg->ver=%d, req->ver=%d)", (int)chunk->version, (int)shm_req->version);
        return -EFAULT;
    }
    for (i = 0; i < VRES_LINE_MAX; i++) {
        if (diff[i]) {
            total++;
            if (vres_shm_check_coverage(chunk, i)) {
                nr_lines++;
                if (1 == total)
                    head = true;
            }
        }
    }
    vres_shm_check_reply(resource, page, chunk, total, &head, &tail, &reply);
reply:
    if ((nr_lines > 0) || reply || head || tail) {
        char *ptr;
        vres_shm_rsp_t *rsp;
        vres_t res = *resource;
        size_t size = sizeof(vres_shm_rsp_t) + nr_lines * sizeof(vres_line_t);

        if (head)
            size += VRES_PAGE_DIFF_SIZE;
        if (tail) {
            size += sizeof(vres_shm_info_t);
            if (flags & VRES_RDONLY)
                size += chunk->nr_holders * sizeof(vres_id_t);
        }
        rsp = (vres_shm_rsp_t *)malloc(size);
        if (!rsp) {
            log_resource_err(resource, "no memory");
            return -ENOMEM;
        }
        memset(rsp, 0, size);
        if (nr_lines > 0) {
            int j = 0;

            for (i = 0; i < VRES_LINE_MAX; i++) {
                if (diff[i] & vres_shm_check_coverage(chunk, i)) {
                    vres_line_t *line = &rsp->lines[j];

                    line->digest = chunk->digest[i];
                    line->num = i;
                    memcpy(line->buf, &chunk->buf[i * VRES_LINE_SIZE], VRES_LINE_SIZE);
                    j++;
                }
            }
        }
        memcpy(&rsp->req, shm_req, sizeof(vres_shm_req_t));
        rsp->req.cmd = VRES_SHM_NOTIFY_PROPOSER;
        rsp->req.clk = chunk->version;
        rsp->nr_lines = nr_lines;
        rsp->total = total;
        ptr = (char *)&rsp[1] + nr_lines * sizeof(vres_line_t);
        if (head) {
            vres_shm_pack_diff(chunk->diff, ptr);
            ptr += VRES_PAGE_DIFF_SIZE;
            vres_set_flags(&res, VRES_DIFF);
        }
        if (tail) {
            ret = vres_shm_get_peer_info(resource, chunk, (vres_shm_info_t *)ptr);
            if (ret) {
                log_resource_err(resource, "failed to get peer info");
                goto out;
            }
            rsp->req.owner = page->owner;
            vres_set_flags(&res, VRES_CRIT);
        }
        if (!chown)
            vres_clear_flags(&res, VRES_CHOWN);
        ret = klnk_io_sync(&res, (char *)rsp, size, NULL, 0, src);
        if (ret) {
            log_resource_err(resource, "failed to send rsp, ret=%s", log_get_err(ret));
            ret = -EFAULT;
        }
out:
        log_shm_do_check_holder(page, req, rsp->lines, nr_lines, total);
        free(rsp);
    }
    return ret;
}


inline bool vres_shm_can_preempt(vres_t *resource, vres_page_t *page)
{
    if (vres_get_flags(resource) & VRES_PRIO)
        return true;
#if defined(ENABLE_PAGE_PREEMPT) && VRES_CHUNK_MAX > 1
    if (page->preemptor == vres_get_id(resource)) {
#ifdef ENABLE_TIMED_PREEMPT
        if (vres_get_time() - page->t_preempt < VRES_SHM_PREEMPT_INTV)
#endif
            return true;
    }
#endif
    return false;
}


inline bool vres_shm_check_sched(vres_t *resource, vres_page_t *page)
{
    if (vres_shm_can_preempt(resource, page)) {
#ifdef ENABLE_PREEMPT_COUNT
        page->preempt_count++;
#endif
        return false;
    } else if (vres_shm_chkown(resource, page) && (vres_page_write(resource, page) || (vres_get_flags(resource) & VRES_RDWR)))
        return true;
    else
        return false;
}


inline bool vres_shm_need_sched(vres_t *resource, vres_page_t *page)
{
#if MANAGER_TYPE == DYNAMIC_MANAGER
    return vres_dmgr_check_sched(resource, page);
#elif MANAGER_TYPE == STATIC_MANAGER
    return vres_smgr_check_sched(resource, page);
#elif defined(ENABLE_PRIORITY)
    return vres_shm_check_sched(resource, page);
#else
    return false;
#endif
}


inline bool vres_shm_need_priority(vres_t *resource, vres_page_t *page)
{
    return vres_shm_need_sched(resource, page);
}


int vres_shm_update_owner(vres_t *resource, vres_page_t *page)
{
#if MANAGER_TYPE == DYNAMIC_MANAGER
    return vres_dmgr_update_owner(resource, page);
#else
    return 0;
#endif
}


inline void vres_shm_set_preemptor(vres_page_t *page)
{
#ifdef ENABLE_PAGE_PREEMPT
    page->preemptor = page->owner;
#ifdef ENABLE_TIMED_PREEMPT
    page->t_preempt = vres_get_time();
#endif
#endif
}


int vres_shm_do_check_owner(vres_page_t *page, vres_req_t *req)
{
    int ret = 0;
    bool need_priority = false;
    vres_t *resource = &req->resource;

    if (vres_shm_need_priority(resource, page)) {
        ret = vres_prio_set_busy(resource);
        if (ret) {
            log_resource_err(resource, "failed to update priority");
            return ret;
        }
        need_priority = true;
    }
    if (vres_shm_chkown(resource, page)) {
        ret = vres_shm_request_holders(page, req);
        if (ret)
            return ret;
        ret = vres_shm_request_silent_holders(page, req);
        if (ret)
            return ret;
    }
#ifdef ENABLE_SPEC
    ret = vres_shm_spec_reply(page, req);
    if (-EAGAIN == ret)
#endif
    if (vres_shm_chkown(resource, page))
        ret = vres_shm_do_check_holder(page, req);
    if (ret && (ret != -EAGAIN))
        return ret;
    if (vres_shm_chkown(resource, page)) {
        ret = vres_shm_update_holder(page, req);
        if (ret) {
            log_resource_err(resource, "failed to update holder");
            return ret;
        }
    }
    ret = vres_shm_update_owner(resource, page);
    if (ret) {
        log_resource_err(resource, "failed to update owner");
        return ret;
    }
    if (need_priority) {
        ret = vres_prio_set_idle(resource);
        if (ret) {
            log_resource_err(resource, "failed to update priority");
            return ret;
        }
    }
    if (!ret) {
        if (vres_shm_chkown(resource, page))
            vres_shm_set_preemptor(page);
        log_shm_do_check_owner(page, req);
    }
    return -EAGAIN == ret ? 0 : ret;
}


void vres_shm_clear_updates(vres_t *resource, vres_page_t *page)
{
    vres_file_t *filp;
    char path[VRES_PATH_MAX];

    page->count = 0;
    if (!vres_pg_update(page))
        return;
    vres_get_update_path(resource, path);
    filp = vres_file_open(path, "r+");
    if (filp) {
        vres_file_truncate(filp, 0);
        vres_file_close(filp);
    }
    memset(page->collect, 0, sizeof(page->collect));
    vres_pg_clrupdate(page);
}


int vres_shm_save_updates(vres_t *resource, vres_page_t *page, vres_line_t *lines, int nr_lines)
{
    int i;
    int ret = 0;
    vres_file_entry_t *entry;
    char path[VRES_PATH_MAX];

    if (!lines || (nr_lines <= 0)) {
        log_resource_err(resource, "no content");
        return -EINVAL;
    }
    vres_get_update_path(resource, path);
    entry = vres_file_get_entry(path, 0, FILE_RDWR | FILE_CREAT);
    if (!entry) {
        log_resource_err(resource, "no entry");
        return -ENOENT;
    }
    ret = vres_file_append(entry, lines, sizeof(vres_line_t) * nr_lines);
    vres_file_put_entry(entry);
    if (ret) {
        log_resource_err(resource, "failed");
        return -EFAULT;
    }
    for (i = 0; i < nr_lines; i++) {
        int n = lines[i].num;

        page->collect[n] = true;
    }
    for (i = 0; i < VRES_LINE_MAX; i++)
        if (page->collect[i])
            ret++;
    vres_pg_mkupdate(page);
    log_shm_save_updates(resource, nr_lines, ret);
    return ret;
}


void vres_shm_load_updates(vres_t *resource, vres_page_t *page, vres_chunk_t *chunk)
{
    int i;
    int total;
    vres_line_t *lines;
    vres_file_entry_t *entry;
    char path[VRES_PATH_MAX];

    if (!vres_pg_update(page))
        return;
    vres_get_update_path(resource, path);
    entry = vres_file_get_entry(path, 0, FILE_RDONLY);
    if (!entry)
        return;
    lines = vres_file_get_desc(entry, vres_line_t);
    total = vres_file_items_count(entry, sizeof(vres_line_t));
    for (i = 0; i < total; i++) {
        vres_line_t *line = &lines[i];
        int pos = line->num * VRES_LINE_SIZE;

        chunk->digest[line->num] = line->digest;
        memcpy(&chunk->buf[pos], line->buf, VRES_LINE_SIZE);
    }
    vres_file_put_entry(entry);
}


int vres_shm_do_change_owner(vres_page_t *page, vres_req_t *req)
{
    vres_t *resource = &req->resource;

    if (vres_shm_chkown(resource, page)) {
        if (vres_shm_is_owner(resource)) {
            int ret;
            int active = 0;
            bool chown = false;
            vres_member_t member;
            int flags = vres_get_flags(resource);
            vres_id_t src = vres_get_id(resource);
            vres_chunk_t *chunk = vres_page_get_chunk(resource, page);

            ret = vres_member_lookup(resource, &member, &active);
            if (ret) {
                log_resource_err(resource, "invalid member");
                return ret;
            }
            if (((flags & VRES_RDWR) || (page->nr_candidates < VRES_PAGE_NR_HOLDERS))
                && ((member.count > 0) || (active < VRES_SHM_NR_AREAS - 1))) {
                int i;
                int total = page->nr_candidates;
                vres_member_t *cand = page->candidates;

                for (i = 0; i < total; i++) {
                    if (cand[i].id == src) {
                        if (cand[i].count >= VRES_SHM_NR_VISITS)
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
                log_shm_owner(resource, "new_owner=%d", src);
            }
        }
    }
    return 0;
}


int vres_shm_change_owner(vres_page_t *page, vres_req_t *req)
{
#if MANAGER_TYPE == DYNAMIC_MANAGER
    return vres_dmgr_change_owner(page, req);
#elif (MANAGER_TYPE == AREA_MANAGER) && defined(ENABLE_DYNAMIC_OWNER)
    return vres_shm_do_change_owner(page, req);
#else
    return 0;
#endif
}


int vres_shm_check_holder(vres_req_t *req)
{
    int ret = 0;
    void *entry;
    vres_page_t *page;
    vres_chunk_t *chunk;
    vres_t *resource = &req->resource;
    vres_shm_req_t *shm_req = (vres_shm_req_t *)req->buf;
    vres_clk_t clk = shm_req->clk;

    entry = vres_page_get(resource, &page, VRES_RDWR);
    if (!entry) {
        log_resource_err(resource, "no entry");
        return -ENOENT;
    }
    chunk = vres_page_get_chunk(resource, page);
    if (!vres_page_active(resource, page) || ((clk > chunk->version) && vres_page_wait(resource, page))) {
        ret = vres_shm_save_req(page, req);
        goto out;
    }
    ret = vres_shm_request_silent_holders(page, req);
    if (ret) {
        log_resource_err(resource, "failed to send to silent holders");
        goto out;
    }
    ret = vres_shm_do_check_holder(page, req);
    if (ret) {
        log_resource_err(resource, "failed to check active holder");
        goto out;
    }
    ret = vres_shm_update_holder(page, req);
    if (ret) {
        log_resource_err(resource, "failed to update holder");
        goto out;
    }
    log_shm_check_holder(page, req);
out:
    vres_page_put(resource, entry);
    return ret;
}


int vres_shm_notify_holder(vres_req_t *req)
{
    int ret = 0;
    void *entry;
    vres_page_t *page;
    vres_chunk_t *chunk;
    vres_t *resource = &req->resource;
    vres_shm_req_t *shm_req = (vres_shm_req_t *)req->buf;
    vres_clk_t clk = shm_req->clk;

    entry = vres_page_get(resource, &page, VRES_RDWR);
    if (!entry) {
        log_resource_err(resource, "no entry");
        return -ENOENT;
    }
    chunk = vres_page_get_chunk(resource, page);
    if (clk < chunk->version) {
        log_shm_expired_req(page, req);
        goto out;
    }
    if (!vres_page_active(resource, page) || ((clk > chunk->version) && vres_page_wait(resource, page))) {
        ret = vres_shm_save_req(page, req);
        goto out;
    }
    ret = vres_shm_request_silent_holders(page, req);
    if (ret) {
        log_resource_err(resource, "failed to send to silent holders");
        goto out;
    }
#ifdef ENABLE_SPEC
    if (!vres_shm_has_record(page, req))
        ret = vres_shm_spec_reply(page, req);
    if (-EAGAIN == ret)
#endif
        ret = vres_shm_do_check_holder(page, req);
    if (ret) {
        log_resource_err(resource, "failed to check active holder");
        goto out;
    }
    ret = vres_shm_update_holder(page, req);
    if (ret) {
        log_resource_err(resource, "failed to update holder");
        goto out;
    }
    log_shm_notify_holder(page, req);
out:
    vres_page_put(resource, entry);
    return ret;
}


inline int vres_shm_wakeup(vres_t *resource, vres_chunk_t *chunk)
{
    int ret;
    vres_t res = *resource;
    vres_shm_result_t result;
    unsigned long pos = vres_get_chunk_off(resource) * PAGE_SIZE;

    result.retval = 0;
    memcpy(result.buf, &chunk->buf[pos], PAGE_SIZE);
    vres_set_off(&res, vres_get_pgno(resource));
    ret = vres_event_set(&res, (char *)&result, sizeof(vres_shm_result_t));
    if (ret) {
        log_resource_err(resource, "failed to wakeup");
        return ret;
    }
    log_shm_wakeup(resource);
    return ret;
}


int vres_shm_deliver(vres_page_t *page, vres_req_t *req)
{
    int ret;
    vres_t *resource = &req->resource;
    int flags = vres_get_flags(resource);
    vres_id_t src = vres_get_id(resource);
    vres_shm_req_t *shm_req = (vres_shm_req_t *)req->buf;
    vres_chunk_t *chunk = vres_page_get_chunk(resource, page);
    vres_clk_t clk = shm_req->clk;

    if (src != resource->owner) {
        log_resource_err(resource, "invalid source %d", src);
        return -EINVAL;
    }
    vres_shm_load_updates(resource, page, chunk);
    ret = vres_shm_wakeup(resource, chunk);
    if (ret) {
        log_resource_err(resource, "failed to wake up");
        return ret;
    }
    if (flags & VRES_RDWR) {
        vres_page_mkwrite(resource, page);
        vres_page_clear_holder_list(resource, chunk);
        chunk->version = clk + 1;
    } else if (flags & VRES_RDONLY) {
        vres_page_mkread(resource, page);
        chunk->version = clk;
    }
    if (chunk->nr_holders < VRES_PAGE_NR_HOLDERS) {
        ret = vres_page_add_holder(resource, page, chunk, src);
        if (ret) {
            log_resource_err(resource, "failed to add holder");
            return ret;
        }
    }
    vres_pg_clrcmpl(page);
    vres_pg_clrready(page);
    vres_page_clrwait(resource, page);
    vres_shm_clear_updates(resource, page);
    chunk->hid = vres_page_get_hid(chunk, src);
    chunk->clk = chunk->version;
    if (vres_page_cand(resource, page)) {
        vres_page_clrcand(resource, page);
        vres_shm_mkown(resource, page);
        log_shm_owner(resource, "owner changes");
    }
    vres_page_mkactive(resource, page);
    vres_page_mkpresent(resource, page);
#ifdef ENABLE_TIME_SYNC
    ret = vres_prio_sync_time(resource);
    if (ret) {
        log_resource_err(resource, "failed to sync time");
        return ret;
    }
#endif
    log_shm_deliver(page, req);
    return 0;
}


int vres_shm_update_peers(vres_t *resource, vres_page_t *page, vres_shm_info_t *info)
{
    int flags = vres_get_flags(resource);

    if (flags & VRES_RDONLY) {
        int i;
        int ret;
        vres_chunk_t *chunk = vres_page_get_chunk(resource, page);

        if ((info->total > VRES_PAGE_NR_HOLDERS) || (info->total < 0)) {
            log_resource_err(resource, "invalid peer info, total=%d", info->total);
            return -EINVAL;
        }
        for (i = 0; i < info->total; i++) {
            ret = vres_page_add_holder(resource, page, chunk, info->list[i]);
            if (ret) {
                log_resource_err(resource, "failed to add holder");
                return ret;
            }
        }
    }
    if (flags & VRES_CHOWN) {
        page->owner = resource->owner;
        vres_page_mkcand(resource, page);
        log_shm_owner(resource, "new_owner=%d", resource->owner);
    }
    return 0;
}


int vres_shm_notify_proposer(vres_req_t *req)
{
    char *ptr;
    void *entry;
    int ret = 0;
    int total = 0;
    vres_page_t *page;
    bool ready = false;
    vres_chunk_t *chunk;
    vres_t *resource = &req->resource;
    int flags = vres_get_flags(resource);
    vres_shm_rsp_t *rsp = (vres_shm_rsp_t *)req->buf;
    vres_clk_t clk = rsp->req.clk;
    int nr_lines = rsp->nr_lines;

    entry = vres_page_get(resource, &page, VRES_RDWR);
    if (!entry) {
        log_resource_err(resource, "no entry");
        return -ENOENT;
    }
    chunk = vres_page_get_chunk(resource, page);
    if (!vres_page_wait(resource, page) || (rsp->req.index != chunk->index)) {
        log_resource_warning(resource, "drop a rsp, rsp->index=%d, pg->index=%d", rsp->req.index, chunk->index);
        goto out;
    }
    if (clk > chunk->clk) {
        if (!vres_shm_chkown(resource, page)) {
            vres_pg_clrready(page);
            vres_pg_clrcmpl(page);
            vres_shm_clear_updates(resource, page);
        }
        log_shm_clock_update(resource, chunk->clk, clk);
        chunk->clk = clk;
    } else if (clk < chunk->clk) {
        log_shm_expired_req(page, req);
        goto out;
    }
    ptr = ((char *)&rsp[1]) + sizeof(vres_line_t) * nr_lines;
    if (flags & VRES_DIFF) {
        vres_shm_unpack_diff(chunk->diff, ptr);
        ptr += VRES_PAGE_DIFF_SIZE;
        log_shm_page_diff(chunk->diff);
    }
    if (flags & VRES_CRIT) {
        vres_shm_info_t *info = (vres_shm_info_t *)ptr;

        if (!vres_shm_chkown(resource, page)) {
            log_shm_owner(resource, "owner=%d, nr_peers=%d", rsp->req.owner, info->total);
            page->owner = rsp->req.owner;
        }
        ret = vres_shm_update_peers(resource, page, info);
        if (ret) {
            log_resource_err(resource, "failed to update peers");
            goto out;
        }
        if (flags & VRES_RDWR)
            page->count -= info->total;
        else if (flags & VRES_RDONLY) {
            if (vres_pg_cmpl(page))
                ready = true;
            else
                vres_pg_mkready(page);
        }
    }
    if (nr_lines > 0) {
        vres_line_t *lines = rsp->lines;

        total = vres_shm_save_updates(resource, page, lines, nr_lines);
        if (total < 0) {
            log_resource_err(resource, "failed to save updates");
            ret = -EINVAL;
            goto out;
        }
    }
    if (flags & VRES_RDWR) {
        page->count++;
        if (0 == page->count)
            ready = true;
    } else if (flags & VRES_RDONLY) {
        if (total == rsp->total) {
            if (vres_pg_ready(page))
                ready = true;
            else
                vres_pg_mkcmpl(page);
        }
    } else {
        log_resource_err(resource, "invalid flags");
        ret = -EINVAL;
        goto out;
    }
    if (ready) {
        ret = vres_shm_deliver(page, req);
        if (ret)
            goto out;
        if (vres_shm_has_req(resource)) {
            log_shm_notify_proposer(page, req, true);
            vres_page_put(resource, entry);
#ifdef VRES_SHM_KEEP_ACCESSING
            vres_sleep(VRES_SHM_ACCESS_TIME);
#endif
            return vres_redo(resource, 0);
        }
    }
out:
    log_shm_notify_proposer(page, req, false);
    vres_page_put(resource, entry);
    return ret;
}


int vres_shm_call(vres_arg_t *arg)
{
    int ret;
    bool prio = false;
    vres_t *resource = &arg->resource;
    vres_file_entry_t *entry = (vres_file_entry_t *)arg->entry;
    vres_page_t *page = vres_file_get_desc(entry, vres_page_t);

    if (vres_shm_need_priority(resource, page))
        prio = true;
    vres_page_put(resource, entry);
    if (prio) {
        vres_prio_set_busy(resource);
        ret = klnk_rpc_send_to_peers(arg);
        vres_prio_set_idle(resource);
    } else
        ret = klnk_rpc_send_to_peers(arg);
    free(arg->in);
    if (ret)
        log_resource_err(resource, "failed to send");
    return ret;
}


int vres_shm_handle_zeropage(vres_t *resource, vres_page_t *page)
{
    int ret;
    int flags = vres_get_flags(resource);
    vres_id_t src = vres_get_id(resource);
    vres_chunk_t *chunk = vres_page_get_chunk(resource, page);

    assert(!page->owner);
    page->owner = resource->owner;
    vres_shm_mkown(resource, page);
    if (flags & VRES_RDWR)
        chunk->clk = 1;
    ret = vres_page_add_holder(resource, page, chunk, src);
    if (ret) {
        log_resource_err(resource, "failed to add holder");
        return ret;
    }
    if (resource->owner == src) {
        if (flags & VRES_RDWR) {
            chunk->clk = 1;
            chunk->version = 1;
            vres_page_mkwrite(resource, page);
        } else
            vres_page_mkread(resource, page);
        chunk->hid = vres_page_get_hid(chunk, src);
        vres_page_mkpresent(resource, page);
        vres_page_mkactive(resource, page);
        vres_shm_set_preemptor(page);
    } else {
        vres_shm_rsp_t *rsp;
        vres_shm_info_t *info;
        vres_t res = *resource;
        size_t size = sizeof(vres_shm_rsp_t) + sizeof(vres_shm_info_t);

        rsp = (vres_shm_rsp_t *)malloc(size);
        if (!rsp) {
            log_resource_err(resource, "no memory");
            return -ENOMEM;
        }
        memset(rsp, 0, size);
        rsp->req.index = 1;
        rsp->req.owner = page->owner;
        rsp->req.cmd = VRES_SHM_NOTIFY_PROPOSER;
        info = (vres_shm_info_t *)&rsp[1];
        if (flags & VRES_RDWR)
            info->total = 1;
        vres_set_flags(&res, VRES_CRIT);
        ret = klnk_io_sync(&res, (char *)rsp, size, NULL, 0, src);
        if (ret) {
            log_resource_err(resource, "failed to send rsp, ret=%s", log_get_err(ret));
            ret = -EFAULT;
        }
        free(rsp);
    }
    if (!ret) {
        ret = vres_shm_update_owner(resource, page);
        log_shm_handle_zeropage(resource, page);
    }
    return ret;
}


inline int vres_shm_request_owner(vres_page_t *page, vres_req_t *req, vres_id_t dest)
{
    int ret;
    vres_t *resource = &req->resource;
    vres_shm_req_t *shm_req = (vres_shm_req_t *)req->buf;
    int cmd = shm_req->cmd;

    log_shm_request_owner(resource, cmd, dest);
    shm_req->cmd = VRES_SHM_NOTIFY_OWNER;
    ret = klnk_io_sync(resource, req->buf, req->length, NULL, 0, dest);
    shm_req->cmd = cmd;
    return ret;
}


int vres_shm_do_forward(vres_page_t *page, vres_req_t *req)
{
    vres_t *resource = &req->resource;
    vres_chunk_t *chunk = vres_page_get_chunk(resource, page);
    vres_id_t dest = vres_shm_is_owner(resource) ? page->owner : -1;

    return vres_shm_request_owner(page, req, dest);
}


int vres_shm_forward(vres_page_t *page, vres_req_t *req)
{
#if MANAGER_TYPE == DYNAMIC_MANAGER
    return vres_dmgr_forward(page, req);
#elif (MANAGER_TYPE == AREA_MANAGER) && defined(ENABLE_DYNAMIC_OWNER)
    return vres_shm_do_forward(page, req);
#else
    return 0;
#endif
}


inline bool vres_shm_do_check_forward(vres_page_t *page, vres_req_t *req)
{
    vres_t *resource = &req->resource;
    vres_shm_req_t *shm_req = (vres_shm_req_t *)req->buf;
    vres_peers_t *peers = &shm_req->peers;

    return !vres_shm_chkown(resource, page) && ((shm_req->cmd != VRES_SHM_CHK_OWNER) || (peers->list[0] == resource->owner));
}


inline bool vres_shm_need_forward(vres_page_t *page, vres_req_t *req)
{
#if (MANAGER_TYPE == AREA_MANAGER) && defined(ENABLE_DYNAMIC_OWNER)
    return vres_shm_do_check_forward(page, req);
#elif (MANAGER_TYPE == DYNAMIC_MANAGER)
    return vres_dmgr_check_forward(page, req);
#else
    return false;
#endif
}


inline int vres_shm_check_priority(vres_page_t *page, vres_req_t *req, int flags)
{
#ifdef ENABLE_PRIORITY
    if (vres_shm_need_priority(&req->resource, page))
        return vres_prio_check(req, flags);
    else
#endif
        return 0;
}


int vres_shm_check_owner(vres_req_t *req, int flags)
{
    int ret = 0;
    void *entry;
    vres_page_t *page;
    vres_t *resource = &req->resource;

    entry = vres_page_get(resource, &page, VRES_RDWR);
    if (vres_shm_is_owner(resource)) {
        if (!entry) {
            entry = vres_page_get(resource, &page, VRES_RDWR | VRES_CREAT);
            if (!entry) {
                log_resource_err(resource, "no entry");
                return -ENOENT;
            }
            ret = vres_shm_handle_zeropage(resource, page);
            goto out;
        }
    } else if (!entry) {
        log_shm_owner(resource, "*owner mismatch*");
        return 0;
    }
#ifdef ENABLE_SPEC
    if (vres_shm_has_record(page, req))
        goto out;
#endif
    ret = vres_shm_check_priority(page, req, flags);
    if (ret)
        goto out;
    if (vres_shm_can_own(resource, page)) {
        if (vres_page_wait(resource, page)) {
            if (!(flags & VRES_REDO))
                ret = vres_shm_save_req(page, req);
            else {
                vres_page_put(resource, entry);
                return -EAGAIN;
            }
            goto out;
        }
    }
    ret = vres_shm_change_owner(page, req);
    if (ret) {
        log_resource_err(resource, "failed to change owner");
        goto out;
    }
    if (vres_shm_need_forward(page, req)) {
        ret = vres_shm_forward(page, req);
        if (ret) {
            if (-EAGAIN == ret)
                ret = vres_shm_save_req(page, req);
            else
                log_resource_err(resource, "failed to send to owner");
            goto out;
        }
    }
    ret = vres_shm_do_check_owner(page, req);
    log_shm_check_owner(page, req);
out:
    vres_page_put(resource, entry);
    if (!(flags & VRES_PRIO))
        return -EAGAIN == ret ? 0 : ret;
    else
        return ret;
}


int vres_shm_notify_owner(vres_req_t *req, int flags)
{
    int ret = 0;
    void *entry;
    vres_page_t *page;
    vres_t *resource = &req->resource;

    entry = vres_page_get(resource, &page, VRES_RDWR | VRES_CREAT);
    if (!entry) {
        log_resource_err(resource, "no entry");
        return -ENOENT;
    }
#ifdef ENABLE_SPEC
    if (vres_shm_has_record(page, req))
        goto out;
#endif
    ret = vres_shm_check_priority(page, req, flags);
    if (ret)
        goto out;
    if (vres_shm_can_own(resource, page)) {
        if (vres_page_wait(resource, page)) {
            if (!(flags & VRES_REDO))
                ret = vres_shm_save_req(page, req);
            else {
                vres_page_put(resource, entry);
                return -EAGAIN;
            }
            goto out;
        }
    } else if (vres_shm_need_forward(page, req)) {
        ret = vres_shm_forward(page, req);
        if (ret) {
            if (-EAGAIN == ret)
                ret = vres_shm_save_req(page, req);
            else
                log_resource_err(resource, "failed to send to owner");
        }
        goto out;
    }
    ret = vres_shm_do_check_owner(page, req);
    log_shm_notify_owner(page, req);
out:
    vres_page_put(resource, entry);
    if (!(flags & VRES_PRIO))
        return -EAGAIN == ret ? 0 : ret;
    else
        return ret;
}


inline void vres_shm_handle_err(vres_t *resource, int err)
{
    vres_t res = *resource;
    vres_shm_result_t result;
    vres_id_t src = vres_get_id(resource);

    result.retval = err;
    vres_set_op(&res, VRES_OP_REPLY);
    klnk_io_sync(&res, (char *)&result, sizeof(vres_shm_result_t), NULL, 0, src);
}


int vres_shm_propose(vres_req_t *req)
{
    int ret;
    vres_arg_t arg;
    vres_t *resource = &req->resource;

    memset(&arg, 0, sizeof(vres_arg_t));
    arg.dest = -1;
    arg.resource = *resource;
    ret = vres_shm_get_arg(resource, &arg);
    if (ret) {
        log_resource_err(resource, "failed to get argument");
        return ret;
    }
    return vres_shm_call(&arg);
}


#ifdef ENABLE_TTL
int vres_shm_check_ttl(vres_req_t *req)
{
    vres_t *resource = &req->resource;
    vres_shm_req_t *shm_req = (vres_shm_req_t *)req->buf;

    shm_req->ttl += 1;
    if (shm_req->ttl > VRES_TTL_MAX) {
        log_resource_err(resource, "cmd=%s, TTL is out of range", log_get_shm_cmd(shm_req->cmd));
        return -EINVAL;
    }
    return 0;
}
#endif


vres_reply_t *vres_shm_fault(vres_req_t *req, int flags)
{
    int ret = 0;
    vres_reply_t *reply = NULL;
    vres_t *resource = &req->resource;
    vres_shm_req_t *shm_req = (vres_shm_req_t *)req->buf;

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
        ret = vres_shm_propose(req);
        break;
    case VRES_SHM_CHK_OWNER:
        ret = vres_shm_check_owner(req, flags);
        break;
    case VRES_SHM_CHK_HOLDER:
        ret = vres_shm_check_holder(req);
        break;
    case VRES_SHM_NOTIFY_OWNER:
        ret = vres_shm_notify_owner(req, flags);
        break;
    case VRES_SHM_NOTIFY_HOLDER:
        ret = vres_shm_notify_holder(req);
        break;
    case VRES_SHM_NOTIFY_PROPOSER:
        ret = vres_shm_notify_proposer(req);
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
            log_resource_err(resource, "failed to handle (cmd=%s)", log_get_shm_cmd(shm_req->cmd));
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
    vres_shmctl_req_t *shmctl_req = (vres_shmctl_req_t *)req->buf;

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
    vres_shmctl_req_t *shmctl_req = (vres_shmctl_req_t *)req->buf;
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


int vres_shm_do_get_peers(vres_t *resource, vres_page_t *page, vres_peers_t *peers)
{
    int i;
    int cnt = 1;
    vres_chunk_t *chunk = vres_page_get_chunk(resource, page);

    if (!vres_shm_chkown(resource, page)) {
#ifdef ENABLE_DYNAMIC_OWNER
        vres_id_t page_owner = page->owner;

        if (!page_owner) {
            vres_desc_t desc;

            if (vres_lookup(resource, &desc)) {
                log_resource_err(resource, "failed to get owner");
                return -EINVAL;
            }
            page->owner = desc.id;
            page_owner = desc.id;
        }
#else
        vres_id_t page_owner = vres_get_initial_owner(resource);
#endif
#ifdef ENABLE_SPEC
        if (vres_get_flags(resource) & VRES_RDONLY) {
            int total = min(chunk->nr_candidates, VRES_SHM_NR_PEERS);

            for (i = 0; i < total; i++) {
                vres_id_t id = chunk->candidates[i].id;

                if ((id != resource->owner) && (id != page_owner)) {
                    peers->list[cnt] = id;
                    cnt++;
                    if (cnt == VRES_SHM_NR_PEERS)
                        break;
                }
            }
        }
#endif
        assert(page_owner != vres_get_id(resource));
        peers->list[0] = page_owner;
        log_shm_get_peers(resource, "page owner is contained, page_owner=%d", page_owner);
    } else {
        for (i = 0, cnt = 0; i < chunk->nr_holders; i++) {
            if (chunk->holders[i] != resource->owner) {
                peers->list[cnt] = chunk->holders[i];
                cnt++;
            }
        }
        log_shm_get_peers(resource, "only holders are contained (total=%d)", cnt);
    }
    peers->total = cnt;
    return 0;
}


int vres_shm_get_peers(vres_t *resource, vres_page_t *page, vres_peers_t *peers)
{
#if MANAGER_TYPE == STATIC_MANAGER
    return vres_smgr_get_peers(resource, page, peers);
#elif MANAGER_TYPE == DYNAMIC_MANAGER
    return vres_dmgr_get_peers(resource, page, peers);
#else
    return vres_shm_do_get_peers(resource, page, peers);
#endif
}


int vres_shm_fast_reply(vres_t *resource, vres_page_t *page, vres_arg_t *arg)
{
    int flags = vres_get_flags(resource);
    vres_id_t src = vres_get_id(resource);
    vres_chunk_t *chunk = vres_page_get_chunk(resource, page);

    if (!vres_page_active(resource, page))
        return 0;
    else if (((1 == chunk->nr_holders) && (chunk->holders[0] == src)) || (flags & VRES_RDONLY)
        || ((flags & VRES_RDWR) && vres_page_is_writable(resource, page))) {
        if (flags & VRES_RDWR)
            vres_page_mkwrite(resource, page);
        else if (flags & VRES_RDONLY) {
            unsigned long pos = vres_get_chunk_off(resource) << PAGE_SHIFT;

            memcpy(arg->buf, &chunk->buf[pos], PAGE_SIZE);
            if (!vres_page_write(resource, page))
                vres_page_mkread(resource, page);
        }
        vres_page_mkpresent(resource, page);
        log_shm_fast_reply(resource);
        return -EOK;
    }
    return 0;
}


void vres_shm_mkwait(vres_t *resource, vres_page_t *page)
{
    if (vres_shm_chkown(resource, page)) {
        if (vres_get_flags(resource) & VRES_RDWR) {
            int overlap;
            vres_chunk_t *chunk = vres_page_get_chunk(resource, page);

            overlap = vres_page_search_holder_list(resource, chunk, vres_get_id(resource));
            page->count = overlap - vres_page_calc_holders(chunk);
        } else
            vres_pg_mkready(page);
    }
    vres_page_mkwait(resource, page);
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


int vres_shm_check_arg(vres_arg_t *arg)
{
    int ret = 0;
    void *entry;
    vres_page_t *page;
    vres_t *resource = &arg->resource;
    vres_chunk_t *chunk;

    if (vres_get_op(resource) != VRES_OP_SHMFAULT)
        return 0;
    entry = vres_page_get(resource, &page, VRES_RDWR | VRES_CREAT);
    if (!entry) {
        log_resource_err(resource, "no entry, op=%s", log_get_op(vres_get_op(resource)));
        return -ENOENT;
    }
    arg->in = NULL;
    arg->index = vres_get_pgno(resource);
    chunk = vres_page_get_chunk(resource, page);
    if (vres_shm_is_owner(resource) && !page->owner) {
        ret = vres_shm_handle_zeropage(resource, page);
        if (!ret)
            ret = -EOK;
        goto out;
    }

    ret = vres_shm_fast_reply(resource, page, arg);
    if (ret)
        goto out;

    if (vres_page_wait(resource, page) || vres_shm_need_priority(resource, page)) {
        vres_req_t *req = vres_shm_get_req(resource);

        if (!req) {
            log_resource_err(resource, "failed to get request");
            ret = -EFAULT;
            goto out;
        }
        if (vres_page_wait(resource, page)) {
            ret = vres_prio_save(req);
            log_resource_warning(resource, "detect a repeat request");
        } else
            ret = vres_prio_check(req, 0);
        vres_shm_put_req(req);
    }
    log_shm_check_arg(resource, "ret=%s", log_get_err(ret));
out:
    if (ret) {
        if (-EOK == ret) {
            vres_shm_result_t *result = (vres_shm_result_t *)arg->out;

            result->retval = 0;
        }
        vres_page_put(resource, entry);
        return ret;
    }
    arg->entry = entry;
    return 0;
}


int vres_shm_get_arg(vres_t *resource, vres_arg_t *arg)
{
    int ret = 0;
    vres_page_t *page;
    vres_chunk_t *chunk;
    vres_shm_req_t *shm_req = NULL;
    const size_t size = sizeof(vres_shm_req_t) + VRES_PAGE_NR_HOLDERS * sizeof(vres_id_t);

    if (vres_get_op(resource) != VRES_OP_SHMFAULT)
        return 0;
    if (!arg->entry) {
        arg->entry = vres_page_get(resource, &page, VRES_RDWR | VRES_CREAT);
        if (!arg->entry) {
            log_resource_err(resource, "no entry");
            return -ENOENT;
        }
    } else
        page = vres_file_get_desc((vres_file_entry_t *)arg->entry, vres_page_t);
    shm_req = (vres_shm_req_t *)malloc(size);
    if (!shm_req) {
        log_resource_err(resource, "no memory");
        ret = -ENOMEM;
        goto out;
    }
    memset(shm_req, 0, size);
    chunk = vres_page_get_chunk(resource, page);
    shm_req->clk = chunk->clk;
    shm_req->version = chunk->version;
    shm_req->index = vres_page_alloc_index(chunk);
    if (vres_shm_chkown(resource, page)) {
        shm_req->cmd = VRES_SHM_CHK_HOLDER;
        vres_shm_set_preemptor(page);
    } else
        shm_req->cmd = VRES_SHM_CHK_OWNER;
    ret = vres_shm_get_peers(resource, page, &shm_req->peers);
    if (ret) {
        log_resource_err(resource, "failed to get peers");
        goto out;
    }
    shm_req->owner = page->owner;
    vres_shm_mkwait(resource, page);
    arg->inlen = size;
    arg->in = (char *)shm_req;
    arg->resource = *resource;
    if (shm_req->peers.total > 0)
        arg->peers = &shm_req->peers;
    log_shm_get_arg(resource, arg);
out:
    if (ret) {
        if (shm_req)
            free(shm_req);
        vres_page_put(resource, arg->entry);
    }
    return ret;
}


int vres_shm_save_page(vres_t *resource, char *buf, size_t size)
{
    int ret = 0;
    void *entry;
    vres_page_t *page;

    if (size != PAGE_SIZE) {
        log_resource_err(resource, "failed (invalid size)");
        return -EINVAL;
    }
    log_shm_save_page(resource);
    entry = vres_page_get(resource, &page, VRES_RDWR);
    if (vres_shm_chkown(resource, page)) {
        ret = vres_page_update(resource, page, vres_get_chunk_off(resource), buf);
        if (!ret)
            vres_page_mkdump(resource, page);
        else
            log_resource_err(resource, "failed to update");
    }
    vres_page_put(resource, entry);
    return ret;
}
