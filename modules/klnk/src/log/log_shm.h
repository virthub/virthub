#ifndef _LOG_SHM_H
#define _LOG_SHM_H

#include "log.h"

#define LOG_SHM_NR_BITS  32
#define LOG_SHM_NR_BYTES 16

static inline unsigned long _convert2flags(bool ready, bool active, bool update, bool cmpl, bool wait)
{
    unsigned long flags = 0;

    if (wait)
        flags |= 0x00002000;
    if (ready)
        flags |= 0x00010000;
    if (active)
        flags |= 0x00020000;
    if (update)
        flags |= 0x00080000;
    if (cmpl)
        flags |= 0x00100000;
    return flags;
}

#define _page_flags(resource, region, chunk) (region->flags[vres_get_region_off(resource)] | _convert2flags(chunk->stat.ready, chunk->stat.active, chunk->stat.update, chunk->stat.cmpl, chunk->stat.wait))

#ifdef ENABLE_PREEMPT_COUNT
#define log_shm_region(resource, region, str) do { \
    log_str(str, ", preempt_count=%d", region->preempt_count); \
} while (0)
#else
#define log_shm_region(resource, region, str) do {} while (0)
#endif

#define log_shm_peers(peer_list, total, str) do { \
    int i; \
    if (!peer_list || !total) \
        break; \
    log_str(str, ", peers=["); \
    for (i = 0; i < total - 1; i++) \
        log_str(str, "%d, ", peer_list[i]); \
    log_str(str, "%d]", peer_list[i]); \
} while (0)

#define log_shm_lines(lines, total, str) do { \
    int i; \
    if (!lines || !total) \
        break; \
    log_str(str, ", lines=["); \
    for (i = 0; i < total - 1; i++) \
        log_str(str, "%d, ", lines[i].num); \
    log_str(str, "%d]", lines[i].num); \
} while (0)

#define log_shm_list_holders(holders, nr_holders, str) do { \
    int i; \
    log_str(str, ", holders=["); \
    for (i = 0; i < nr_holders - 1; i++) \
        log_str(str, "%d, ", holders[i]); \
    log_str(str, "%d]", holders[i]); \
} while (0)

#define log_shm_chunk_holders(chunk, str) do { \
    if (chunk->nr_holders) \
        log_shm_list_holders(chunk->holders, chunk->nr_holders, str); \
} while (0)

#define log_shm_holders(resource, region, chunk_id, str) do { \
    vres_chunk_t *chunk; \
    if (chunk_id >= 0) { \
        assert(chunk_id < VRES_CHUNK_MAX); \
        vres_slice_t *slice = vres_region_get_slice(resource, region); \
        chunk = &slice->chunks[chunk_id]; \
    } else \
        chunk = vres_region_get_chunk(resource, region); \
    log_shm_chunk_holders(chunk, str); \
} while (0)

#define log_shm_req(req, str) do { \
    if (req) { \
        int i; \
        log_str(str, ", req_ver=["); \
        for (i = 0; i < VRES_CHUNK_MAX - 1; i++) \
            log_str(str, "%lu, ", (req)->chunk_ver[i]); \
        log_str(str, "%lu]", (req)->chunk_ver[VRES_CHUNK_MAX - 1]); \
        log_str(str, " <idx:%d>", (req)->index); \
    } \
} while (0)

#define log_shm_slice_ver(resource, region, str) do { \
    int i; \
    int slice_id = vres_get_slice(resource); \
    vres_chunk_t *chunk = region->slices[slice_id].chunks; \
    log_str(str, ", slice_ver=["); \
    for (i = 0; i < VRES_CHUNK_MAX - 1; i++) \
        log_str(str, "%lu, ", chunk[i].shadow_version); \
    log_str(str, "%lu]", chunk[i].shadow_version); \
} while (0)

