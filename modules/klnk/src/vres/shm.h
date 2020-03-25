#ifndef _SHM_H
#define _SHM_H

#include <sys/ipc.h>
#include <sys/shm.h>
#include "resource.h"
#include "special.h"
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
#include "line.h"
#include "tsk.h"

// #define VRES_SHM_EXTEND_HOLD_TIME

#define VRES_SHM_PEER_MAX     8
#define VRES_SHM_NR_PEERS     (VRES_SHM_PEER_MAX > (VRES_PAGE_NR_HOLDERS - 1) ? (VRES_PAGE_NR_HOLDERS - 1) : VRES_SHM_PEER_MAX)
#define VRES_SHM_NR_AREAS     4
#define VRES_SHM_NR_VISITS    2
#define VRES_SHM_HOLD_INTV    5000 // usec
#define VRES_SHM_PREEMPT_INTV VRES_PRIO_PERIOD

#define VRES_SHMMAX           0x2000000                                         /* max shared seg size (bytes) */
#define VRES_SHMMIN           1                                                 /* min shared seg size (bytes) */
#define VRES_SHMMNI           4096                                              /* max num of segs system wide */
#define VRES_SHMALL           (VRES_SHMMAX / PAGE_SIZE * (VRES_SHMMNI / 16))    /* max shm system wide (pages) */
#define VRES_SHMSEG           VRES_SHMMNI                                       /* max shared segs per process */

#ifdef SHOW_SHM
#define LOG_SHM_OWNER
#define LOG_SHM_DESTORY
#define LOG_SHM_DELIVER
#define LOG_SHM_LINE_NUM
#define LOG_SHM_SAVE_REQ
#define LOG_SHM_GET_ARGS
#define LOG_SHM_GET_PEERS
#define LOG_SHM_SPEC_REPLY
#define LOG_SHM_CHECK_REPLY
#define LOG_SHM_CHECK_OWNER
#define LOG_SHM_EXPIRED_REQ
#define LOG_SHM_CHECK_HOLDER
#define LOG_SHM_NOTIFY_OWNER
#define LOG_SHM_REQUEST_OWNER
#define LOG_SHM_GET_PEER_INFO
#define LOG_SHM_NOTIFY_HOLDER
#define LOG_SHM_DO_CHECK_OWNER
#define LOG_SHM_DO_CHECK_HOLDER
#define LOG_SHM_HANDLE_ZEROPAGE
#define LOG_SHM_NOTIFY_PROPOSER
#define LOG_SHM_REQUEST_HOLDERS
#define LOG_SHM_CHECK_SPEC_REPLY
#define LOG_SHM_REQUEST_SILENT_HOLERS

#ifdef SHOW_MORE
#define LOG_SHM_LINES
#define LOG_SHM_RECORD
#define LOG_SHM_WAKEUP
#define LOG_SHM_SAVE_PAGE
#define LOG_SHM_PAGE_DIFF
#define LOG_SHM_FAST_REPLY
#define LOG_SHM_CHECK_ARGS
#define LOG_SHM_PAGE_CONTENT
#define LOG_SHM_SAVE_UPDATES
#define LOG_SHM_CLOCK_UPDATE
#endif
#endif

#include "log_shm.h"

#define vres_shm_get_record_path vres_get_temp_path
#define vres_shm_is_silent_holder(chunk) (!(chunk)->hid)

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

typedef struct {
    int cmd;
#ifdef ENABLE_TTL
    int ttl;
#endif
    vres_clk_t clk;
    vres_index_t index;
    vres_version_t version;
    vres_id_t owner;
    vres_peers_t peers;
} vres_shm_req_t;

typedef struct {
    vres_shm_req_t req; // Note that this item must be placed at the beginning
    int total;
    int nr_lines;
    vres_line_t lines[0];
} vres_shm_rsp_t;

typedef struct {
    int total;
    vres_id_t list[0];
} vres_shm_info_t;

typedef struct {
    long retval;
    char buf[PAGE_SIZE];
} vres_shm_result_t;

typedef struct {
    int cmd;
} vres_shmctl_req_t;

typedef struct {
    long retval;
} vres_shmctl_result_t;

typedef struct {
    int index;
    vres_id_t id;
    vres_version_t version;
} vres_shm_record_t;

void vres_shm_init();
int vres_shm_call(vres_arg_t *arg);
int vres_shm_create(vres_t *resource);
int vres_shm_destroy(vres_t *resource);
int vres_shm_ipc_init(vres_t *resource);
int vres_shm_check_arg(vres_arg_t *arg);
int vres_shm_get_arg(vres_t *resource, vres_arg_t *arg);
vres_reply_t *vres_shm_fault(vres_req_t *req, int flags);
vres_reply_t *vres_shm_shmctl(vres_req_t *req, int flags);
int vres_shm_save_page(vres_t *resource, char *buf, size_t size);

#endif
