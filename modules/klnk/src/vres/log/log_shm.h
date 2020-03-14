#ifndef _LOG_SHM_H
#define _LOG_SHM_H

#include "log.h"

#define LOG_SHM_NR_BITS  32
#define LOG_SHM_NR_BYTES 16

static inline int _convert2flags(bool ready, bool active, bool wait, bool update, bool cmpl, bool save, bool cand, bool own)
{
    int flags = 0;

    if (ready)
        flags |= 0x00010000;
    if (active)
        flags |= 0x00020000;
    if (wait)
        flags |= 0x00040000;
    if (update)
        flags |= 0x00080000;
    if (cmpl)
        flags |= 0x00100000;
    if (save)
        flags |= 0x00200000;
    if (cand)
        flags |= 0x00400000;
    if (own)
        flags |= 0x00800000;

    return flags;
}

#define _page_flags(resource, page) (page->flags[vres_page_get_off(resource)] | _convert2flags(page->ready, page->active, page->wait, page->update, page->cmpl, page->save, page->cand, page->own))

#define log_shm_page(resource, page, str) \
        log_str(str, ", page={pgno:%ld, hid:%d, cnt:%d, clk:%lld, ver:%lld, hld:%d, own:%d, flg:%s}", \
        vres_get_pgno(resource), page->hid, page->count, page->clk, page->version, page->nr_holders, page->owner, log_get_flags(_page_flags(resource, page))) \

#define log_shm_peers(peers, str) do { \
    int i; \
    if (!peers || !peers->total) \
        break; \
    log_str(str, ", peers=["); \
    for (i = 0; i < peers->total - 1; i++) \
        log_str(str, "%d, ", peers->list[i]); \
    log_str(str, "%d]", peers->list[i]); \
} while (0)

#define log_shm_holders(page, str) do { \
    int i; \
    if (!page->nr_holders) \
        break; \
    log_str(str, ", holders=["); \
    for (i = 0; i < page->nr_holders - 1; i++) \
        log_str(str, "%d, ", page->holders[i]); \
    log_str(str, "%d]", page->holders[i]); \
} while (0)

#define log_shm_req(req, str) do { \
    if (req) \
        log_str(str, ", req={clk:%d, ver:%d} <idx:%d>", (int)(req)->clk, (int)(req)->version, (int)(req)->index); \
} while (0)

#ifdef LOG_SHM_CHECK_OWNER
#define log_shm_check_owner(page, req) do { \
    char tmp[LOG_STR_LEN] = {0}; \
    vres_t *resource = &(req)->resource; \
    vres_shm_req_t *shm_req = (vres_shm_req_t *)(req->buf); \
    log_resource_str(resource, tmp); \
    log_shm_holders(page, tmp); \
    log_shm_page(resource, page, tmp); \
    log_shm_req(shm_req, tmp); \
    log_ln("%s", tmp); \
} while (0)
#else
#define log_shm_check_owner(...) do {} while (0)
#endif

#ifdef LOG_SHM_DO_CHECK_OWNER
#define log_shm_do_check_owner(page, req) do { \
    char tmp[LOG_STR_LEN] = {0}; \
    vres_t *resource = &(req)->resource; \
    vres_shm_req_t *shm_req = (vres_shm_req_t *)(req->buf); \
    log_resource_str(resource, tmp); \
    log_shm_holders(page, tmp); \
    log_shm_page(resource, page, tmp); \
    log_shm_req(shm_req, tmp); \
    log_ln("%s", tmp); \
} while (0)
#else
#define log_shm_do_check_owner(...) do {} while (0)
#endif

#ifdef LOG_SHM_SPEC_REPLY
#define log_shm_spec_reply(resource, hid, nr_peers) log_resource_info(resource, "hid=%d, nr_peers=%d", hid, nr_peers)
#else
#define log_shm_spec_reply(...) do {} while (0)
#endif