#ifdef LOG_SHM_SHOW_CHUNK_HOLDERS
#define log_shm_show_chunk_holders(resource, chunk, fmt, ...) do { \
    char tmp[LOG_STR_LEN] = {0}; \
    log_resource_str(resource, tmp); \
    log_shm_chunk_holders(chunk, tmp); \
    log_str(tmp, ", "fmt, ##__VA_ARGS__); \
    log_ln("%s", tmp); \
} while (0)
#else
#define log_shm_show_chunk_holders(...) do {} while (0)
#endif

#ifdef LOG_SHM_CHECK_OWNER
#define log_shm_check_owner(region, req) do { \
    char tmp[LOG_STR_LEN] = {0}; \
    vres_t *resource = &(req)->resource; \
    vres_shm_req_t *shm_req = (vres_shm_req_t *)(req->buf); \
    vres_slice_t *slice = vres_region_get_slice(resource, region); \
    log_resource_str(resource, tmp); \
    log_str(tmp, ", owner=%d", slice->owner); \
    log_shm_holders(resource, region, -1, tmp); \
    log_shm_region(resource, region, tmp); \
    log_shm_slice_ver(resource, region, tmp); \
    log_shm_req(shm_req, tmp); \
    log_ln("%s", tmp); \
} while (0)
#else
#define log_shm_check_owner(...) do {} while (0)
#endif

#ifdef LOG_SHM_DO_CHECK_OWNER
#define log_shm_do_check_owner(region, req) do { \
    char tmp[LOG_STR_LEN] = {0}; \
    vres_t *resource = &(req)->resource; \
    vres_shm_req_t *shm_req = (vres_shm_req_t *)(req->buf); \
    log_resource_str(resource, tmp); \
    log_shm_holders(resource, region, -1, tmp); \
    log_shm_region(resource, region, tmp); \
    log_shm_req(shm_req, tmp); \
    log_ln("%s", tmp); \
} while (0)
#else
#define log_shm_do_check_owner(...) do {} while (0)
#endif

#ifdef LOG_SHM_SEND_OWNER_REQUEST
#define log_shm_send_owner_request(region, req) do { \
    char tmp[LOG_STR_LEN] = {0}; \
    vres_t *resource = &(req)->resource; \
    vres_shm_req_t *shm_req = (vres_shm_req_t *)(req->buf); \
    log_resource_str(resource, tmp); \
    log_shm_holders(resource, region, -1, tmp); \
    log_shm_region(resource, region, tmp); \
    log_shm_slice_ver(resource, region, tmp); \
    log_shm_req(shm_req, tmp); \
    log_ln("%s", tmp); \
} while (0)
#else
#define log_shm_send_owner_request(...) do {} while (0)
#endif

#ifdef LOG_SHM_SPEC_REPLY
#define log_shm_spec_reply(resource, hid, nr_peers) log_resource_info(resource, "hid=%d, nr_peers=%d", hid, nr_peers)
#else
#define log_shm_spec_reply(...) do {} while (0)
#endif

#ifdef LOG_SHM_CHECK_HOLDER
#define log_shm_check_holder(region, req) do { \
    char tmp[LOG_STR_LEN] = {0}; \
    vres_t *resource = &(req)->resource; \
    vres_shm_req_t *shm_req = (vres_shm_req_t *)(req->buf); \
    log_resource_str(resource, tmp); \
    log_shm_region(resource, region, tmp); \
    log_shm_req(shm_req, tmp); \
    log_ln("%s", tmp); \
} while (0)
#else
#define log_shm_check_holder(...) do {} while (0)
#endif

#ifdef LOG_SHM_DO_CHECK_HOLDER
#define log_shm_do_check_holder(region, req, rsp) do { \
    char tmp[LOG_STR_LEN] = {0}; \
    vres_t *resource = &(req)->resource; \
    vres_shm_req_t *shm_req = (vres_shm_req_t *)(req->buf); \
    log_resource_str(resource, tmp); \
    log_shm_region(resource, region, tmp); \
    log_shm_req(shm_req, tmp); \
} while (0)
#else
#define log_shm_do_check_holder(...) do {} while (0)
#endif

