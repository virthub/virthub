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

int vres_shm_htab[VRES_PAGE_NR_HOLDERS][VRES_LINE_MAX];

static inline int vres_shm_calc_htab(int *htab, size_t length, int nr_holders)
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


void vres_shm_init_htab()
{
    int i;
    static bool init = false;

    if (init)
        return;
    else
        init = true;
    for (i = 0; i < VRES_PAGE_NR_HOLDERS; i++)
        vres_shm_calc_htab(vres_shm_htab[i], VRES_LINE_MAX, i + 1);
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
#if MANAGER_TYPE == STATIC_MANAGER
    return vres_smgr_is_owner(resource);
#elif MANAGER_TYPE == DYNAMIC_MANAGER
    return vres_dmgr_is_owner(resource);
#else
    return vres_is_owner(resource);
#endif
}


int vres_shm_init(vres_t *resource)
{
    int ret = 0;

    vres_shm_init_htab();
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


static inline void vres_shm_pack_diff(int (*diff)[VRES_LINE_MAX], char *out)
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


static inline void vres_shm_unpack_diff(int (*dest)[VRES_LINE_MAX], char *src)
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
    vres_pg_mkredo(page);
    log_shm_save_req(req);
    return 0;
}


int *vres_shm_get_htab(vres_page_t *page)
{
    if (page->nr_holders <= 0)
        return NULL;
    else
        return vres_shm_htab[page->nr_holders - 1];
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


static inline int vres_shm_get_peer_info(vres_t *resource, vres_page_t *page, vres_shm_peer_info_t *info)
{
    int flags = vres_get_flags(resource);

    if (flags & VRES_RDWR) {
        vres_id_t src = vres_get_id(resource);
        int overlap = vres_page_search_holder_list(resource, page, src);

        info->total = vres_page_calc_holders(page) - overlap;
    } else if (flags & VRES_RDONLY) {
        int i;

        for (i = 0; i < page->nr_holders; i++)
            info->list[i] = page->holders[i];
        info->total = page->nr_holders;
    }
    log_shm_get_peer_info(resource, info);
    return 0;
}


#ifdef ENABLE_FAST_REPLY
static inline int vres_shm_get_peer_info_fast(vres_t *resource, vres_page_t *page, vres_shm_peer_info_t *info)
{
    int flags = vres_get_flags(resource);
    int ret = vres_shm_get_peer_info(resource, page, info);

    if (!ret && (flags & VRES_RDWR))
        if (!vres_page_search_holder_list(resource, page, resource->owner))
            info->total++;
    return ret;
}


static inline int vres_shm_cache_add(vres_page_t *page, vres_req_t *req)
{
    int ret = 0;
    bool update = false;
    vres_file_entry_t *entry;
    char path[VRES_PATH_MAX];
    vres_t *resource = &req->resource;
    vres_id_t id = vres_get_id(resource);
    vres_version_t version = page->version;
    vres_shmfault_arg_t *arg = (vres_shmfault_arg_t *)req->buf;
    vres_index_t index = arg->index;

    vres_get_cache_path(resource, path);
    entry = vres_file_get_entry(path, 0, FILE_RDWR);
    if (entry) {
        int total = vres_file_items_count(entry, sizeof(vres_shm_cache_t));

        if (total > 0) {
            int i;
            vres_shm_cache_t *caches = vres_file_get_desc(entry, vres_shm_cache_t);

            for (i = 0; i < total; i++) {
                if (caches[i].id == id) {
                    update = true;
                    caches[i].index = index;
                    caches[i].version = version;
                    break;
                }
            }
        }
    }
    if (!update) {
        vres_shm_cache_t cache;

        if (!entry) {
            entry = vres_file_get_entry(path, 0, FILE_RDWR | FILE_CREAT);
            if (!entry) {
                log_resource_err(resource, "no entry");
                return -ENOENT;
            }
        }
        cache.index = index;
        cache.version = version;
        cache.id = id;
        ret = vres_file_append(entry, &cache, sizeof(vres_shm_cache_t));
    }
    vres_file_put_entry(entry);
    log_shm_cache(resource, id, index, version);
    return ret;
}


static inline bool vres_shm_check_request(vres_page_t *page, vres_req_t *req)
{
    bool ret = false;
    char path[VRES_PATH_MAX];
    vres_file_entry_t *entry;
    vres_t *resource = &req->resource;

    vres_get_cache_path(resource, path);
    entry = vres_file_get_entry(path, 0, FILE_RDONLY);
    if (entry) {
        int i;
        int total;
        vres_shm_cache_t *caches;
        vres_id_t id = vres_get_id(resource);
        vres_version_t version = page->version;
        vres_shmfault_arg_t *arg = (vres_shmfault_arg_t *)req->buf;
        vres_index_t index = arg->index;

        caches = vres_file_get_desc(entry, vres_shm_cache_t);
        total = vres_file_items_count(entry, sizeof(vres_shm_cache_t));
        for (i = 0; i < total; i++) {
            if ((caches[i].id == id) && (caches[i].index == index) && (caches[i].version >= version)) {
                log_shm_cache(resource, id, index, caches[i].version);
                ret = true;
                break;
            }
        }
        vres_file_put_entry(entry);
    }
    return ret;
}


inline int vres_shm_check_fast_reply(vres_page_t *page, vres_req_t *req, int *hid, int *nr_peers)
{
    int i, j;
    int cnt = 0;
    vres_t *resource = &req->resource;
    vres_id_t peers[VRES_PAGE_NR_HOLDERS];
    vres_shmfault_arg_t *arg = (vres_shmfault_arg_t *)req->buf;

    if (page->nr_holders < 2)
        return -EAGAIN;
    *hid = 0;
    *nr_peers = 0;
    for (i = 0; i < page->nr_holders; i++) {
        for (j = 0; j < arg->peers.total; j++) {
            if (arg->peers.list[j] == page->holders[i]) {
                peers[cnt] = arg->peers.list[j];
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
    log_shm_check_fast_reply(req, *hid, *nr_peers);
    return 0;
}


int vres_shm_fast_reply(vres_page_t *page, vres_req_t *req)
{
    int i;
    int clk;
    int hid = 0;
    int ret = 0;
    int head = 0;
    int tail = 0;
    int total = -1;
    int nr_lines = 0;
    int nr_peers = 0;
    int diff[VRES_LINE_MAX];
    int htab[VRES_LINE_MAX];
    vres_t *resource = &req->resource;
    int flags = vres_get_flags(resource);
    vres_shmfault_arg_t *arg = (vres_shmfault_arg_t *)req->buf;
    int notify = VRES_SHM_NOTIFY_HOLDER == arg->cmd;
    int reply = notify && (flags & VRES_RDWR);

    if (!vres_pg_own(page)) {
        if (!vres_pg_active(page))
            return 0;
        clk = page->version;
    } else {
        tail = 1;
        clk = page->clk;
    }
    ret = vres_shm_check_fast_reply(page, req, &hid, &nr_peers);
    if (ret)
        goto cache;
    if (vres_pg_active(page) && (hid > 0)) {
        memset(diff, 0, sizeof(diff));
        if (vres_page_get_diff(page, arg->version, diff)) {
            log_resource_err(resource, "failed to differentiate (page->version=%d, arg->version=%d)", (int)page->version, (int)arg->version);
            return -EINVAL;
        }
        if (vres_shm_calc_htab(htab, VRES_LINE_MAX, nr_peers)) {
            log_resource_err(resource, "failed to get htab");
            return -EINVAL;
        }
        total = 0;
        for (i = 0; i < VRES_LINE_MAX; i++) {
            if (diff[i] != 0) {
                total++;
                if (htab[i] == hid) {
                    nr_lines++;
                    if (1 == total)
                        head = 1;
                }
            }
        }
        if (!total && !reply)
            reply = hid == 1;
    }
    if ((nr_lines > 0) || reply || head || tail) {
        char *ptr;
        vres_t res = *resource;
        vres_id_t src = vres_get_id(resource);
        vres_shm_notify_proposer_arg_t *new_arg;
        size_t size = sizeof(vres_shm_notify_proposer_arg_t) + nr_lines * sizeof(vres_line_t);

        if (head)
            size += VRES_PAGE_DIFF_SIZE;
        if (tail) {
            size += sizeof(vres_shm_peer_info_t);
            if (flags & VRES_RDONLY)
                size += page->nr_holders * sizeof(vres_id_t);
        }
        new_arg = (vres_shm_notify_proposer_arg_t *)calloc(1, size);
        if (!new_arg) {
            log_resource_err(resource, "no memory");
            return -ENOMEM;
        }
        if (nr_lines > 0) {
            int j = 0;

            for (i = 0; i < VRES_LINE_MAX; i++) {
                if (diff[i] && (htab[i] == hid)) {
                    vres_line_t *line = &new_arg->lines[j];

                    line->num = i;
                    line->digest = page->digest[i];
                    memcpy(line->buf, &page->buf[i * VRES_LINE_SIZE], VRES_LINE_SIZE);
                    j++;
                }
            }
        }
        memcpy(&new_arg->arg, arg, sizeof(vres_shmfault_arg_t));
        new_arg->arg.cmd = VRES_SHM_NOTIFY_PROPOSER;
        new_arg->arg.clk = clk;
        new_arg->nr_lines = nr_lines;
        new_arg->total = total;
        ptr = (char *)&new_arg[1] + nr_lines * sizeof(vres_line_t);
        if (head) {
            vres_shm_pack_diff(page->diff, ptr);
            ptr += VRES_PAGE_DIFF_SIZE;
            vres_set_flags(&res, VRES_DIFF);
        }
        if (tail) {
            ret = vres_shm_get_peer_info_fast(resource, page, (vres_shm_peer_info_t *)ptr);
            if (ret) {
                log_resource_err(resource, "failed to get peer info");
                goto out;
            }
            new_arg->arg.owner = page->owner;
            vres_set_flags(&res, VRES_CRIT);
        }
        if (klnk_io_sync(&res, (char *)new_arg, size, NULL, 0, &src)) {
            log_resource_err(resource, "failed to send request");
            ret = -EFAULT;
        }
out:
        log_lines(resource, new_arg->lines, nr_lines, total);
        free(new_arg);
        if (ret)
            return ret;
    }
    log_shm_fast_reply(resource, hid, nr_peers);
cache:
    if (vres_pg_own(page) || (hid > 0) || notify) {
        if (vres_shm_cache_add(page, req)) {
            log_resource_err(resource, "failed to cache");
            ret = -EFAULT;
        }
    }
    return ret;
}
#endif


int vres_shm_do_request_holders(vres_page_t *page, vres_req_t *req)
{
    int i;
    int ret = 0;
    int cnt = 0;
    vres_t *resource = &req->resource;
    vres_id_t src = vres_get_id(resource);
    pthread_t *threads = malloc(page->nr_holders * sizeof(pthread_t));

    if (!threads) {
        log_resource_err(resource, "no memory");
        return -ENOMEM;
    }
    for (i = 0; i < page->nr_holders; i++) {
        if ((page->holders[i] != src) && (page->holders[i] != resource->owner)) {
            ret = klnk_io_async(resource, req->buf, req->length, NULL, 0, &page->holders[i], &threads[cnt]);
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
    vres_shmfault_arg_t *arg = (vres_shmfault_arg_t *)req->buf;
    int cmd = arg->cmd;

    if (req->length > VRES_IO_MAX) {
        log_resource_err(resource, "invalid request length");
        return -EINVAL;
    }
    // Note that the clk piggybacked on req must be updated.
    arg->clk = page->clk;
    arg->owner = resource->owner;
    arg->cmd = VRES_SHM_NOTIFY_HOLDER;
    ret = vres_shm_do_request_holders(page, req);
    arg->cmd = cmd;
    log_shm_request_holders(resource);
    return ret;
}


static inline void vres_shm_do_detect_owner(vres_page_t *page, vres_req_t *req)
{
    vres_t *resource = &req->resource;
    int flags = vres_get_flags(resource);

    if (!(flags & VRES_CHOWN) && !vres_pg_own(page)) {
        vres_shmfault_arg_t *arg = (vres_shmfault_arg_t *)req->buf;

        page->owner = arg->owner;
        log_shm_owner(resource, "new_owner=%d", arg->owner);
    }
}


static inline void vres_shm_detect_owner(vres_page_t *page, vres_req_t *req)
{
#if (MANAGER_TYPE == NO_MANAGER) && defined(ENABLE_DYNAMIC_OWNER)
    vres_shm_do_detect_owner(page, req);
#endif
}


int vres_shm_update_holder(vres_page_t *page, vres_req_t *req)
{
    int ret;
    vres_t *resource = &req->resource;
    int flags = vres_get_flags(resource);
    vres_id_t src = vres_get_id(resource);

    if (flags & VRES_RDONLY) {
        ret = vres_page_add_holder(resource, page, src);
        if (ret) {
            log_resource_err(resource, "failed to add holder");
            return ret;
        }
    } else if (flags & VRES_RDWR) {
        vres_page_clear_holder_list(resource, page);

        if (vres_pg_own(page)) {
            ret = vres_page_add_holder(resource, page, src);
            if (ret) {
                log_resource_err(resource, "failed to add holder");
                return ret;
            }
            page->clk += 1;
        }
    }
    if (!vres_pg_pgsave(page))
        if (vres_page_protect(resource, page))
            log_resource_warning(resource, "failed to protect");
    if (flags & VRES_CHOWN) {
        if (vres_pg_own(page)) {
            vres_pg_clrown(page);
            if (!vres_pg_active(page))
                vres_page_clear_holder_list(resource, page);
        }
        page->owner = src;
        log_shm_owner(resource, "new_owner=%d", src);
    }
    vres_shm_detect_owner(page, req);
    log_shm_update_holder(resource, page);
    return 0;
}


int vres_shm_request_silent_holders(vres_page_t *page, vres_req_t *req)
{
    int ret = 0;
    vres_t *resource = &req->resource;
    int flags = vres_get_flags(resource);
    vres_shmfault_arg_t *arg = (vres_shmfault_arg_t *)req->buf;

    if (req->length > VRES_IO_MAX) {
        log_resource_err(resource, "invalid request length");
        return -EINVAL;
    }
    if ((flags & VRES_RDWR) && !vres_shm_is_silent_holder(page) && vres_pg_active(page)) {
        if (page->nr_silent_holders > 0) {
            int i;
            int count = 0;
            int *htab = NULL;
            pthread_t *thread;
            int hid = page->hid;
            int cmd = arg->cmd;
            vres_file_entry_t *entry;
            char path[VRES_PATH_MAX];
            vres_id_t *silent_holders;
            int nr_sources = page->nr_holders;
            vres_id_t src = vres_get_id(resource);
            int nr_silent_holders = page->nr_silent_holders;

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
                if (page->holders[i] == src) {
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
            arg->cmd = VRES_SHM_NOTIFY_HOLDER;
            for (i = 0; i < nr_silent_holders; i++) {
                if ((htab[i] == hid) && (silent_holders[i] != src) && (silent_holders[i] != arg->owner)) {
                    ret = klnk_io_async(resource, req->buf, req->length, NULL, 0, &silent_holders[i], &thread[count]);
                    if (ret) {
                        log_resource_err(resource, "failed to notify");
                        break;
                    }
                    count++;
                }
            }
            for (i = 0; i < count; i++)
                pthread_join(thread[i], NULL);
            arg->cmd = cmd;
            free(thread);
out:
            free(htab);
            vres_file_put_entry(entry);
            log_shm_request_silent_holders(resource);
        }
    }
    return ret;
}


int vres_shm_check_coverage(vres_page_t *page, int line)
{
    int n = page->nr_holders - 1;

    if ((n >= 0) && (n < VRES_PAGE_NR_HOLDERS)
        && (line >= 0) && (line < VRES_LINE_MAX))
        return vres_shm_htab[n][line] == page->hid;
    else
        return 0;
}


void vres_shm_do_check_reply(vres_t *resource, vres_page_t *page, int total, bool *head, bool *tail, bool *reply)
{
    int flags = vres_get_flags(resource);
    vres_id_t src = vres_get_id(resource);

    if (flags & VRES_RDWR)
        *reply = true;
    else
        *reply = false;
    if (src != page->owner) {
        if (!total) {
            if (page->holders[0] != src)
                *tail = page->hid == 1;
            else
                *tail = page->hid == 2;
        } else
            *tail = *head;
    } else if (!total && !*reply) {
        if (page->holders[0] != src)
            *reply = page->hid == 1;
        else
            *reply = page->hid == 2;
    }
}


void vres_shm_check_reply(vres_t *resource, vres_page_t *page, int total, bool *head, bool *tail, bool *reply)
{
    vres_shm_do_check_reply(resource, page, total, head, tail, reply);
    log_shm_check_reply(resource, total, head, tail, reply);
}


int vres_shm_check_active_holder(vres_page_t *page, vres_req_t *req)
{
    int i;
    int ret = 0;
    int total = 0;
    int nr_lines = 0;
    bool tail = false;
    bool head = false;
    bool reply = false;
    int diff[VRES_LINE_MAX];
    vres_t *resource = &req->resource;
    int flags = vres_get_flags(resource);
    vres_id_t src = vres_get_id(resource);
    vres_shmfault_arg_t *arg = (vres_shmfault_arg_t *)req->buf;

    if (!vres_pg_active(page))
        return 0;
    if (!vres_pg_pgsave(page)) {
        if (vres_page_protect(resource, page))
            log_resource_warning(resource, "failed to protect");
    }
    if (vres_page_get_diff(page, arg->version, diff)) {
        log_resource_err(resource, "failed to differentiate (page->version=%d, arg->version=%d)", (int)page->version, (int)arg->version);
        return -EFAULT;
    }
    for (i = 0; i < VRES_LINE_MAX; i++) {
        if (diff[i] != 0) {
            total++;
            if (vres_shm_check_coverage(page, i)) {
                nr_lines++;
                if (1 == total)
                    head = true;
            }
        }
    }
    vres_shm_check_reply(resource, page, total, &head, &tail, &reply);
    if ((nr_lines > 0) || reply || head || tail) {
        char *ptr;
        vres_t res = *resource;
        vres_shm_notify_proposer_arg_t *new_arg;
        size_t size = sizeof(vres_shm_notify_proposer_arg_t) + nr_lines * sizeof(vres_line_t);

        if (head)
            size += VRES_PAGE_DIFF_SIZE;
        if (tail) {
            size += sizeof(vres_shm_peer_info_t);
            if (flags & VRES_RDONLY)
                size += page->nr_holders * sizeof(vres_id_t);
        }
        new_arg = (vres_shm_notify_proposer_arg_t *)calloc(1, size);
        if (!new_arg) {
            log_resource_err(resource, "no memory");
            return -ENOMEM;
        }
        if (nr_lines > 0) {
            int j = 0;

            for (i = 0; i < VRES_LINE_MAX; i++) {
                if (diff[i] & vres_shm_check_coverage(page, i)) {
                    vres_line_t *line = &new_arg->lines[j];

                    line->digest = page->digest[i];
                    line->num = i;
                    memcpy(line->buf, &page->buf[i * VRES_LINE_SIZE], VRES_LINE_SIZE);
                    j++;
                }
            }
        }
        memcpy(&new_arg->arg, arg, sizeof(vres_shmfault_arg_t));
        new_arg->arg.cmd = VRES_SHM_NOTIFY_PROPOSER;
        new_arg->arg.clk = page->version;
        new_arg->nr_lines = nr_lines;
        new_arg->total = total;
        ptr = (char *)&new_arg[1] + nr_lines * sizeof(vres_line_t);
        if (head) {
            vres_shm_pack_diff(page->diff, ptr);
            ptr += VRES_PAGE_DIFF_SIZE;
            vres_set_flags(&res, VRES_DIFF);
        }
        if (tail) {
            ret = vres_shm_get_peer_info(resource, page, (vres_shm_peer_info_t *)ptr);
            if (ret) {
                log_resource_err(resource, "failed to get peer info");
                goto out;
            }
            new_arg->arg.owner = page->owner;
            vres_set_flags(&res, VRES_CRIT);
        }
        if (klnk_io_sync(&res, (char *)new_arg, size, NULL, 0, &src)) {
            log_resource_err(resource, "failed to send request");
            ret = -EFAULT;
        }
out:
        log_lines(resource, new_arg->lines, nr_lines, total);
        log_shm_check_active_holder(page, req, nr_lines, total);
        free(new_arg);
    }
    return ret;
}


static inline bool vres_shm_check_sched(vres_t *resource, vres_page_t *page)
{
    int flags = vres_get_flags(resource);

    if (vres_pg_own(page) && !(flags & VRES_CHOWN) && (vres_pg_write(page) || (flags & VRES_RDWR)))
        return true;
    else
        return false;
}


static inline bool vres_shm_is_scheduler(vres_t *resource, vres_page_t *page)
{
#if MANAGER_TYPE == DYNAMIC_MANAGER
    return vres_dmgr_check_sched(resource, page);
#elif MANAGER_TYPE == STATIC_MANAGER
    return vres_smgr_check_sched(resource, page);
#elif (MANAGER_TYPE == NO_MANAGER) && defined(ENABLE_DYNAMIC_OWNER)
    return vres_shm_check_sched(resource, page);
#else
    return false;
#endif
}


static inline bool vres_shm_need_priority(vres_t *resource, vres_page_t *page)
{
#ifdef ENABLE_PRIORITY
    return vres_shm_is_scheduler(resource, page);
#else
    return false;
#endif
}


int vres_shm_update_owner(vres_page_t *page, vres_req_t *req)
{
#ifdef DYNAMIC_MANAGER
    return vres_dmgr_update_owner(page, req);
#else
    return 0;
#endif
}


int vres_shm_check_active_owner(vres_page_t *page, vres_req_t *req)
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
    if (vres_pg_own(page)) {
        ret = vres_shm_request_holders(page, req);
        if (ret)
            return ret;
        ret = vres_shm_request_silent_holders(page, req);
        if (ret)
            return ret;
    }
#ifdef ENABLE_FAST_REPLY
    ret = vres_shm_fast_reply(page, req);
    if (-EAGAIN == ret)
#endif
    if (vres_pg_own(page))
        ret = vres_shm_check_active_holder(page, req);
    if (ret && (ret != -EAGAIN))
        return ret;
    if (vres_pg_own(page)) {
        ret = vres_shm_update_holder(page, req);
        if (ret) {
            log_resource_err(resource, "failed to update holder");
            return ret;
        }
    } else {
        ret = vres_shm_update_owner(page, req);
        if (ret) {
            log_resource_err(resource, "failed to update owner");
            return ret;
        }
    }
    if (need_priority) {
        ret = vres_prio_set_idle(resource);
        if (ret) {
            log_resource_err(resource, "failed to update priority");
            return ret;
        }
    }
    if (!ret)
        log_shm_check_active_owner(page, req);
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

        page->collect[n] = 1;
    }
    for (i = 0; i < VRES_LINE_MAX; i++)
        if (page->collect[i])
            ret++;
    vres_pg_mkupdate(page);
    log_shm_save_updates(resource, nr_lines, ret);
    return ret;
}


void vres_shm_load_updates(vres_t *resource, vres_page_t *page)
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

        page->digest[line->num] = line->digest;
        memcpy(&page->buf[pos], line->buf, VRES_LINE_SIZE);
    }
    vres_file_put_entry(entry);
}


int vres_shm_do_change_owner(vres_page_t *page, vres_req_t *req)
{
    if (vres_pg_own(page)) {
        vres_t *resource = &req->resource;

        if (vres_shm_is_owner(resource)) {
            int ret;
            int active = 0;
            bool chown = false;
            vres_member_t member;
            int flags = vres_get_flags(resource);
            vres_id_t src = vres_get_id(resource);

            ret = vres_member_lookup(resource, &member, &active);
            if (ret) {
                log_resource_err(resource, "invalid member");
                return ret;
            }
            if (((flags & VRES_RDWR) || (page->nr_holders < VRES_PAGE_NR_HOLDERS))
                && ((member.count > 0) || (active < VRES_SHM_NR_AREAS - 1))) {
                if (VRES_SHM_NR_VISITS > 0) {
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
                } else
                    chown = true;
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
#elif (MANAGER_TYPE == NO_MANAGER) && defined(ENABLE_DYNAMIC_OWNER)
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
    vres_t *resource = &req->resource;
    vres_shmfault_arg_t *arg = (vres_shmfault_arg_t *)req->buf;
    vres_clk_t clk = arg->clk;

    entry = vres_page_get(resource, &page, VRES_PAGE_RDWR);
    if (!entry) {
        log_resource_err(resource, "no entry");
        return -ENOENT;
    }
    if (!vres_pg_active(page) || ((clk > page->version) && vres_pg_wait(page))) {
        ret = vres_shm_save_req(page, req);
        goto out;
    }
    ret = vres_shm_request_silent_holders(page, req);
    if (ret) {
        log_resource_err(resource, "failed to send to silent holders");
        goto out;
    }
    ret = vres_shm_check_active_holder(page, req);
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
    vres_t *resource = &req->resource;
    vres_shmfault_arg_t *arg = (vres_shmfault_arg_t *)req->buf;
    vres_clk_t clk = arg->clk;

    entry = vres_page_get(resource, &page, VRES_PAGE_RDWR);
    if (!entry) {
        log_resource_err(resource, "no entry");
        return -ENOENT;
    }
    if (clk < page->version) {
        log_shm_expired_req(page, req);
        goto out;
    }
    if (!vres_pg_active(page) || ((clk > page->version) && vres_pg_wait(page))) {
        ret = vres_shm_save_req(page, req);
        goto out;
    }
    ret = vres_shm_request_silent_holders(page, req);
    if (ret) {
        log_resource_err(resource, "failed to send to silent holders");
        goto out;
    }
#ifdef ENABLE_FAST_REPLY
    if (!vres_shm_check_request(page, req))
        ret = vres_shm_fast_reply(page, req);
    if (-EAGAIN == ret)
#endif
        ret = vres_shm_check_active_holder(page, req);
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


static inline void vres_shm_wait(vres_time_t timeout)
{
    pthread_cond_t cond;
    struct timespec time;
    pthread_mutex_t mutex;

    if (timeout <= 0)
        return;
    pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&cond, NULL);
    vres_set_timeout(&time, timeout);
    pthread_mutex_lock(&mutex);
    pthread_cond_timedwait(&cond, &mutex, &time);
    pthread_mutex_unlock(&mutex);
}


int vres_shm_wakeup(vres_t *resource, vres_page_t *page)
{
    int ret;
    vres_shmfault_result_t result;

    result.retval = 0;
    memcpy(result.buf, page->buf, PAGE_SIZE);
    ret = vres_event_set(resource, (char *)&result, sizeof(vres_shmfault_result_t));
    if (ret) {
        log_resource_err(resource, "failed to set event");
        return ret;
    }
    do {
        vres_shm_wait(VRES_SHM_CHECK_INTERVAL);
    } while (!sys_shm_present(resource->key,
                              resource->owner,
                              vres_get_off(resource),
                              vres_get_flags(resource)));

    log_shm_wakeup(resource);
    return ret;
}


int vres_shm_deliver(vres_page_t *page, vres_req_t *req)
{
    int ret;
    vres_t *resource = &req->resource;
    int flags = vres_get_flags(resource);
    vres_id_t src = vres_get_id(resource);
    vres_shmfault_arg_t *arg = (vres_shmfault_arg_t *)req->buf;
    vres_clk_t clk = arg->clk;

    if (src != resource->owner) {
        log_resource_err(resource, "invalid source %d", src);
        return -EINVAL;
    }
    vres_shm_load_updates(resource, page);
    ret = vres_shm_wakeup(resource, page);
    if (ret) {
        log_resource_err(resource, "failed to wake up");
        return ret;
    }
    if (flags & VRES_RDWR) {
        vres_pg_mkwrite(page);
        page->version = clk + 1;
        vres_page_clear_holder_list(resource, page);
    } else if (flags & VRES_RDONLY) {
        vres_pg_mkread(page);
        page->version = clk;
    }
    if (page->nr_holders < VRES_PAGE_NR_HOLDERS) {
        ret = vres_page_add_holder(resource, page, src);
        if (ret) {
            log_resource_err(resource, "failed to add holder");
            return ret;
        }
    }
    vres_pg_clrwait(page);
    vres_pg_clrready(page);
    vres_pg_clrcurr(page);
    vres_shm_clear_updates(resource, page);
    page->hid = vres_page_get_hid(page, src);
    page->clk = page->version;
    if (vres_pg_cand(page)) {
        vres_pg_clrcand(page);
        vres_pg_mkown(page);
        log_shm_owner(resource, "*owner changes*");
    }
    vres_pg_mkactive(page);
#ifdef ENABLE_TIME_SYNC
    ret = vres_prio_sync_time(resource);
    if (ret) {
        log_resource_err(resource, "failed to sync time");
        return ret;
    }
#endif
    log_shm_deliver(req);
    return 0;
}


int vres_shm_update_peers(vres_t *resource, vres_page_t *page, vres_shm_peer_info_t *info)
{
    int ret;
    int flags = vres_get_flags(resource);

    if (flags & VRES_RDONLY) {
        int i;

        if ((info->total > VRES_PAGE_NR_HOLDERS) || (info->total < 0)) {
            log_resource_err(resource, "invalid peer info");
            return -EINVAL;
        }
        for (i = 0; i < info->total; i++) {
            ret = vres_page_add_holder(resource, page, info->list[i]);
            if (ret) {
                log_resource_err(resource, "failed to add holder");
                return ret;
            }
        }
    }
    if (flags & VRES_CHOWN) {
        vres_pg_mkcand(page);
        page->owner = resource->owner;
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
    vres_t *resource = &req->resource;
    int flags = vres_get_flags(resource);
    vres_shm_notify_proposer_arg_t *arg = (vres_shm_notify_proposer_arg_t *)req->buf;
    vres_clk_t clk = arg->arg.clk;
    int nr_lines = arg->nr_lines;

    entry = vres_page_get(resource, &page, VRES_PAGE_RDWR);
    if (!entry) {
        log_resource_err(resource, "no entry");
        return -ENOENT;
    }
    if (!vres_pg_wait(page) || (arg->arg.index != page->index)) {
        log_resource_warning(resource, "drop a request, arg.index=%d, page.index=%d", arg->arg.index, page->index);
        goto out;
    }
    if (clk > page->clk) {
        if (!vres_pg_own(page)) {
            vres_pg_clrready(page);
            vres_pg_clrcurr(page);
            vres_shm_clear_updates(resource, page);
        }
        log_shm_clock_update(resource, page->clk, clk);
        page->clk = clk;
    } else if (clk < page->clk) {
        log_shm_expired_req(page, req);
        goto out;
    }
    ptr = ((char *)&arg[1]) + sizeof(vres_line_t) * nr_lines;
    if (flags & VRES_DIFF) {
        vres_shm_unpack_diff(page->diff, ptr);
        ptr += VRES_PAGE_DIFF_SIZE;
        log_page_diff(page->diff);
    }
    if (flags & VRES_CRIT) {
        vres_shm_peer_info_t *info = (vres_shm_peer_info_t *)ptr;

        if (!vres_pg_own(page)) {
            log_shm_owner(resource, "current_owner=%d, nr_peers=%d", arg->arg.owner, info->total);
            page->owner = arg->arg.owner;
        }
        ret = vres_shm_update_peers(resource, page, info);
        if (ret) {
            log_resource_err(resource, "failed to update peers");
            goto out;
        }
        if (flags & VRES_RDWR)
            page->count -= info->total;
        else if (flags & VRES_RDONLY) {
            if (vres_pg_curr(page))
                ready = true;
            else
                vres_pg_mkready(page);
        }
    }
    if (nr_lines > 0) {
        vres_line_t *lines = arg->lines;

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
        if (total == arg->total) {
            if (vres_pg_ready(page))
                ready = true;
            else
                vres_pg_mkcurr(page);
        }
    } else {
        log_resource_err(resource, "invalid flag");
        ret = -EINVAL;
        goto out;
    }
    if (ready) {
        ret = vres_shm_deliver(page, req);
        if (ret)
            goto out;
        if (vres_pg_redo(page)) {
            log_shm_notify_proposer(page, req);
            vres_pg_clrredo(page);
            vres_page_put(resource, entry);
            return vres_redo(resource, 0);
        }
    }
out:
    log_shm_notify_proposer(page, req);
    vres_page_put(resource, entry);
    return ret;
}


int vres_shm_call(vres_arg_t *arg)
{
    int ret;
    bool need_priority = false;
    vres_t *resource = &arg->resource;
    vres_file_entry_t *entry = (vres_file_entry_t *)arg->entry;
    vres_page_t *page = vres_file_get_desc(entry, vres_page_t);

    if (vres_shm_need_priority(resource, page)) {
        ret = vres_prio_set_busy(resource);
        if (ret) {
            log_resource_err(resource, "failed to update priority");
            goto out;
        }
        need_priority = true;
    }
    ret = klnk_broadcast(arg);
    if (ret) {
        log_resource_err(resource, "failed to broadcast");
        goto out;
    }
    if (need_priority) {
        ret = vres_prio_set_idle(resource);
        if (ret) {
            log_resource_err(resource, "failed to update priority");
            goto out;
        }
    }
out:
    vres_page_put(resource, entry);
    return ret;
}


int vres_shm_handle_zeropage(vres_t *resource, vres_page_t *page)
{
    int ret;
    int flags = vres_get_flags(resource);
    vres_id_t src = vres_get_id(resource);

    memset(page, 0, sizeof(vres_page_t));
    page->owner = resource->owner;
    log_shm_owner(resource, "first_owner=%d", page->owner);
    vres_pg_mkown(page);
    if (flags & VRES_RDWR)
        page->clk = 1;
    ret = vres_page_add_holder(resource, page, src);
    if (ret) {
        log_resource_err(resource, "failed to add holder");
        return ret;
    }
    if (resource->owner == src) {
        if (flags & VRES_RDWR) {
            page->clk = 1;
            page->version = 1;
            vres_pg_mkwrite(page);
        } else
            vres_pg_mkread(page);
        page->hid = vres_page_get_hid(page, src);
        vres_pg_mkactive(page);
    } else {
        vres_t res = *resource;
        vres_shm_peer_info_t *info;
        vres_shm_notify_proposer_arg_t *arg;
        size_t size = sizeof(vres_shm_notify_proposer_arg_t) + sizeof(vres_shm_peer_info_t);

        arg = (vres_shm_notify_proposer_arg_t *)calloc(1, size);
        if (!arg) {
            log_resource_err(resource, "no memory");
            return -ENOMEM;
        }
        arg->arg.index = 1;
        arg->arg.owner = page->owner;
        arg->arg.cmd = VRES_SHM_NOTIFY_PROPOSER;
        info = (vres_shm_peer_info_t *)&arg[1];
        if (flags & VRES_RDWR)
            info->total = 1;
        vres_set_flags(&res, VRES_CRIT);
        if (klnk_io_sync(&res, (char *)arg, size, NULL, 0, &src)) {
            log_resource_err(resource, "failed to send request");
            ret = -EFAULT;
        }
out:
        free(arg);
    }
    log_shm_handle_zeropage(resource, page);
    return ret;
}


static inline int vres_shm_request_owner(vres_page_t *page, vres_req_t *req, vres_id_t *dest)
{
    int ret;
    vres_t *resource = &req->resource;
    vres_shmfault_arg_t *arg = (vres_shmfault_arg_t *)req->buf;
    int cmd = arg->cmd;

    log_shm_request_owner(resource, cmd, dest);
    arg->cmd = VRES_SHM_NOTIFY_OWNER;
    ret = klnk_io_sync(resource, req->buf, req->length, NULL, 0, dest);
    arg->cmd = cmd;
    return ret;
}


int vres_shm_do_forward(vres_page_t *page, vres_req_t *req)
{
    vres_t *resource = &req->resource;
    vres_id_t *dest = vres_shm_is_owner(resource) ? &page->owner : NULL;

    return vres_shm_request_owner(page, req, dest);
}


int vres_shm_forward(vres_page_t *page, vres_req_t *req)
{
#if MANAGER_TYPE == DYNAMIC_MANAGER
    return vres_dmgr_forward(page, req);
#elif (MANAGER_TYPE == NO_MANAGER) && defined(ENABLE_DYNAMIC_OWNER)
    return vres_shm_do_forward(page, req);
#else
    return 0;
#endif
}


static inline bool vres_shm_check_forward(vres_page_t *page, vres_req_t *req)
{
    vres_t *resource = &req->resource;
    vres_id_t src = vres_get_id(resource);
    vres_shmfault_arg_t *arg = (vres_shmfault_arg_t *)req->buf;
    vres_peers_t *peers = &arg->peers;

    return !vres_pg_own(page) && (src != page->owner) && ((arg->cmd != VRES_SHM_CHK_OWNER) || (peers->list[0] == resource->owner));
}


static inline bool vres_shm_need_forward(vres_page_t *page, vres_req_t *req)
{
#if (MANAGER_TYPE == DYNAMIC_MANAGER) || ((MANAGER_TYPE == NO_MANAGER) && defined(ENABLE_DYNAMIC_OWNER))
    return vres_shm_check_forward(page, req);
#else
    return false;
#endif
}


static inline int vres_shm_check_priority(vres_page_t *page, vres_req_t *req, int flags)
{
#ifdef ENABLE_PRIORITY
    vres_t *resource = &req->resource;

    if (vres_shm_need_priority(resource, page))
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

    entry = vres_page_get(resource, &page, VRES_PAGE_RDWR);
    if (vres_shm_is_owner(resource)) {
        if (!entry) {
            entry = vres_page_get(resource, &page, VRES_PAGE_RDWR | VRES_PAGE_CREAT);
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
#ifdef ENABLE_FAST_REPLY
    if (vres_shm_check_request(page, req))
        goto out;
#endif
    ret = vres_shm_check_priority(page, req, flags);
    if (ret)
        goto out;
    if (vres_pg_own(page)) {
        if (vres_pg_wait(page)) {
            ret = vres_shm_save_req(page, req);
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
    ret = vres_shm_check_active_owner(page, req);
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

    entry = vres_page_get(resource, &page, VRES_PAGE_RDWR | VRES_PAGE_CREAT);
    if (!entry) {
        log_resource_err(resource, "no entry");
        return -ENOENT;
    }
#ifdef ENABLE_FAST_REPLY
    if (vres_shm_check_request(page, req))
        goto out;
#endif
    ret = vres_shm_check_priority(page, req, flags);
    if (ret)
        goto out;
    if (vres_pg_own(page)) {
        if (vres_pg_wait(page)) {
            ret = vres_shm_save_req(page, req);
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
    ret = vres_shm_check_active_owner(page, req);
    log_shm_notify_owner(page, req);
out:
    vres_page_put(resource, entry);
    if (!(flags & VRES_PRIO))
        return -EAGAIN == ret ? 0 : ret;
    else
        return ret;
}


static inline void vres_shm_handle_err(vres_t *resource, int err)
{
    vres_t res = *resource;
    vres_shmfault_result_t result;
    vres_id_t src = vres_get_id(resource);

    result.retval = err;
    vres_set_op(&res, VRES_OP_REPLY);
    klnk_io_sync(&res, (char *)&result, sizeof(vres_shmfault_result_t), NULL, 0, &src);
}


int vres_shm_propose(vres_req_t *req)
{
    int ret;
    vres_arg_t arg;
    vres_t *resource = &req->resource;

    memset(&arg, 0, sizeof(vres_arg_t));
    arg.resource = *resource;
    ret = vres_shm_get_arg(resource, &arg);
    if (ret) {
        log_resource_err(resource, "failed to get arg");
        return ret;
    }
    ret = vres_shm_call(&arg);
    vres_shm_put_arg(&arg);
    return ret;
}


int vres_shm_check_ttl(vres_req_t *req)
{
    vres_t *resource = &req->resource;
    vres_shmfault_arg_t *arg = (vres_shmfault_arg_t *)req->buf;

    arg->ttl += 1;
    if (arg->ttl > VRES_TTL_MAX) {
        log_resource_err(resource, "cmd=%s, TTL is out of range", log_get_shm_cmd(arg->cmd));
        return -EINVAL;
    }
    return 0;
}


vres_reply_t *vres_shm_fault(vres_req_t *req, int flags)
{
    int ret = 0;
    vres_t *resource = &req->resource;
    vres_shmfault_arg_t *arg = (vres_shmfault_arg_t *)req->buf;

#ifdef ENABLE_TTL
    ret = vres_shm_check_ttl(req);
    if (ret) {
        vres_shm_handle_err(resource, ret);
        return NULL;
    }
#endif
    switch (arg->cmd) {
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
        log_resource_warning(resource, "%s is not supported", log_get_shm_cmd(arg->cmd));
        break;
    }
    if (ret) {
        if (flags & VRES_REDO)
            return vres_reply_err(ret);
        else {
            vres_shm_handle_err(resource, ret);
            log_resource_err(resource, "*failed* to handle (cmd=%s)", log_get_shm_cmd(arg->cmd));
        }
    }
    return NULL;
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
    char buf[sizeof(vres_req_t) + sizeof(vres_shmctl_arg_t)];
    vres_req_t *req = (vres_req_t *)buf;
    vres_shmctl_arg_t *arg = (vres_shmctl_arg_t *)req->buf;

    log_shm_destroy(resource);
    arg->cmd = IPC_RMID;
    req->resource = *resource;
    req->length = sizeof(vres_shmctl_arg_t);
    vres_set_op(&req->resource, VRES_OP_SHMCTL);
    vres_set_id(&req->resource, resource->owner);
    ret = vres_get_peer(resource->owner, &req->src);
    if (ret)
        return ret;
    return vres_shm_rmid(req);
}


vres_reply_t *vres_shm_shmctl(vres_req_t *req, int flags)
{
    int ret = 0;
    vres_reply_t *reply = NULL;
    vres_t *resource = &req->resource;
    vres_shmctl_result_t *result = NULL;
    int outlen = sizeof(vres_shmctl_result_t);
    vres_shmctl_arg_t *arg = (vres_shmctl_arg_t *)req->buf;
    int cmd = arg->cmd;

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
        //Fixme: global information of shm is not available
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
        struct shmid_ds *buf = (struct shmid_ds *)&arg[1];

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

    if (!vres_pg_own(page)) {
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
#ifdef ENABLE_FAST_REPLY
        if (vres_get_flags(resource) & VRES_RDONLY) {
            int total = min(page->nr_candidates, VRES_SHM_NR_PROBABLE_HOLDERS);

            for (i = 0; i < total; i++) {
                vres_id_t id = page->candidates[i].id;
                if ((id != resource->owner) && (id != page_owner)) {
                    peers->list[cnt] = id;
                    cnt++;
                }
            }
        }
#endif
        peers->list[0] = page_owner;
    } else {
        for (i = 0, cnt = 0; i < page->nr_holders; i++) {
            if (page->holders[i] != resource->owner) {
                peers->list[cnt] = page->holders[i];
                cnt++;
            }
        }
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


int vres_shm_bypass(vres_t *resource, vres_page_t *page)
{
    int flags = vres_get_flags(resource);
    vres_id_t src = vres_get_id(resource);

    if (!vres_pg_active(page))
        return 0;
    if (((1 == page->nr_holders) && (page->holders[0] == src)) || (flags & VRES_RDONLY)
        || ((flags & VRES_RDWR) && vres_pg_write(page))) {
        if (flags & VRES_RDWR)
            vres_pg_mkwrite(page);
        log_shm_bypass(resource);
        return -EOK;
    }
    return 0;
}


void vres_shm_mkwait(vres_t *resource, vres_page_t *page)
{
    if (vres_pg_own(page)) {
        if (vres_get_flags(resource) & VRES_RDWR) {
            int overlap;

            overlap = vres_page_search_holder_list(resource, page, vres_get_id(resource));
            page->count = overlap - vres_page_calc_holders(page);
        } else
            vres_pg_mkready(page);
    }
    vres_pg_mkwait(page);
}


vres_req_t *vres_shm_get_req(vres_t *resource)
{
    vres_req_t *req;
    vres_shmfault_arg_t *arg;
    size_t size = sizeof(vres_req_t) + sizeof(vres_shmfault_arg_t);

    req = (vres_req_t *)calloc(1, size);
    if (!req) {
        log_resource_err(resource, "no memory");
        return NULL;
    }
    req->resource = *resource;
    req->length = sizeof(vres_shmfault_arg_t);
    arg = (vres_shmfault_arg_t *)req->buf;
    arg->cmd = VRES_SHM_PROPOSE;
#ifdef ENABLE_LIVE_TIME
    if (vres_prio_check_live_time(resource, &arg->prio, &arg->live)) {
        log_resource_err(resource, "failed to get live time");
        free(req);
        return NULL;
    }
#endif
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

    if (vres_get_op(resource) != VRES_OP_SHMFAULT)
        return 0;
    entry = vres_page_get(resource, &page, VRES_PAGE_RDWR | VRES_PAGE_CREAT);
    if (!entry) {
        log_resource_err(resource, "no entry, op=%s", log_get_op(vres_get_op(resource)));
        return -ENOENT;
    }
    arg->in = NULL;
    arg->index = vres_get_off(resource);
    if (vres_shm_is_owner(resource) && (0 == page->owner)) {
        ret = vres_shm_handle_zeropage(resource, page);
        if (!ret)
            ret = -EOK;
        goto out;
    }
    ret = vres_shm_bypass(resource, page);
    if (ret)
        goto out;
    if (vres_shm_need_priority(resource, page)) {
        vres_req_t *req = vres_shm_get_req(resource);

        if (!req) {
            log_resource_err(resource, "failed to get request");
            ret = -EFAULT;
            goto out;
        }
        ret = vres_prio_check(req, 0);
        vres_shm_put_req(req);
    }
    log_shm_check_arg(resource);
out:
    if (ret) {
        if (-EOK == ret) {
            vres_shmfault_result_t *result = (vres_shmfault_result_t *)arg->out;

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
    size_t size;
    vres_page_t *page;
    vres_shmfault_arg_t *shmfault_arg = NULL;

    if (vres_get_op(resource) != VRES_OP_SHMFAULT)
        return 0;
    if (!arg->entry) {
        arg->entry = vres_page_get(resource, &page, VRES_PAGE_RDWR | VRES_PAGE_CREAT);
        if (!arg->entry) {
            log_resource_err(resource, "no entry");
            return -ENOENT;
        }
    } else
        page = vres_file_get_desc((vres_file_entry_t *)arg->entry, vres_page_t);
    size = sizeof(vres_shmfault_arg_t) + VRES_PAGE_NR_HOLDERS * sizeof(vres_id_t);
    shmfault_arg = (vres_shmfault_arg_t *)calloc(1, size);
    if (!shmfault_arg) {
        log_resource_err(resource, "no memory");
        ret = -ENOMEM;
        goto out;
    }
    shmfault_arg->clk = page->clk;
    shmfault_arg->version = page->version;
    shmfault_arg->index = vres_page_update_index(page);
    if (vres_pg_own(page))
        shmfault_arg->cmd = VRES_SHM_CHK_HOLDER;
    else
        shmfault_arg->cmd = VRES_SHM_CHK_OWNER;
    ret = vres_shm_get_peers(resource, page, &shmfault_arg->peers);
    if (ret) {
        log_resource_err(resource, "failed to get peers");
        goto out;
    }
    shmfault_arg->owner = page->owner;
#ifdef ENABLE_LIVE_TIME
    if (!vres_pg_own(page)) {
        ret = vres_prio_check_live_time(resource, &shmfault_arg->prio, &shmfault_arg->live);
        if (ret) {
            log_resource_err(resource, "failed to get priority");
            goto out;
        }
    }
#endif
    vres_shm_mkwait(resource, page);
    arg->inlen = size;
    arg->in = (char *)shmfault_arg;
    arg->resource = *resource;
    if (shmfault_arg->peers.total > 0)
        arg->peers = &shmfault_arg->peers;
    log_shm_get_arg(resource, arg);
out:
    if (ret) {
        if (shmfault_arg)
            free(shmfault_arg);
        vres_page_put(resource, arg->entry);
    }
    return ret;
}


void vres_shm_put_arg(vres_arg_t *arg)
{
    vres_t *resource = &arg->resource;

    if (VRES_OP_SHMFAULT == vres_get_op(resource)) {
        if (arg->in) {
            free(arg->in);
            arg->in = NULL;
        }
    }
}


int vres_shm_save_page(vres_t *resource, char *buf, size_t size)
{
    int ret = 0;
    void *entry;
    vres_page_t *page;
    unsigned long pgoff =vres_get_off(resource);

    if (size != PAGE_SIZE) {
        log_resource_err(resource, "failed (invalid size)");
        return -EINVAL;
    }
    log_shm_save_page(resource);
    entry = vres_page_get(resource, &page, VRES_PAGE_RDWR);
    if (vres_pg_own(page)) {
        ret = vres_page_update(page, buf);
        if (!ret)
            vres_pg_mkpgsave(page);
        else
            log_resource_err(resource, "failed to update");
    }
    vres_page_put(resource, entry);
    return ret;
}
