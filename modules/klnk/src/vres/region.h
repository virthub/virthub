#ifndef _REGION_H
#define _REGION_H

#include <vres.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include "member.h"
#include "line.h"
#include "file.h"
#include "util.h"

// #define VRES_REGION_GRP_LOCK
#define VRES_REGION_CHECK_PRESENT
#define VRES_REGION_SLICE_ACCESS_PROTECT

#define VRES_REGION_CHECK_MAX       16 // if VRES_REGION_CHECK_MAX is set to -1, the times of performing page check are not limited
#define VRES_REGION_HOLDER_MAX      32
#define VRES_REGION_RETRY_MAX       64
#define VRES_REGION_OFF_MAX         (1 << VRES_REGION_SHIFT) // the number of conventional pages in a virtial region

#define VRES_REGION_NR_CANDIDATES   16
#define VRES_REGION_NR_PROVIDERS    16
#define VRES_REGION_NR_VERSIONS     16

#define VRES_REGION_DIFF_SIZE       ((VRES_REGION_NR_VERSIONS * VRES_LINE_MAX + BYTE_WIDTH - 1) / BYTE_WIDTH)
#define VRES_REGION_LOCK_GROUP_SIZE 1024
#define VRES_REGION_LOCK_ENTRY_SIZE 2

#define VRES_REGION_CHECK_INTV      10000 // usec
#define VRES_REGION_ACCESS_TIME     10000 // usec

#ifdef SHOW_REGION
#define LOG_REGION_WRPROTECT
#define LOG_REGION_RDPROTECT
#define LOG_REGION_ACCESS_PROTECT

#ifdef SHOW_MORE
#define LOG_REGION_GET
#define LOG_REGION_PUT
#define LOG_REGION_LINE
#define LOG_REGION_DIFF
#define LOG_REGION_LOCK
#define LOG_REGION_UNLOCK
#endif
#endif

#include "log_region.h"

typedef unsigned long vres_region_lock_entry_t;

typedef struct vres_region_lock_desc {
    vres_region_lock_entry_t entry[VRES_REGION_LOCK_ENTRY_SIZE];
} vres_region_lock_desc_t;

typedef struct vres_region_lock_group {
    rbtree_t tree;
    pthread_mutex_t mutex;
} vres_region_lock_group_t;

typedef struct vres_region_lock {
    int count;
    rbtree_node_t node;
    pthread_cond_t cond;
    pthread_mutex_t mutex;
    vres_region_lock_desc_t desc;
} vres_region_lock_t;

typedef struct vres_chunk_stat {
    bool rw;
    bool diff;
    bool wait;
    bool cmpl;
    bool dump;
    bool chown;
    bool ready;
    bool active;
    bool update;
    bool wakeup;
    bool protect;
} vres_chunk_stat_t;

typedef struct vres_chunk {
    int hid;
    int redo;
    int count;
    int nr_holders;
    int retry_times;
    vres_index_t index;
    vres_version_t version;
    vres_chunk_stat_t stat;
    char buf[VRES_CHUNK_SIZE];
    bool collect[VRES_LINE_MAX];
    vres_version_t shadow_version;
    vres_digest_t digest[VRES_LINE_MAX];
    vres_id_t holders[VRES_REGION_HOLDER_MAX];
    bool diff[VRES_REGION_NR_VERSIONS][VRES_LINE_MAX];
} vres_chunk_t;

typedef struct vres_cand {
    int nr_candidates;
    vres_member_t candidates[VRES_REGION_NR_CANDIDATES];
} vres_cand_t;

typedef struct vres_slice {
    int last;
    int epoch;
    vres_id_t owner;
    vres_cand_t cand;
    vres_index_t index;
    vres_time_t t_access;
    vres_chunk_t chunks[VRES_CHUNK_MAX];
} vres_slice_t;

typedef struct vres_region {
#ifdef ENABLE_PREEMPT_COUNT
    int preempt_count;
#endif
    int last;
    void *entry;
    vres_time_t t_access;
    int flags[VRES_REGION_OFF_MAX];
    vres_slice_t slices[VRES_SLICE_MAX];
} vres_region_t;

void vres_region_mkrd(vres_region_t *region, unsigned long off);
void vres_region_mkwr(vres_region_t *region, unsigned long off);
void vres_region_mkdump(vres_region_t *region, unsigned long off);
void vres_region_mkpresent(vres_region_t *region, unsigned long off);

void vres_region_clrrd(vres_region_t *region, unsigned long off);
void vres_region_clrwr(vres_region_t *region, unsigned long off);
void vres_region_clrdump(vres_region_t *region, unsigned long off);
void vres_region_clrpresent(vres_region_t *region, unsigned long off);

bool vres_region_wr(vres_t *resource, vres_region_t *region);
bool vres_region_present(vres_t *resource, vres_region_t *region);

void vres_region_lock_init();
void vres_region_put(vres_t *resource, vres_region_t *region);
vres_index_t vres_region_alloc_index(vres_slice_t *slice);

vres_region_t *vres_region_get(vres_t *resource, int flags);
vres_slice_t *vres_region_get_slice(vres_t *resource, vres_region_t *region);
vres_chunk_t *vres_region_get_chunk(vres_t *resource, vres_region_t *region);
int vres_region_protect(vres_t *resource, vres_region_t *region, int chunk_id);

#endif