#ifdef LOG_SHM_NOTIFY_OWNER
#define log_shm_notify_owner(region, req) do { \
    char tmp[LOG_STR_LEN] = {0}; \
    vres_t *resource = &(req)->resource; \
    vres_shm_req_t *shm_req = (vres_shm_req_t *)(req->buf); \
    log_resource_str(resource, tmp); \
    log_shm_holders(resource, region, -1, tmp); \
    log_shm_region(resource, region, tmp); \
    log_shm_req(shm_req, tmp); \
    log_ln("%s", tmp); \
} while (0)
#else
#define log_shm_notify_owner(...) do {} while (0)
#endif

#ifdef LOG_SHM_COMPLETE
#define log_shm_complete(resource, region, chunk_page) do { \
    int chunk_id = vres_get_chunk(resource); \
    log_shm_region_content(resource, region, chunk_id, 1, ", chunk_page=0x%lx, >> complete locally <<", (unsigned long)chunk_page); \
} while (0)
#else
#define log_shm_complete(...) do {} while (0)
#endif

#ifdef LOG_SHM_NOTIFY_HOLDER
#define log_shm_notify_holder(region, req) do { \
    char tmp[LOG_STR_LEN] = {0}; \
    vres_t *resource = &(req)->resource; \
    vres_shm_req_t *shm_req = (vres_shm_req_t *)(req->buf); \
    log_resource_str(resource, tmp); \
    log_shm_region(resource, region, tmp); \
    log_shm_req(shm_req, tmp); \
    log_ln("%s", tmp); \
} while (0)
#else
#define log_shm_notify_holder(...) do {} while (0)
#endif

#ifdef LOG_SHM_NOTIFY_PROPOSER
#define log_shm_notify_proposer(region, req, redo) do { \
    char tmp[LOG_STR_LEN] = {0}; \
    vres_t *resource = &(req)->resource; \
    vres_shm_req_t *shm_req = (vres_shm_req_t *)(req->buf); \
    log_resource_str(resource, tmp); \
    log_shm_region(resource, region, tmp); \
    log_shm_req(shm_req, tmp); \
    log_ln("%s (pending=%d)", tmp, redo); \
} while (0)
#else
#define log_shm_notify_proposer(...) do {} while (0)
#endif

#ifdef LOG_SHM_HANDLE_ZEROPAGE
#define log_shm_handle_zeropage(resource, region, req) do { \
    char tmp[LOG_STR_LEN] = {0}; \
    int slice_id = vres_get_slice(resource); \
    int chunk_id = vres_get_chunk(resource); \
    vres_slice_t *slice = vres_region_get_slice(resource, region); \
    vres_shm_req_t *shm_req = (vres_shm_req_t *)(req->buf); \
    log_resource_str(resource, tmp); \
    log_shm_region(resource, region, tmp); \
    if (req) \
        log_ln("%s, slice_id=%d, chunk_id=%d, owner=%d <idx:%d>", tmp, slice_id, chunk_id, slice->owner, shm_req->index); \
    else \
        log_ln("%s, slice_id=%d, chunk_id=%d, owner=%d <idx:0>", tmp, slice_id, chunk_id, slice->owner); \
} while (0)
#else
#define log_shm_handle_zeropage(...) do {} while (0)
#endif

#ifdef LOG_SHM_CHECK_ARGS
#define log_shm_check_arg log_resource_info
#else
#define log_shm_check_arg(...) do {} while (0)
#endif

#ifdef LOG_SHM_SAVE_UPDATES
#define log_shm_save_updates(resource, chunk_lines) do { \
    char tmp[LOG_STR_LEN] = {0}; \
    log_resource_str(resource, tmp); \
    log_str(tmp, ", nr_lines=%d", chunk_lines->nr_lines); \
    log_shm_lines(chunk_lines->lines, chunk_lines->nr_lines, tmp); \
    log_ln("%s", tmp); \
} while (0)
#else
#define log_shm_save_updates(...) do {} while (0)
#endif

