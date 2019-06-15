#ifndef _DEFAULTS_H
#define _DEFAULTS_H

#define FUSE_USE_VERSION 29

#ifdef UNSHARE
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sched.h>
#endif

#define LOG_KLNK_OPEN
#define LOG_KLNK_HANDLER
#define LOG_KLNK_BROADCAST
#define LOG_KLNK_IO_SYNC
#define LOG_KLNK_IO_SYNC_CONNECT
// #define LOG_KLNK_IO_SYNC_SEND
// #define LOG_KLNK_IO_SYNC_OUTPUT

#ifdef LOG_KLNK_BROADCAST
#define log_klnk_broadcast log_resource_ln
#else
#define log_klnk_broadcast(...) do {} while (0)
#endif

#ifdef LOG_KLNK_HANDLER
#define log_klnk_handler log_resource_info
#else
#define log_klnk_handler(...) do {} while (0)
#endif

#ifdef LOG_KLNK_IO_SYNC
#define log_klnk_io_sync log_resource_info
#else
#define log_klnk_io_sync(...) do {} while (0)
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

#ifdef LOG_KLNK_IO_SYNC_CONNECT
#define log_klnk_io_sync_connect(resource, peer) log_resource_info(resource, "connect to %s (id=%d)", vres_addr2str(peer.address), peer.id)
#else
#define log_klnk_io_sync_connect(...) do {} while (0)
#endif

#ifdef LOG_KLNK_OPEN
#define log_klnk_open log_resource_info
#else
#define log_klnk_open(...) do {} while (0)
#endif

#endif