#ifdef LOG_SHM_CHECK_HOLDER
#define log_shm_check_holder(page, req) do { \
    char tmp[LOG_STR_LEN] = {0}; \
    vres_t *resource = &(req)->resource; \
    vres_shm_req_t *shm_req = (vres_shm_req_t *)(req->buf); \
    log_resource_str(resource, tmp); \
    log_shm_page(resource, page, tmp); \
    log_shm_req(shm_req, tmp); \
    log_ln("%s", tmp); \
} while (0)
#else
#define log_shm_check_holder(...) do {} while (0)
#endif

#ifdef LOG_SHM_DO_CHECK_HOLDER
#define log_shm_do_check_holder(page, req, lines, nr_lines, total) do { \
    char tmp[LOG_STR_LEN] = {0}; \
    vres_t *resource = &(req)->resource; \
    vres_shm_req_t *shm_req = (vres_shm_req_t *)(req->buf); \
    log_resource_str(resource, tmp); \
    log_shm_page(resource, page, tmp); \
    log_shm_req(shm_req, tmp); \
    log_ln("%s, nr_lines=%d (total=%d)", tmp, nr_lines, total); \
    log_shm_lines(resource, page, lines, nr_lines, total); \
} while (0)
#else
#define log_shm_do_check_holder(...) do {} while (0)
#endif

#ifdef LOG_SHM_NOTIFY_OWNER
#define log_shm_notify_owner(page, req) do { \
    char tmp[LOG_STR_LEN] = {0}; \
    vres_t *resource = &(req)->resource; \
    vres_shm_req_t *shm_req = (vres_shm_req_t *)(req->buf); \
    log_resource_str(resource, tmp); \
    log_shm_holders(page, tmp); \
    log_shm_page(resource, page, tmp); \
    log_shm_req(shm_req, tmp); \
    log_ln("%s", tmp); \
} while (0)
#else
#define log_shm_notify_owner(...) do {} while (0)
#endif

#ifdef LOG_SHM_REPLY
#define log_shm_reply log_resource_ln
#else
#define log_shm_reply(...) do {} while (0)
#endif

#ifdef LOG_SHM_NOTIFY_HOLDER
#define log_shm_notify_holder(page, req) do { \
    char tmp[LOG_STR_LEN] = {0}; \
    vres_t *resource = &(req)->resource; \
    vres_shm_req_t *shm_req = (vres_shm_req_t *)(req->buf); \
    log_resource_str(resource, tmp); \
    log_shm_page(resource, page, tmp); \
    log_shm_req(shm_req, tmp); \
    log_ln("%s", tmp); \
} while (0)
#else
#define log_shm_notify_holder(...) do {} while (0)
#endif

#ifdef LOG_SHM_NOTIFY_PROPOSER
#define log_shm_notify_proposer(page, req, redo) do { \
    char tmp[LOG_STR_LEN] = {0}; \
    vres_t *resource = &(req)->resource; \
    vres_shm_req_t *shm_req = (vres_shm_req_t *)(req->buf); \
    log_resource_str(resource, tmp); \
    log_shm_page(resource, page, tmp); \
    log_shm_req(shm_req, tmp); \
    log_ln("%s (redo=%d)", tmp, redo); \
} while (0)
#else
#define log_shm_notify_proposer(...) do {} while (0)
#endif

#ifdef LOG_SHM_UPDATE_HOLDER
#define log_shm_update_holder(page, req) do { \
    char tmp[LOG_STR_LEN] = {0}; \
    vres_t *resource = &(req)->resource; \
    vres_shm_req_t *shm_req = (vres_shm_req_t *)(req->buf); \
    log_resource_str(resource, tmp); \
    log_shm_holders(page, tmp); \
    log_shm_req(shm_req, tmp); \
    log_ln("%s", tmp); \
} while (0)
#else
#define log_shm_update_holder(...) do {} while (0)
#endif