#ifdef LOG_SHM_GET_ARGS
#define log_shm_get_arg(resource, slice, chunk, arg) do { \
    char tmp[LOG_STR_LEN] = {0}; \
    vres_peers_t *peers = (arg)->peers; \
    vres_id_t *peer_list = peers ? peers->list : NULL; \
    int total = peers ? peers->total : 0; \
    vres_region_t *region; \
    if ((arg)->in == (arg)->buf) \
        break; \
    region = (vres_region_t *)(arg)->ptr; \
    log_resource_str(resource, tmp); \
    log_str(tmp, ", slice=0x%lx, chunk=0x%lx", (unsigned long)slice, (unsigned long)chunk); \
    log_shm_peers(peer_list, total, tmp); \
    log_shm_region(resource, region, tmp); \
    log_shm_req((vres_shm_req_t *)(arg)->in, tmp); \
    log_ln("%s", tmp); \
} while (0)
#else
#define log_shm_get_arg(...) do {} while (0)
#endif

#ifdef LOG_SHM_SAVE_REQ
#define log_shm_save_req(region, req, chunk, chunk_id) do { \
    char tmp[LOG_STR_LEN] = {0}; \
    vres_t *resource = &(req)->resource; \
    vres_shm_req_t *shm_req = (vres_shm_req_t *)(req->buf); \
    log_resource_str(resource, tmp); \
    log_str(tmp, ", redo=%d, chunk_id=%d", chunk->redo, chunk_id); \
    log_shm_region(resource, region, tmp); \
    log_shm_req(shm_req, tmp); \
    log_ln("%s", tmp); \
} while (0)
#else
#define log_shm_save_req(...) do {} while (0)
#endif

#ifdef LOG_SHM_WAKEUP
#define log_shm_wakeup(resource, region, chunk, chunk_id, chunk_page) log_shm_region_content(resource, region, chunk_id, 1, ", chunk_page=0x%lx", (unsigned long)chunk_page)
#else
#define log_shm_wakeup(...) do {} while (0)
#endif

#ifdef LOG_SHM_DELIVER
#define log_shm_deliver log_resource_info
#else
#define log_shm_deliver(...) do {} while (0)
#endif

#ifdef LOG_SHM_RECORD
#define log_shm_record(resource, path, chunk, chunk_id, shm_req) log_resource_info(resource, "path=%s, chunk=0x%lx, chunk_id=%d, chunk_index=%d, chunk_ver=%lld <idx:%d>", path, (unsigned long)chunk, chunk_id, (chunk)->index, (chunk)->version, (shm_req)->index)
#else
#define log_shm_record(...) do {} while (0)
#endif

#ifdef LOG_SHM_DESTORY
#define log_shm_destroy log_resource_ln
#else
#define log_shm_destroy(...) do {} while (0)
#endif

#ifdef LOG_SHM_REQUEST_OWNER
#define log_shm_request_owner(resource, cmd, dest) do { \
    if (dest != -1) \
        log_resource_info(resource, "dest=%d", dest); \
    else \
        log_resource_info(resource, "dest=None"); \
} while (0)
#else
#define log_shm_request_owner(...) do {} while (0)
#endif

#ifdef LOG_SHM_PACK_RSP
#define log_shm_pack_rsp log_resource_info
#else
#define log_shm_pack_rsp(...) do {} while (0)
#endif

#ifdef LOG_SHM_GET_RSP
#define log_shm_get_rsp log_resource_info
#else
#define log_shm_get_rsp(...) do {} while (0)
#endif

#ifdef LOG_SHM_SAVE_PAGE
#define log_shm_save_page log_resource_info
#else
#define log_shm_save_page(...) do {} while (0)
#endif


#ifdef LOG_SHM_LOAD_UPDATES
#define log_shm_load_updates log_resource_info
#else
#define log_shm_load_updates(...) do {} while (0)
#endif

#ifdef LOG_SHM_CHANGE_OWNER
#define log_shm_change_owner log_resource_info
#else
#define log_shm_change_owner(...) do {} while (0)
#endif

#ifdef LOG_SHM_MKWAIT
#define log_shm_mkwait log_resource_info
#else
#define log_shm_mkwait(...) do {} while (0)
#endif

