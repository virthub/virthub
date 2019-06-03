#ifndef _MIG_H
#define _MIG_H

#include "file.h"
#include "path.h"
#include "dump.h"
#include "resource.h"

#define MIG_PATH_FST  "/.^"
#define MIG_PATH_LST  "/.$"
#define MIG_PATH_NXT  "/.+"
#define MIG_PATH_BAC  "/.-"
#define MIG_PATH_RND  "/.?"
#define MIG_PATH_LOC  "/.@"
#define MIG_PATH_ROOT "/"

#ifdef SHOW_MIG
#define LOG_MIG
#define LOG_MIG_SET
#define LOG_MIG_CREATE
#endif

#include "log_mig.h"

typedef struct vres_mig_arg {
    char path[VRES_PATH_MAX];
} vres_mig_arg_t;

typedef struct vres_mig_req {
    vres_id_t id;
    vres_addr_t addr;
} vres_mig_req_t;

int vres_migrate(vres_t *resource, vres_arg_t *arg);

#endif