#ifdef LOG_SHM_HANDLE_ZEROPAGE
#define log_shm_handle_zeropage(resource, page) do { \
    char tmp[LOG_STR_LEN] = {0}; \
    log_resource_str(resource, tmp); \
    log_shm_page(resource, page, tmp); \
    log_ln("%s", tmp); \
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
#define log_shm_save_updates(resource, nr_lines, total) log_resource_info(resource, "nr_lines=%d (total=%d)", nr_lines, total)
#else
#define log_shm_save_updates(...) do {} while (0)
#endif

#ifdef LOG_SHM_GET_ARGS
#define log_shm_get_arg(resource, arg) do { \
    char tmp[LOG_STR_LEN] = {0}; \
    vres_page_t *page; \
    if ((arg)->in == (arg)->buf) \
        break; \
    page = vres_file_get_desc((vres_file_entry_t *)(arg)->entry, vres_page_t); \
    log_resource_str(resource, tmp); \
    log_shm_peers((arg)->peers, tmp); \
    log_shm_page(resource, page, tmp); \
    log_shm_req((vres_shm_req_t *)(arg)->in, tmp); \
    log_ln("%s", tmp); \
} while (0)
#else
#define log_shm_get_arg(...) do {} while (0)
#endif

#ifdef LOG_SHM_SAVE_REQ
#define log_shm_save_req(page, req) do { \
    char tmp[LOG_STR_LEN] = {0}; \
    vres_t *resource = &(req)->resource; \
    vres_shm_req_t *shm_req = (vres_shm_req_t *)(req->buf); \
    log_resource_str(resource, tmp); \
    log_shm_page(resource, page, tmp); \
    log_shm_req(shm_req, tmp); \
    log_ln("%s", tmp); \
} while (0)
#else
#define log_shm_save_req(...) do {} while (0)
#endif

#ifdef LOG_SHM_WAKEUP
#define log_shm_wakeup log_resource
#else
#define log_shm_wakeup(...) do {} while (0)
#endif

#ifdef LOG_SHM_DELIVER
#define log_shm_deliver(page, req) do { \
    vres_t *resource = &(req)->resource; \
    vres_shm_req_t *shm_req = (vres_shm_req_t *)req->buf; \
    log_resource_info(resource, ">-- deliver --< <idx:%d>", shm_req->index); \
    log_shm_lines(resource, page, NULL, 0, 0); \
} while (0)
#else
#define log_shm_deliver(...) do {} while (0)
#endif

#ifdef LOG_SHM_RECORD
#define log_shm_record(resource, path, id, index, version) log_resource_info(resource, "path=%s, record={id:%d, ver:%d} <idx:%d>", path, id, (int)version, index)
#else
#define log_shm_record(...) do {} while (0)
#endif

#ifdef LOG_SHM_EXPIRED_REQ
#define log_shm_expired_req(page, req) do { \
    char tmp[LOG_STR_LEN] = {0}; \
    vres_shm_req_t *shm_req = (vres_shm_req_t *)(req)->buf; \
    log_resource_str(&(req)->resource, tmp); \
    log_str(tmp, "find an expired request, page={clk:%lld, ver:%lld}", (page)->clk, (page)->version); \
    log_shm_req(shm_req, tmp); \
    log_ln("%s", tmp); \
} while (0)
#else
#define log_shm_expired_req(...) do {} while (0)
#endif

#ifdef LOG_SHM_CLOCK_UPDATE
#define log_shm_clock_update(resource, from, to) log_resource_info(resource, "clk=%lld=>%lld", from, to)
#else
#define log_shm_clock_update(...) do {} while (0)
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

#ifdef LOG_SHM_SAVE_PAGE
#define log_shm_save_page log_resource_ln
#else
#define log_shm_save_page(...) do {} while (0)
#endif

#ifdef LOG_SHM_OWNER
#define log_shm_owner log_resource_info
#else
#define log_shm_owner(...) do {} while (0)
#endif