#ifdef LOG_SHM_CREATE
#define log_shm_create log_resource_info
#else
#define log_shm_create(...) do {} while (0)
#endif

#ifdef LOG_SHM_DEBUG
#define log_shm_debug log_resource_debug
#else
#define log_shm_debug(...) do {} while (0)
#endif

#ifdef LOG_SHM_GET_PEER_INFO
#define log_shm_get_peer_info(resource, info) log_resource_info(resource, "nr_holders=%d", (info)->nr_holders)
#else
#define log_shm_get_peer_info(...) do {} while (0)
#endif

#ifdef LOG_SHM_SHOW_HOLDERS
#define log_shm_show_holders(resource, holders, nr_holders, fmt, ...) do { \
    char tmp[LOG_STR_LEN] = {0}; \
    log_resource_str(resource, tmp); \
    log_str(tmp, ", " fmt, ##__VA_ARGS__); \
    log_shm_list_holders(holders, nr_holders, tmp); \
    log_ln("%s", tmp); \
} while (0)
#else
#define log_shm_show_holders(...) do {} while (0)
#endif

#ifdef LOG_SHM_GET_SPEC_PEERS
#define log_get_spec_peers(resource, hid, peer_list, total) do { \
    if (hid) { \
        char tmp[LOG_STR_LEN] = {0}; \
        log_resource_str(resource, tmp); \
        log_str(tmp, ", hid=%d", hid); \
        log_shm_peers(peer_list, total, tmp); \
        log_ln("%s", tmp); \
    } \
} while (0)
#else
#define log_get_spec_peers(...) do {} while (0)
#endif

#ifdef LOG_SHM_LINE_NUM
#define log_shm_line_num(resource, lines, nr_lines, str) do { \
    if (nr_lines > 0) { \
        int i; \
        vres_line_t *p = lines; \
        if (p) { \
            log_str(str, ", lines=["); \
            for (i = 0; i < nr_lines - 1; i++) \
                log_str(str, "%d, ", p[i].num); \
            log_str(str, "%d]", p[i].num); \
        } \
    } \
} while (0)
#else
#define log_shm_line_num(...) do {} while (0)
#endif

#ifdef LOG_SHM_PAGE_CONTENT
#define log_shm_region_content(resource, region, chunk_id, truncate, fmt, ...) do { \
    vres_slice_t *slice = vres_region_get_slice(resource, region); \
    vres_chunk_t *chunk = &slice->chunks[chunk_id]; \
    log_resource_info(resource, "slice_id=%d (0x%lx), chunk_id=%d (0x%lx)" fmt,  vres_get_slice(resource), (unsigned long)slice, chunk_id, (unsigned long)chunk, ##__VA_ARGS__); \
    log_buf(chunk->buf, PAGE_SIZE, truncate); \
} while (0)
#else
#define log_shm_region_content(...) do {} while (0)
#endif

#ifdef LOG_SHM_GET_PEERS
#define log_shm_get_peers log_resource_info
#else
#define log_shm_get_peers(...) do {} while (0)
#endif

#ifdef LOG_SHM_ADD_HOLDERS
#define log_shm_add_holders log_resource_info
#else
#define log_shm_add_holders(...) do {} while (0)
#endif

#ifdef LOG_SHM_UPDATE_HOLDER
#define log_shm_update_holder log_resource_info
#else
#define log_shm_update_holder(...) do {} while (0)
#endif

#ifdef LOG_SHM_DO_UPDATE_HOLDER
#define log_shm_do_update_holder log_resource_info
#else
#define log_shm_do_update_holder(...) do {} while (0)
#endif

#ifdef LOG_SHM_REQUEST_HOLDERS
#define log_shm_request_holders(resource, region, chunk_id, shm_req) do { \
    char tmp[LOG_STR_LEN] = {0}; \
    vres_slice_t *slice = vres_region_get_slice(resource, region); \
    log_resource_str(resource, tmp); \
    log_shm_slice_ver(resource, region, tmp); \
    log_shm_holders(resource, region, chunk_id, tmp); \
    log_str(tmp, ", owner=%d, chunk_id=%d <idx:%d>", slice->owner, chunk_id, shm_req->index); \
    log_ln("%s", tmp); \
} while (0)
#else
#define log_shm_request_holders(...) do {} while (0)
#endif

