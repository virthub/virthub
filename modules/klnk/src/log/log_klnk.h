#ifndef _LOG_KLNK_H
#define _LOG_KLNK_H

#include "log.h"

#ifdef LOG_KLNK_RPC_SEND_TO_PEERS
#define log_klnk_rpc_send_to_peers log_resource_ln
#else
#define log_klnk_rpc_send_to_peers(...) do {} while (0)
#endif

#ifdef LOG_KLNK_HANDLER
#define log_klnk_handler log_resource_info
#else
#define log_klnk_handler(...) do {} while (0)
#endif

#ifdef LOG_KLNK_IO_SYNC_SEND
#define log_klnk_io_sync_send(resource) log_resource_info(resource, "start to send")
#else
#define log_klnk_io_sync_send(...) do {} while (0)
#endif

#ifdef LOG_KLNK_IO_SYNC_OUTPUT
#define log_klnk_io_sync_output(resource) log_resource_info(resource, "get output")
#else
#define log_klnk_io_sync_output(...) do {} while (0)
#endif

#ifdef LOG_KLNK_IO_SYNC
#define log_klnk_io_sync(resource, dest, str) do { \
    if (dest != -1) \
        log_resource_info(resource, "dest=%d, %s", dest, str); \
    else \
        log_resource_info(resource, "%s", str); \
} while (0)
#else
#define log_klnk_io_sync(...) do {} while (0)
#endif

#ifdef LOG_KLNK_IO_ASYNC
#define log_klnk_io_async(resource, dest) do { \
    if (dest != -1) \
        log_resource_info(resource, "dest=%d", dest); \
    else \
        log_resource_info(resource, "dest=None"); \
} while (0)
#else
#define log_klnk_io_async(...) do {} while (0)
#endif

#ifdef LOG_KLNK_IO_SYNC_CONNECT
#define log_klnk_io_sync_connect(resource, peer, dest) do { \
    if (dest != -1) \
        log_resource_info(resource, "connect to %s (%d)", addr2str(peer.address), dest); \
    else \
        log_resource_info(resource, "connect to %s", addr2str(peer.address)); \
} while (0)
#else
#define log_klnk_io_sync_connect(...) do {} while (0)
#endif

#ifdef LOG_KLNK_IO_CREATE
#define log_klnk_io_create(resource, dest) do { \
    if (dest != -1) \
        log_resource_info(resource, ">> sent a request to %d <<", dest); \
    else \
        log_resource_info(resource, ">> sent a request <<"); \
} while (0)
#else
#define log_klnk_io_create(...) do {} while (0)
#endif

#ifdef LOG_KLNK_WRITE
#define log_klnk_write log_resource_info
#else
#define log_klnk_write(...) do {} while (0)
#endif

#ifdef LOG_KLNK_BARRIER_SET
#define log_klnk_barrier_set log_resource_ln
#else
#define log_klnk_barrier_set(...) do {} while (0)
#endif

#ifdef LOG_KLNK_BARRIER_CLEAR
#define log_klnk_barrier_clear log_resource_ln
#else
#define log_klnk_barrier_clear(...) do {} while (0)
#endif

#ifdef LOG_KLNK_BARRIER_WAIT
#define log_klnk_barrier_wait log_resource_info
#else
#define log_klnk_barrier_wait(...) do {} while (0)
#endif

#ifdef LOG_KLNK_BARRIER_WAIT_TIMEOUT
#define log_klnk_barrier_wait_timeout log_resource_info
#else
#define log_klnk_barrier_wait_timeout(...) do {} while (0)
#endif

#ifdef LOG_KLNK_RPC_GET
#define log_klnk_rpc_get(resource) log_resource_info(resource, "op=%s", log_get_op(vres_get_op(resource)))
#else
#define log_klnk_rpc_get(...) do {} while (0)
#endif

#ifdef LOG_KLNK_RPC_PUT
#define log_klnk_rpc_put(resource) log_resource_info(resource, "op=%s", log_get_op(vres_get_op(resource)))
#else
#define log_klnk_rpc_put(...) do {} while (0)
#endif

#ifdef LOG_KLNK_RPC_WAIT
#define log_klnk_rpc_wait log_resource_info
#else
#define log_klnk_rpc_wait(...) do {} while (0)
#endif

#endif