#ifdef LOG_SHM_CHECK_REPLY
#define log_shm_check_reply(resource, total, head, tail, reply) log_resource_info(resource, "total=%d, head=%d, tail=%d, reply=%d", total, *head, *tail, *reply)
#else
#define log_shm_check_reply(...) do {} while (0)
#endif

#ifdef LOG_SHM_GET_PEER_INFO
#define log_shm_get_peer_info(resource, info) log_resource_info(resource, "total=%d", (info)->total)
#else
#define log_shm_get_peer_info(...) do {} while (0)
#endif

#ifdef LOG_SHM_REQUEST_SILENT_HOLERS
#define log_shm_request_silent_holders log_resource_ln
#else
#define log_shm_request_silent_holders(...) do {} while (0)
#endif

#ifdef LOG_SHM_REQUEST_HOLDERS
#define log_shm_request_holders log_resource_ln
#else
#define log_shm_request_holders(...) do {} while (0)
#endif

#ifdef LOG_SHM_CHECK_SPEC_REPLY
#define log_check_spec_reply(resource, hid, peers, cnt) do { \
    if (hid) { \
        int i; \
        char tmp[LOG_STR_LEN] = {0}; \
        log_resource_str(resource, tmp); \
        log_str(tmp, ", hid=%d, peers=[", hid); \
        for (i = 0; i < cnt - 1; i++) \
            log_str(tmp, "%d, ", peers[i]); \
        log_str(tmp, "%d]", peers[i]); \
        log_ln("%s", tmp); \
    } \
} while (0)
#else
#define log_check_spec_reply(...) do {} while (0)
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
#define log_shm_page_content(page) do { \
    int i, j; \
    int cnt = 0; \
    char *p; \
    char tmp[LOG_STR_LEN] = {0}; \
    log_str(tmp, "----- partial page content ----- (%s)\n", __func__); \
    p = tmp + strlen(tmp); \
    for (i = 0; i < LOG_SHM_NR_BYTES; i++) { \
        char ch = page->buf[i]; \
        for (j = 0; j < 8; j++) { \
            if (ch & 1) \
                *p++ = '1'; \
            else \
                *p++ = '0'; \
            ch >>= 1; \
        } \
        cnt += 8; \
        if (cnt >= LOG_SHM_NR_BITS) { \
            *p++ = '\n'; \
            cnt = 0; \
        } \
    } \
    *p = '\0'; \
    log_str(tmp, "--------------------------------"); \
    log_ln("%s", tmp); \
} while (0)
#else
#define log_shm_page_content(...) do {} while (0)
#endif

#ifdef LOG_SHM_LINES
#define log_shm_lines(resource, page, lines, nr_lines, total) do { \
    char tmp[LOG_STR_LEN] = {0}; \
    if (nr_lines > 0) { \
        log_resource_str(resource, tmp); \
        log_str(tmp, ", line_info={new:%d, total:%d}", nr_lines, total); \
        log_shm_line_num(resource, lines, nr_lines, tmp); \
        log_ln("%s", tmp); \
    } \
    log_shm_page_content(page); \
} while (0)
#else
#define log_shm_lines(...) do {} while (0)
#endif

#ifdef LOG_SHM_PAGE_DIFF
#define log_shm_page_diff(diff) do { \
    int i, j; \
    char tmp[LOG_STR_LEN] = {0}; \
    log_str(tmp, "page diff: (%s)\n", __func__); \
    for (i = 0; i < VRES_PAGE_NR_VERSIONS; i++) { \
        for (j = 0; j < VRES_LINE_MAX; j++) \
            log_str(tmp, "%d", diff[i][j]); \
        log_str(tmp, "\n"); \
    } \
    log("%s", tmp); \
} while (0)
#else
#define log_shm_page_diff(...) do {} while (0)
#endif

#endif
