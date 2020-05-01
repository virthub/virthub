#ifndef _PAGE_H
#define _PAGE_H

#include <vres.h>
#include <fcntl.h>
#include <common.h>
#include <pthread.h>
#include <sys/mman.h>
#include "member.h"
#include "line.h"
#include "file.h"
#include "util.h"

#define VRES_PAGE_MAX             (1 << VRES_PAGE_SHIFT)
#define VRES_PAGE_LINES           (1 << (VRES_LINE_SHIFT - VRES_CHUNK_SHIFT))
#define VRES_PAGE_HODER_MAX       8
#define VRES_PAGE_NR_HITS         4
#define VRES_PAGE_NR_HOLDERS      ((VRES_PAGE_HODER_MAX > VRES_LINE_MAX) ? VRES_LINE_MAX : VRES_PAGE_HODER_MAX)
#define VRES_PAGE_NR_VERSIONS     16
#define VRES_PAGE_NR_CANDIDATES   16
#define VRES_PAGE_CHECK_INTV      5000 // usec
#define VRES_PAGE_DIFF_SIZE       ((VRES_PAGE_NR_VERSIONS * VRES_LINE_MAX + BITS_PER_BYTE - 1) / BITS_PER_BYTE)
#define VRES_PAGE_LOCK_GROUP_SIZE 256
#define VRES_PAGE_LOCK_ENTRY_SIZE 2
#define VRES_PAGE_CHECK_MAX       -1

#ifdef SHOW_PAGE
#define LOG_PAGE_LOCK
#define LOG_PAGE_UNLOCK
#define LOG_PAGE_UPDATE_HOLDER_LIST

#ifdef SHOW_MORE
#define LOG_PAGE_GET
#define LOG_PAGE_PUT
#endif
#endif

#include "log_page.h"

typedef unsigned long vres_page_lock_entry_t;

typedef struct vres_page_lock_desc {
    vres_page_lock_entry_t entry[VRES_PAGE_LOCK_ENTRY_SIZE];
} vres_page_lock_desc_t;

typedef struct vres_page_lock_group {
    pthread_mutex_t mutex;
    rbtree_t tree;
} vres_page_lock_group_t;

typedef struct vres_page_lock {
    vres_page_lock_desc_t desc;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    rbtree_node_t node;
    int count;
} vres_page_lock_t;

typedef struct vres_chunk {
    int hid; // holder id
    bool cand;
    bool wait;
    bool active;
    bool writable;
    int nr_holders;
    vres_clk_t clk;
    vres_id_t owner;
    int nr_candidates;
    vres_index_t index;
#ifdef ENABLE_CHUNK_PREEMPT
    vres_id_t preemptor;
#ifdef ENABLE_TIMED_PREEMPT
    vres_time_t t_preempt;
#endif
#endif
    int nr_silent_holders;
    vres_version_t version;
    char buf[VRES_CHUNK_SIZE];
    vres_digest_t digest[VRES_LINE_MAX];
    vres_id_t holders[VRES_PAGE_NR_HOLDERS];
    bool diff[VRES_PAGE_NR_VERSIONS][VRES_LINE_MAX];
    vres_member_t candidates[VRES_PAGE_NR_CANDIDATES];
} vres_chunk_t;

typedef struct vres_page {
    int count;
    bool cmpl;
    bool ready;
    bool update;
    vres_id_t owner;
    int nr_candidates;
#ifdef ENABLE_PAGE_PREEMPT
    vres_id_t preemptor;
#ifdef ENABLE_TIMED_PREEMPT
    vres_time_t t_preempt;
#endif
#endif
#ifdef ENABLE_PREEMPT_COUNT
    int preempt_count;
#endif
    int flags[VRES_PAGE_MAX];
    bool collect[VRES_LINE_MAX];
    vres_chunk_t chunks[VRES_CHUNK_MAX];
    vres_member_t candidates[VRES_PAGE_NR_CANDIDATES];
} vres_page_t;

static inline void vres_pg_mkready(vres_page_t *page)
{
    page->ready = true;
}

static inline void vres_pg_mkupdate(vres_page_t *page)
{
    page->update = true;
}

static inline void vres_pg_mkcmpl(vres_page_t *page)
{
    page->cmpl = true;
}

static inline void vres_pg_clrready(vres_page_t *page)
{
    page->ready = false;
}

