#ifndef _LOG_REDO_H
#define _LOG_REDO_H

#include "log.h"

#ifdef LOG_REDO_REQ
#define log_redo_req(resource, in, index, ret) do { \
    if (VRES_OP_SHMFAULT == vres_get_op(resource)) { \
        vres_shm_req_t *shm_req = (vres_shm_req_t *)in; \
        if (shm_req && (shm_req->cmd < VRES_SHM_NR_COMMANDS)) { \
            log_resource_info(resource, "redo_index=%d, cmd=%s, ret=%s", index, log_get_shm_cmd(shm_req->cmd), log_get_err(ret)); \
            break; \
        } \
    } \
    log_resource_info(resource, "redo_index=%d, op=%s, ret=%s", index, log_get_op(vres_get_op(resource)), log_get_err(ret)); \
} while (0)
#else
#define log_redo_req(...) do {} while (0)
#endif

#ifdef LOG_REDO
#define log_redo log_resource_info
#else
#define log_redo(...) do {} while (0)
#endif

#ifdef LOG_REDO_ALL
#define log_redo_all log_resource_info
#else
#define log_redo_all(...) do {} while (0)
#endif

#endif
