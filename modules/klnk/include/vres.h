#ifndef _VRES_H
#define _VRES_H

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <defaults.h>

#define AREA_MANAGER     0
#define STATIC_MANAGER   1
#define DYNAMIC_MANAGER  2

#define PAGE_SIZE        4096
#define PAGE_SHIFT       12
#define BITS_PER_BYTE    8
#define NODE_NAME_SIZE   9
#define LEN_LISTEN_QUEUE 16

#define EOK              900
#define ENOOWNER         901
#define ERMID            902

#define VRES_POS_OP      0
#define VRES_POS_ID      1
#define VRES_POS_VAL1    2
#define VRES_POS_VAL2    3
#define VRES_POS_INDEX   3

#define VRES_RDONLY      0x0001
#define VRES_RDWR        0x0002
#define VRES_CREAT       0x0004
#define VRES_CHOWN       0x0008
#define VRES_CAND        0x0010
#define VRES_CANCEL      0x0020
#define VRES_DIFF        0x0040
#define VRES_CRIT        0x0080
#define VRES_REDO        0x0100
#define VRES_PRIO        0x0200

#define VRES_STAT_INIT   1
#define VRES_TTL_MAX     128

enum vres_class {
    VRES_CLS_MSG,
    VRES_CLS_SEM,
    VRES_CLS_SHM,
    VRES_CLS_TSK,
    VRES_NR_CLASSES,
};

enum vres_operation {
    VRES_OP_UNUSED0,
    VRES_OP_MSGSND,
    VRES_OP_MSGRCV,
    VRES_OP_MSGCTL,
    VRES_OP_SEMOP,
    VRES_OP_SEMCTL,
    VRES_OP_SEMEXIT,
    VRES_OP_SHMFAULT,
    VRES_OP_SHMCTL,
    VRES_OP_TSKCTL,
    VRES_OP_UNUSED1,
    VRES_OP_TSKGET,
    VRES_OP_TSKPUT,
    VRES_OP_MSGGET,
    VRES_OP_MSGPUT,
    VRES_OP_SEMGET,
    VRES_OP_SEMPUT,
    VRES_OP_SHMGET,
    VRES_OP_SHMPUT,
    VRES_OP_PGSAVE,
    VRES_OP_MIGRATE,
    VRES_OP_DUMP,
    VRES_OP_RESTORE,
    VRES_OP_RELEASE,
    VRES_OP_PROBE,
    VRES_OP_UNUSED2,
    VRES_OP_JOIN,
    VRES_OP_LEAVE,
    VRES_OP_SYNC,
    VRES_OP_REPLY,
    VRES_OP_CANCEL,
    VRES_OP_UNUSED3,
    VRES_NR_OPERATIONS,
};

#define VRES_LOCAL_START VRES_OP_UNUSED1
#define VRES_LOCAL_END   VRES_OP_UNUSED2
#define VRES_SYNC_START  VRES_OP_UNUSED2
#define VRES_SYNC_END    VRES_OP_UNUSED3

#define VRES_ERRNO_MAX   1000
#define VRES_KEY_MAX     0x7fffffff
#define VRES_ID_MAX      0x7fffffff
#define VRES_IO_MAX      ((1 << 14) - 1)
#define VRES_BUF_MAX     ((1 << 16) - 1)
#define VRES_INDEX_MAX   ((1 << 30) - 1)
#define VRES_MANAGER_MAX 16
#define VRES_REPLY_MAX   8192

typedef key_t vres_key_t;
typedef int32_t vres_id_t;
typedef int32_t vres_val_t;
typedef int32_t vres_index_t;
typedef int32_t vres_entry_t;
typedef int64_t vres_time_t;
typedef uint32_t vres_op_t;
typedef uint32_t vres_flg_t;
typedef uint32_t vres_cls_t;
typedef uint64_t vres_clk_t;
typedef uint64_t vres_version_t;
typedef struct in_addr vres_addr_t;

typedef struct vresource {
    vres_cls_t cls;
    vres_key_t key;
    vres_entry_t entry[4];
    vres_id_t owner;
} vres_t;

typedef struct vres_desc {
    vres_id_t id;
    vres_addr_t address;
} vres_desc_t;

typedef struct vres_peers {
    int total;
    vres_id_t list[0];
} vres_peers_t;

typedef struct vres_member {
    vres_id_t id;
    int count;
} vres_member_t;

struct vres_arg;

typedef int(*vres_call_t)(struct vres_arg *);

typedef struct vres_arg {
    // int desc;
    char *in;
    char *out;
    char *buf;
    void *entry;
    size_t size;
    size_t inlen;
    size_t outlen;
    vres_id_t dest;
    vres_t resource;
    vres_call_t call;
    vres_index_t index;
    vres_peers_t *peers;
    struct timespec *timeout;
} vres_arg_t;

typedef struct vres_req {
    vres_t resource;
    int length;
    char buf[0];
} vres_req_t;

typedef struct vres_reply {
    int length;
    char buf[0];
} vres_reply_t;

extern char log_name[];
extern char node_name[];
extern char master_addr[];
extern char master_name[];
extern int vres_nr_managers;
extern vres_addr_t node_addr;
extern vres_addr_t *vres_managers;

void vres_init();
int vres_dump(vres_t *resource, int flags);
int vres_restore(vres_t *resource, int flags);
int vres_lookup(vres_t *resource, vres_desc_t *desc);

#endif