static inline void vres_pg_clrupdate(vres_page_t *page)
{
    page->update = false;
}

static inline void vres_pg_clrcmpl(vres_page_t *page)
{
    page->cmpl = false;
}

static inline bool vres_pg_ready(vres_page_t *page)
{
    return page->ready;
}

static inline bool vres_pg_update(vres_page_t *page)
{
    return page->update;
}

static inline bool vres_pg_cmpl(vres_page_t *page)
{
    return page->cmpl;
}

static inline void vres_pg_clrread(vres_page_t *page, unsigned long off)
{
    assert(off >= 0 && off < VERS_PAGE_MAX);
    page->flags[off] &= ~VRES_RDWR;
    page->flags[off] &= ~VRES_RDONLY;
}

static inline void vres_pg_clrwrite(vres_page_t *page, unsigned long off)
{
    assert(off >= 0 && off < VERS_PAGE_MAX);
    page->flags[off] &= ~VRES_RDWR;
    page->flags[off] |= VRES_RDONLY;
}

static inline void vres_pg_clrpresent(vres_page_t *page, unsigned long off)
{
    assert(off >= 0 && off < VERS_PAGE_MAX);
    page->flags[off] &= ~VRES_PRESENT;
}

static inline bool vres_pg_write(vres_page_t *page, unsigned long off)
{
    assert(off >= 0 && off < VERS_PAGE_MAX);
    return (page->flags[off] & VRES_RDWR) != 0;
}

static inline bool vres_pg_present(vres_page_t *page, unsigned long off)
{
    assert(off >= 0 && off < VERS_PAGE_MAX);
    return (page->flags[off] & VRES_PRESENT) != 0;
}

void vres_page_mkown(vres_t *resource, vres_page_t *page);
void vres_page_mkwait(vres_t *resource, vres_page_t *page);
void vres_page_mkcand(vres_t *resource, vres_page_t *page);
void vres_page_mkread(vres_t *resource, vres_page_t *page);
void vres_page_mkdump(vres_t *resource, vres_page_t *page);
void vres_page_mkwrite(vres_t *resource, vres_page_t *page);
void vres_page_mkactive(vres_t *resource, vres_page_t *page);
void vres_page_mkpresent(vres_t *resource, vres_page_t *page);

void vres_page_clrcand(vres_t *resource, vres_page_t *page);
void vres_page_clrwait(vres_t *resource, vres_page_t *page);
void vres_page_clrdump(vres_t *resource, vres_page_t *page);
void vres_page_clractive(vres_t *resource, vres_page_t *page);
void vres_page_clrwritable(vres_t *resource, vres_page_t *page);

bool vres_page_cand(vres_t *resource, vres_page_t *page);
bool vres_page_wait(vres_t *resource, vres_page_t *page);
bool vres_page_dump(vres_t *resource, vres_page_t *page);
bool vres_page_write(vres_t *resource, vres_page_t *page);
bool vres_page_active(vres_t *resource, vres_page_t *page);
bool vres_page_present(vres_t *resource, vres_page_t *page);
bool vres_page_is_writable(vres_t *resource, vres_page_t *page);

void vres_page_lock_init();
int vres_page_calc_holders(vres_chunk_t *chunk);
void vres_page_put(vres_t *resource, void *entry);
vres_index_t vres_page_alloc_index(vres_chunk_t *chunk);
int vres_page_get_hid(vres_chunk_t *chunk, vres_id_t id);

int vres_page_protect(vres_t *resource, vres_page_t *page);
bool vres_page_chkown(vres_t *resource, vres_page_t *page);
void *vres_page_get(vres_t *resource, vres_page_t **page, int flags);
vres_chunk_t *vres_page_get_chunk(vres_t *resource, vres_page_t *page);
void vres_page_clear_holder_list(vres_t *resource, vres_chunk_t *chunk);
int vres_page_check(vres_t *resource, unsigned long off, int retry, int flags);
int vres_page_get_diff(vres_chunk_t *chunk, vres_version_t version, bool *diff);
bool vres_page_search_holder_list(vres_t *resource, vres_chunk_t *chunk, vres_id_t id);
int vres_page_update(vres_t *resource, vres_page_t *page, unsigned long off, char *buf);
int vres_page_add_holder(vres_t *resource, vres_page_t *page, vres_chunk_t *chunk, vres_id_t id);

#endif