#ifdef LOG_SHM_GET_HOLDER_INFO
#define log_shm_get_holder_info(resource, region, chunk_id, shm_req, hid, nr_peers) do { \
    char tmp[LOG_STR_LEN] = {0}; \
    log_resource_str(resource, tmp); \
    log_shm_slice_ver(resource, region, tmp); \
    log_str(tmp, ", chunk_id=%d, hid=%d, nr_peers=%d <idx:%d>", chunk_id, *hid, *nr_peers, shm_req->index); \
    log_ln("%s", tmp); \
} while (0)
#else
#define log_shm_get_holder_info(...) do {} while (0)
#endif

#ifdef LOG_SHM_CHECK_ACTIVE
#define log_shm_check_active log_resource_info
#else
#define log_shm_check_active(...) do {} while (0)
#endif

#ifdef LOG_SHM_CHECK_VERSION
#define log_shm_check_version log_resource_info
#else
#define log_shm_check_version(...) do {} while (0)
#endif

#ifdef LOG_SHM_UNPACK_RSP
#define log_shm_unpack_rsp log_resource_info
#else
#define log_shm_unpack_rsp(...) do {} while (0)
#endif

#ifdef LOG_SHM_CHECK_PRIORITY
#define log_shm_check_priority log_resource_info
#else
#define log_shm_check_priority(...) do {} while (0)
#endif

#ifdef LOG_SHM_SEND_RETRY
#define log_shm_send_retry log_resource_info
#else
#define log_shm_send_retry(...) do {} while (0)
#endif

#ifdef LOG_SHM_CHECK_REPLY
#define log_shm_check_reply log_resource_info
#else
#define log_shm_check_reply(...) do {} while (0)
#endif

#ifdef LOG_SHM_DEACTIVE
#define log_shm_deactive log_resource_info
#else
#define log_shm_deactive(...) do {} while (0)
#endif

#ifdef LOG_SHM_IS_REFL
#define log_shm_is_refl log_resource_info
#else
#define log_shm_is_refl(...) do {} while (0)
#endif

#ifdef LOG_SHM_CLEAR_UPDATES
#define log_shm_clear_updates log_resource_info
#else
#define log_shm_clear_updates(...) do {} while (0)
#endif

#ifdef LOG_SHM_PREEMPT
#define log_shm_preempt log_resource_info
#else
#define log_shm_preempt(...) do {} while (0)
#endif

#ifdef LOG_SHM_RETRY
#define log_shm_retry(resource, slice, shm_req) do { \
    int slice_id = vres_get_slice(resource); \
    int chunk_id = vres_get_chunk(resource); \
    if (vres_get_flags(resource) & VRES_SLICE) \
        log_resource_info(resource, "a slice request, slice_id=%d, chunk_id=%d, slice_index=%d, slice_epoch=%d (req_epoch=%d) <idx:%d>", slice_id, chunk_id, slice->index, slice->epoch, shm_req->epoch, shm_req->index); \
    else \
        log_resource_info(resource, "a chunk request, slice_id=%d, chunk_id=%d, slice_index=%d, slice_epoch=%d (req_epoch=%d) <idx:%d>", slice_id, chunk_id, slice->index, slice->epoch, shm_req->epoch, shm_req->index); \
} while (0)
#else
#define log_shm_retry(...) do {} while (0)
#endif

#ifdef LOG_SHM_UPDATE_HOLDER_LIST
#define log_shm_update_holder_list log_resource_info
#else
#define log_shm_update_holder_list(...) do {} while (0)
#endif

#ifdef LOG_SHM_UPDATE_PEERS
#define log_shm_update_peers log_shm_show_chunk_holders
#else
#define log_shm_update_peers(...) do {} while (0)
#endif

#endif
