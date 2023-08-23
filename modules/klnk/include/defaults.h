#ifndef _DEFAULTS_H
#define _DEFAULTS_H

#define FUSE_USE_VERSION 35

#ifdef UNSHARE
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sched.h>
#endif

#define ENABLE_MONITOR
#define ENABLE_PRIORITY
#define ENABLE_TIME_SYNC
#define ENABLE_DYNAMIC_OWNER
#define ENABLE_ACCESS_PROTECT

// #define ENABLE_TTL
// #define ENABLE_CKPT
// #define ENABLE_SPEC // speculative mode
// #define ENABLE_PGSAVE
// #define ENABLE_TSKPUT
// #define ENABLE_BARRIER
// #define ENABLE_TIMED_PREEMPT
// #define ENABLE_PREEMPT_COUNT

#define SHOW_IO       // show KLNK I/O
#define SHOW_SHM      // show shared-memory IPC
#define SHOW_SEM      // show semaphore IPC
#define SHOW_MSG      // show message IPC
#define SHOW_TSK      // show task information
#define SHOW_IPC      // show IPC information
#define SHOW_DMGR     // show dmgr information
#define SHOW_KLNK     // show klnk information
#define SHOW_PROC     // show processing information
#define SHOW_PAGE     // show page information
#define SHOW_LOCK     // show lock information
#define SHOW_REDO     // show redo information
#define SHOW_PRIO     // show priority information
#define SHOW_NODE     // show node informations
#define SHOW_EVENT    // show event information
#define SHOW_RECORD   // show record information
#define SHOW_REQUEST  // show request information

// #define SHOW_MON      // show monitor information
// #define SHOW_RPC      // show RPC information
// #define SHOW_MIG      // show resource migration
// #define SHOW_FILE     // show file information
// #define SHOW_DUMP     // show dumping information
// #define SHOW_CKPT     // show checkpointing information
// #define SHOW_RWLOCK   // show rwlock information
// #define SHOW_MEMBER   // show member information
// #define SHOW_RESTORE  // show restoring information
// #define SHOW_BARRIER  // show barrier information
// #define SHOW_METADATA // show metadata information
// #define SHOW_RESOURCE // show resource information

//----------------------------------------------------------//
// #define SHOW_MORE     // show more details
//----------------------------------------------------------//

#ifdef SHOW_KLNK
#define LOG_KLNK_WRITE

#ifdef SHOW_MORE
#define LOG_KLNK_HANDLER
#endif
#endif

#ifdef SHOW_IO
#define LOG_KLNK_IO_SYNC_CONNECT

#ifdef SHOW_MORE
#define LOG_KLNK_IO_SYNC
#define LOG_KLNK_IO_ASYNC
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

#include "common.h"

#endif
