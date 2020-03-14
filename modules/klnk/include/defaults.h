#ifndef _DEFAULTS_H
#define _DEFAULTS_H

#define FUSE_USE_VERSION 29

#ifdef UNSHARE
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sched.h>
#endif

#define ENABLE_PRIORITY
#define ENABLE_DYNAMIC_OWNER
#define ENABLE_SPEC          // speculative mode
// #define ENABLE_PAGE_CHECK    // this setting must be enabled if a stronger consistency is required
// #define ENABLE_TTL
// #define ENABLE_PGSAVE
// #define ENABLE_TSKPUT
// #define ENABLE_BARRIER
// #define ENABLE_TIME_SYNC

#define PAGE_CHECK_MAX -1

#ifdef SHOW_KLNK
#define LOG_KLNK_OPEN
#define LOG_KLNK_HANDLER
#endif

#ifdef SHOW_IO
#define LOG_KLNK_IO_SYNC
#define LOG_KLNK_IO_SYNC_CONNECT

#ifdef SHOW_MORE
#define LOG_KLNK_IO_CREATE
#define LOG_KLNK_IO_SYNC_SEND
#define LOG_KLNK_IO_SYNC_OUTPUT
#endif
#endif

#ifdef SHOW_BARRIER
#define LOG_KLNK_BARRIER_SET
#define LOG_KLNK_BARRIER_WAIT
#define LOG_KLNK_BARRIER_CLEAR
#define LOG_KLNK_BARRIER_WAIT_TIMEOUT
#endif

#ifdef SHOW_RPC
#define LOG_KLNK_RPC_GET
#define LOG_KLNK_RPC_PUT
#define LOG_KLNK_RPC_WAIT
#define LOG_KLNK_RPC_SEND_TO_PEERS
#endif

#include "info.h"
#include "common.h"

#endif
