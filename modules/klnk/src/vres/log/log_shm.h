#ifndef _LOG_SHM_H
#define _LOG_SHM_H

#include "log.h"

#define log_shm_req_arg(resource, in) do { \
    vres_shmfault_arg_t *arg = (vres_shmfault_arg_t *)in; \
    if (arg) { \
        log(", arg={clk:%d, ", (int)arg->clk); \
        log("ver:%d, ", (int)arg->version); \
        log("%s}, ", log_get_shmop(arg->cmd)); \
        log("idx=%d", arg->index); \
    } \
    log("\n"); \
} while (0)

#define log_shm_dest(dest) do { \
    if (dest) \
        log("dest=%d", (dest)->id); \
    else \
        log("dest=NULL"); \
} while (0)

#define log_shm_req(resource, in, dest) do { \
    log_resource(resource); \
    log_op(resource); \
    log_shm_dest(dest); \
    log_shm_req_arg(resource, in); \
} while (0)

#define log_shm_page(page) \
        log(", page={hid:%d, cnt:%d, clk:%lld, ver:%lld, hld:%d, own:%d, flg:%x}", \
        page->hid, page->count, page->clk, page->version, page->nr_holders, page->owner, page->flags) \

#define log_shm_peers(peers) do {\
    int i; \
    if (!peers || !peers->total) \
        break; \
    log(", peers=["); \
    for (i = 0; i < peers->total - 1; i++) \
        log("%d, ", peers->list[i]); \
    log("%d]", peers->list[i]); \
} while (0)

#define log_shm_holders(page) do { \
    int i; \
    if (!page->nr_holders) \
        break; \
    log(", holders=["); \
    for (i = 0; i < page->nr_holders - 1; i++) \
        log("%d, ", page->holders[i]); \
    log("%d]", page->holders[i]); \
} while (0)

#define log_shm_arg(arg) do {\
    if (NULL == arg) { \
        log("\n"); \
        break; \
    } \
    log(", arg={clk:%d, ver:%d}, idx=%d\n", (int)(arg)->clk, (int)(arg)->version, (int)(arg)->index); \
} while (0)

#define log_shm_silent_holders(resource, page) do { \
    int nr_silent_holders = page->nr_silent_holders; \
    log_owner(resource); \
    log("silent_holders=["); \
    if (nr_silent_holders > 0) { \
        char path[VRES_PATH_MAX]; \
        vres_file_entry_t *entry; \
        vres_get_holder_path(resource, path); \
        entry = vres_file_get_entry(path, nr_silent_holders * sizeof(vres_id_t), FILE_RDONLY); \
        if (entry) { \
            int i; \
            vres_id_t *holders = vres_file_get_desc(entry, vres_id_t); \
            for (i = 0; i < nr_silent_holders - 1; i++) \
                log("%d, ", holders[i]); \
            log_ln("%d]", holders[i]); \
            break; \
        } \
    } \
    log_ln("empty]"); \
} while (0)

#define log_shm_index(index) log(", idx=%d\n", index)

#ifdef LOG_SHM_CHECK_OWNER
#define log_shm_check_owner(page, req) do { \
    vres_t *resource = &(req)->resource; \
    vres_shmfault_arg_t *arg = (vres_shmfault_arg_t *)(req->buf); \
    log_resource(resource); \
    log_shm_holders(page); \
    log_shm_page(page); \
    log_shm_arg(arg); \
} while (0)
#else
#define log_shm_check_owner(...) do {} while (0)
#endif

#ifdef LOG_SHM_FAST_REPLY
#define log_shm_fast_reply(resource, hid, nr_peers) do { \
    log_resource(resource); \
    log_ln(", hid=%d, nr_peers=%d", hid, nr_peers); \
} while (0)
#else
#define log_shm_fast_reply(...) do {} while (0)
#endif

#ifdef LOG_SHM_CHECK_HOLDER
#define log_shm_check_holder(page, req) do { \
    vres_t *resource = &(req)->resource; \
    vres_shmfault_arg_t *arg = (vres_shmfault_arg_t *)(req->buf); \
    log_resource(resource); \
    log_shm_page(page); \
    log_shm_arg(arg); \
} while (0)
#else
#define log_shm_check_holder(...) do {} while (0)
#endif

#ifdef LOG_SHM_CHECK_FAST_REPLY
#define log_shm_check_fast_reply(req, hid, nr_peers) do { \
    vres_t *resource = &(req)->resource; \
    vres_shmfault_arg_t *arg = (vres_shmfault_arg_t *)(req->buf); \
    log_resource(resource); \
    log(", hid=%d, nr_peers=%d", hid, nr_peers); \
    log_shm_arg(arg); \
} while (0)
#else
#define log_shm_check_fast_reply(...) do {} while (0)
#endif

#ifdef LOG_SHM_NOTIFY_OWNER
#define log_shm_notify_owner(page, req) do { \
    vres_t *resource = &(req)->resource; \
    vres_shmfault_arg_t *arg = (vres_shmfault_arg_t *)(req->buf); \
    log_resource(resource); \
    log_shm_holders(page); \
    log_shm_page(page); \
    log_shm_arg(arg); \
} while (0)
#else
#define log_shm_notify_owner(...) do {} while (0)
#endif

#ifdef LOG_SHM_PASSTHROUGH
#define log_shm_passthrough log_resource_ln
#else
#define log_shm_passthrough(...) do {} while (0)
#endif

