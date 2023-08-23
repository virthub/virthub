#ifndef _SHM_H
#define _SHM_H

#include <sys/ipc.h>
#include <sys/shm.h>
#include "resource.h"
#include "rwlock.h"
#include "record.h"
#include "region.h"
#include "event.h"
#include "file.h"
#include "util.h"
#include "lock.h"
#include "redo.h"
#include "prio.h"
#include "wait.h"
#include "line.h"
#include "tsk.h"
#include "mon.h"

// #define VRES_SHM_PREEMPT
#define VRES_SHM_AREA_MAX     2 // determine the number of owners
#define VRES_SHM_ACCESS_THR   4
#define VRES_SHM_ACCESS_MAX   4

#define VRES_SHM_DELAY        5000 // usec
#define VRES_SHM_LIVE_TIME    50000
#define VRES_SHM_PREEMPT_INTV 20000

#define VRES_SHM_RSP_SIZE     (1 << 20)
#define VRES_SHM_RECORD_MAX   1024

#define VRES_SHMMAX           0x2000000                                      /* max shared seg size (bytes) */
#define VRES_SHMMIN           1                                              /* min shared seg size (bytes) */
#define VRES_SHMMNI           4096                                           /* max num of segs system wide */
#define VRES_SHMALL           (VRES_SHMMAX / PAGE_SIZE * (VRES_SHMMNI / 16)) /* max shm system wide (pages) */
#define VRES_SHMSEG           VRES_SHMMNI                                    /* max shared segs per process */

#ifdef SHOW_SHM
#define LOG_SHM_DEBUG
#define LOG_SHM_RETRY
#define LOG_SHM_WAKEUP
#define LOG_SHM_MKWAIT
#define LOG_SHM_CREATE
#define LOG_SHM_IS_REFL
#define LOG_SHM_PREEMPT
#define LOG_SHM_DESTORY
#define LOG_SHM_DELIVER
#define LOG_SHM_COMPLETE
#define LOG_SHM_SAVE_REQ
#define LOG_SHM_GET_ARGS
#define LOG_SHM_GET_PEERS
#define LOG_SHM_CHECK_ARGS
#define LOG_SHM_SEND_RETRY
#define LOG_SHM_CHECK_OWNER
#define LOG_SHM_CHECK_REPLY
#define LOG_SHM_CHECK_HOLDER
#define LOG_SHM_NOTIFY_OWNER
#define LOG_SHM_CHANGE_OWNER
#define LOG_SHM_CHECK_VERSION
#define LOG_SHM_REQUEST_OWNER
#define LOG_SHM_UPDATE_HOLDER
#define LOG_SHM_NOTIFY_HOLDER
#define LOG_SHM_CHECK_PRIORITY
#define LOG_SHM_DO_CHECK_OWNER
#define LOG_SHM_DO_CHECK_HOLDER
#define LOG_SHM_HANDLE_ZEROPAGE
#define LOG_SHM_NOTIFY_PROPOSER
#define LOG_SHM_REQUEST_HOLDERS
#define LOG_SHM_SHOW_CHUNK_HOLDERS

#ifdef SHOW_MORE
#define LOG_SHM_RECORD
#define LOG_SHM_GET_RSP
#define LOG_SHM_DEACTIVE
#define LOG_SHM_LINE_NUM
#define LOG_SHM_PACK_RSP
#define LOG_SHM_SAVE_PAGE
#define LOG_SHM_UNPACK_RSP
#define LOG_SHM_ADD_HOLDERS
#define LOG_SHM_LOAD_UPDATES
#define LOG_SHM_SAVE_UPDATES
#define LOG_SHM_PAGE_CONTENT
#define LOG_SHM_CHECK_ACTIVE
#define LOG_SHM_UPDATE_PEERS
#define LOG_SHM_SHOW_HOLDERS
#define LOG_SHM_CLEAR_UPDATES
#define LOG_SHM_GET_PEER_INFO
#define LOG_SHM_GET_SPEC_PEERS
#define LOG_SHM_GET_HOLDER_INFO
#define LOG_SHM_DO_UPDATE_HOLDER
#define LOG_SHM_UPDATE_HOLDER_LIST
#define LOG_SHM_SEND_OWNER_REQUEST
#endif
#endif

#include "log_shm.h"

#define vres_shm_get_responder(resource) ((resource)->owner)
#define vres_shm_req_convert(req) ((vres_shm_req_t *)(req)->buf)
#define vres_shm_rsp_convert(req) ((vres_shm_rsp_t *)(req)->buf)
#define vres_shmctl_req_convert(req) ((vres_shmctl_req_t *)(req)->buf)
#define vres_shm_req_clone(req_dest, req_src) do { \
    size_t size = sizeof(vres_req_t) + req_src->length; \
    req_dest = (vres_req_t *)malloc(size); \
    memcpy(req_dest, req_src, size); \
} while (0)
#define vres_shm_req_release(req) do { \
    free(req); \
} while (0)

#define vres_shm_is_silent_holder(chunk) (!(chunk)->hid)
#define vres_shm_is_local_request(resource) ((resource)->owner == vres_get_id(resource))

enum vres_shm_cmd {
    VRES_SHM_UNUSED0,
    VRES_SHM_PROPOSE,
    VRES_SHM_CHK_OWNER,
    VRES_SHM_CHK_HOLDER,
    VRES_SHM_NOTIFY_OWNER,
    VRES_SHM_NOTIFY_HOLDER,
    VRES_SHM_NOTIFY_PROPOSER,
    VRES_SHM_RETRY,
    VRES_SHM_NR_COMMANDS,
};

typedef struct {
    int cmd;
#ifdef ENABLE_TTL
    int ttl;
#endif
    int epoch;
    vres_id_t owner;
    vres_index_t index;
    bool chunk_valids[VRES_CHUNK_MAX];
    vres_version_t chunk_ver[VRES_CHUNK_MAX]; // chunk versions
    vres_version_t shadow_ver[VRES_CHUNK_MAX]; // shadow versions
    vres_peers_t peers; // this item must be placed at the end !!!
} vres_shm_req_t;

typedef struct {
    int flags;
    int total;
    int nr_lines;
    int chunk_id;
    vres_line_t lines[0];
} vres_shm_chunk_lines_t;

typedef struct {
    vres_shm_req_t req; // this item must be placed at the begin !!!
    int size;
    vres_id_t responder;
    vres_shm_chunk_lines_t lines[0];
} vres_shm_rsp_t;

typedef struct {
    int nr_holders;
    vres_id_t list[0];
} vres_shm_chunk_info_t;

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
    vres_id_t id;
    vres_index_t index;
    vres_version_t version;
} vres_shm_record_t;

void vres_shm_init();
int vres_shm_call(vres_arg_t *arg);
int vres_shm_create(vres_t *resource);
int vres_shm_destroy(vres_t *resource);
int vres_shm_ipc_init(vres_t *resource);
int vres_shm_check_arg(vres_arg_t *arg);
vres_reply_t *vres_shm_fault(vres_req_t *req, int flags);
vres_reply_t *vres_shm_shmctl(vres_req_t *req, int flags);
int vres_shm_save_page(vres_t *resource, char *buf, size_t size);
int vres_shm_get_arg(vres_t *resource, vres_arg_t *arg, int flags);

#endif
