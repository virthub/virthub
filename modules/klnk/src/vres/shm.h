#ifndef _SHM_H
#define _SHM_H

#include <sys/ipc.h>
#include <sys/shm.h>
#include "resource.h"
#include "rwlock.h"
#include "record.h"
#include "event.h"
#include "file.h"
#include "util.h"
#include "lock.h"
#include "page.h"
#include "redo.h"
#include "prio.h"
#include "wait.h"
#include "sync.h"
#include "line.h"
#include "tsk.h"

#define VRES_SHM_CHECK_INTERVAL      2000     // usec
#define VRES_SHM_NR_PROBABLE_HOLDERS (VRES_PAGE_NR_HOLDERS - 1)

#define VRES_SHMMAX                  0x2000000                                         /* max shared seg size (bytes) */
#define VRES_SHMMIN                  1                                                 /* min shared seg size (bytes) */
#define VRES_SHMMNI                  4096                                              /* max num of segs system wide */
#define VRES_SHMALL                  (VRES_SHMMAX / PAGE_SIZE * (VRES_SHMMNI / 16))    /* max shm system wide (pages) */
#define VRES_SHMSEG                  VRES_SHMMNI                                       /* max shared segs per process */

#define VRES_SHM_NR_AREAS            3
#define VRES_SHM_NR_VISITS           1

#ifdef SHOW_SHM
#define LOG_SHM_OWNER
#define LOG_SHM_DESTORY
#define LOG_SHM_DELIVER
#define LOG_SHM_SAVE_REQ
#define LOG_SHM_GET_ARGS
#define LOG_SHM_SAVE_PAGE
#define LOG_SHM_FAST_REPLY
#define LOG_SHM_CHECK_REPLY
#define LOG_SHM_CHECK_OWNER
#define LOG_SHM_EXPIRED_REQ
#define LOG_SHM_CHECK_HOLDER
#define LOG_SHM_NOTIFY_OWNER
#define LOG_SHM_REQUEST_OWNER
#define LOG_SHM_GET_PEER_INFO
#define LOG_SHM_NOTIFY_HOLDER
#define LOG_SHM_HANDLE_ZEROPAGE
#define LOG_SHM_NOTIFY_PROPOSER
#define LOG_SHM_REQUEST_HOLDERS
#define LOG_SHM_CHECK_ACTIVE_OWNER
#define LOG_SHM_CHECK_ACTIVE_HOLDER
#define LOG_SHM_REQUEST_SILENT_HOLERS

#ifdef SHOW_MORE
#define LOG_SHM_CACHE
#define LOG_SHM_WAKEUP
#define LOG_SHM_BYPASS
#define LOG_SHM_CHECK_ARGS
#define LOG_SHM_SAVE_UPDATES
#define LOG_SHM_CLOCK_UPDATE
#define LOG_SHM_CHECK_FAST_REPLY
#endif
#endif

#include "log_shm.h"

#define vres_shm_is_silent_holder(page) (!(page)->hid)

enum vres_shm_cmd {
    VRES_SHM_UNUSED0,
    VRES_SHM_PROPOSE,
    VRES_SHM_CHK_OWNER,
    VRES_SHM_CHK_HOLDER,
    VRES_SHM_NOTIFY_OWNER,
    VRES_SHM_NOTIFY_HOLDER,
    VRES_SHM_NOTIFY_PROPOSER,
    VRES_SHM_NR_COMMANDS,
};

typedef struct vres_shmfault_arg {
    int cmd;
#ifdef ENABLE_TTL
    int ttl;
#endif
#ifdef ENABLE_LIVE_TIME
    int prio;           // priority
    vres_time_t live;   // live time
#endif
    vres_clk_t clk;
    vres_index_t index;
    vres_version_t version;
    vres_id_t owner;
    vres_peers_t peers;
} vres_shmfault_arg_t;

typedef struct vres_shmfault_result {
    long retval;
    char buf[PAGE_SIZE];
} vres_shmfault_result_t;

typedef struct vres_shm_peer_info {
    int total;
    vres_id_t list[0];
} vres_shm_peer_info_t;

typedef struct vres_shm_notify_proposer_arg {
    vres_shmfault_arg_t arg;
    int total;
    int nr_lines;
    vres_line_t lines[0];
} vres_shm_notify_proposer_arg_t;

typedef struct vres_shmctl_arg {
    int cmd;
} vres_shmctl_arg_t;

typedef struct vres_shmctl_result {
    long retval;
} vres_shmctl_result_t;

typedef struct vres_shm_cache {
    int index;
    vres_id_t id;
    vres_version_t version;
} vres_shm_cache_t;

int vres_shm_call(vres_arg_t *arg);
int vres_shm_init(vres_t *resource);
int vres_shm_create(vres_t *resource);
int vres_shm_destroy(vres_t *resource);
void vres_shm_put_arg(vres_arg_t *arg);
int vres_shm_check_arg(vres_arg_t *arg);
int vres_shm_get_arg(vres_t *resource, vres_arg_t *arg);
vres_reply_t *vres_shm_fault(vres_req_t *req, int flags);
vres_reply_t *vres_shm_shmctl(vres_req_t *req, int flags);
int vres_shm_save_page(vres_t *resource, char *buf, size_t size);

#endif