#ifdef LOG_SHM_NOTIFY_HOLDER
#define log_shm_notify_holder(page, req) do { \
    vres_t *resource = &(req)->resource; \
    vres_shmfault_arg_t *arg = (vres_shmfault_arg_t *)(req->buf); \
    log_resource(resource); \
    log_shm_page(page); \
    log_shm_arg(arg); \
} while (0)
#else
#define log_shm_notify_holder(...) do {} while (0)
#endif

#ifdef LOG_SHM_NOTIFY_PROPOSER
#define log_shm_notify_proposer(page, req) do { \
    vres_t *resource = &(req)->resource; \
    vres_shmfault_arg_t *arg = (vres_shmfault_arg_t *)(req->buf); \
    log_resource(resource); \
    log_shm_page(page); \
    log_shm_arg(arg); \
} while (0)
#else
#define log_shm_notify_proposer(...) do {} while (0)
#endif

#ifdef LOG_SHM_UPDATE_HOLDER
#define log_shm_update_holder(page, req) do { \
    vres_t *resource = &(req)->resource; \
    vres_shmfault_arg_t *arg = (vres_shmfault_arg_t *)(req->buf); \
    log_owner(resource); \
    log_shm_holders(page); \
    log_shm_arg(arg); \
} while (0)
#else
#define log_shm_update_holder(...) do {} while (0)
#endif

#ifdef LOG_SHM_HANDLE_ZEROPAGE
#define log_shm_handle_zeropage(resource, page) do { \
    log_resource(resource); \
    log_shm_page(page); \
    log("\n"); \
} while (0)
#else
#define log_shm_handle_zeropage(...) do {} while (0)
#endif

#ifdef LOG_SHM_CHECK_ARGS
#define log_shm_check_arg log_resource_ln
#else
#define log_shm_check_arg(...) do {} while (0)
#endif

#ifdef LOG_SHM_SAVE_UPDATES
#define log_shm_save_updates(resource, nr_lines) do { \
    log_resource(resource); \
    log_ln(", updates=%d", nr_lines); \
} while (0)
#else
#define log_shm_save_updates(...) do {} while (0)
#endif

#ifdef LOG_SHM_GET_ARGS
#define log_shm_get_arg(resource, arg) do { \
    vres_page_t *page; \
    if ((arg)->in == (arg)->buf) \
        break; \
    page = vres_file_get_desc((vres_file_entry_t *)(arg)->entry, vres_page_t); \
    log_resource(resource); \
    log_shm_peers((arg)->peers); \
    log_shm_page(page); \
    log_shm_arg((vres_shmfault_arg_t *)(arg)->in); \
} while (0)
#else
#define log_shm_get_arg(...) do {} while (0)
#endif

#ifdef LOG_SHM_SAVE_REQ
#define log_shm_save_req(req) do { \
    vres_t *resource = &(req)->resource; \
    vres_shmfault_arg_t *arg = (vres_shmfault_arg_t *)(req->buf); \
    log_resource(resource); \
    log_shm_arg(arg); \
} while (0)
#else
#define log_shm_save_req(...) do {} while (0)
#endif

#ifdef LOG_SHM_SELECT
#define log_shm_select(resource) do { \
    log_resource(resource); \
    log_ln(", op=%s", vres_op2str(vres_get_op(resource))); \
} while (0)
#else
#define log_shm_select(...) do {} while (0)
#endif

#ifdef LOG_SHM_WAKEUP
#define log_shm_wakeup log_resource
#else
#define log_shm_wakeup(...) do {} while (0)
#endif

#ifdef LOG_SHM_DELIVER_EVENT
#define log_shm_deliver_event(req)    do { \
    vres_shmfault_arg_t *arg = (vres_shmfault_arg_t *)req->buf; \
    log_owner(&req->resource); \
    log("++ new event ++"); \
    log_shm_index(arg->index); \
} while (0)
#else
#define log_shm_deliver_event(...) do {} while (0)
#endif

#ifdef LOG_SHM_CACHE
#define log_shm_cache(resource, id, index, version) do { \
    log_resource(resource); \
    log(", cache={id:%d, ver:%d}", id, (int)version);\
    log_shm_index(index); \
} while (0)
#else
#define log_shm_cache(...) do {} while (0)
#endif

#ifdef LOG_SHM_EXPIRED_REQ
#define log_shm_expired_req(page, req) do { \
    vres_shmfault_arg_t *arg = (vres_shmfault_arg_t *)(req)->buf; \
    log_owner(&(req)->resource); \
    log("find an expired request, "); \
    log("page={clk:%lld, ver:%lld}", (page)->clk, (page)->version); \
    log_shm_arg(arg); \
} while (0)
#else
#define log_shm_expired_req(...) do {} while (0)
#endif

#ifdef LOG_SHM_CLOCK_UPDATE
#define log_shm_clock_update(resource, from, to) do { \
    log_owner(resource); \
    log_ln("clk=<%lld=>%lld>", from, to); \
} while (0)
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
    log_resource(resource); \
    if (dest) \
        log("dest=%d, cmd=<%s=>%s>", *dest, log_get_shmop(VRES_SHM_NOTIFY_OWNER), log_get_shmop(cmd)); \
    else \
        log("dest=NULL, cmd=<%s=>%s>", log_get_shmop(VRES_SHM_NOTIFY_OWNER), log_get_shmop(cmd)); \
    log_op_ln(resource); \
} while (0)
#else
#define log_shm_request_owner(...) do {} while (0)
#endif

#ifdef LOG_SHM_SAVE_PAGE
#define log_shm_save_page log_resource_ln
#else
#define log_shm_save_page(...) do {} while (0)
#endif
#endif
