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
#define VRES_PAGE_HODER_MAX       8
#define VRES_PAGE_NR_HITS         4
#define VRES_PAGE_NR_HOLDERS      ((VRES_PAGE_HODER_MAX > VRES_LINE_MAX) ? VRES_LINE_MAX : VRES_PAGE_HODER_MAX)
#define VRES_PAGE_NR_VERSIONS     16
#define VRES_PAGE_NR_CANDIDATES   16
#define VRES_PAGE_CHECK_INTV      5000 // usec
#define VRES_PAGE_DIFF_SIZE       ((VRES_PAGE_NR_VERSIONS * VRES_LINE_MAX + BITS_PER_BYTE - 1) / BITS_PER_BYTE)
#define VRES_PAGE_LOCK_GROUP_SIZE 256
#define VRES_PAGE_LOCK_ENTRY_SIZE 2

#ifdef SHOW_PAGE
#define LOG_PAGE_LOCK
#define LOG_PAGE_UNLOCK

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

typedef struct vres_page {
    vres_version_t version;
    vres_digest_t digest[VRES_LINE_MAX];
    vres_index_t index;
    vres_clk_t clk;
    int hid; // the identity of holder
    int flags[VRES_PAGE_MAX];
    int count;
    int nr_holders;
    int nr_candidates;
    int nr_silent_holders;
    bool diff[VRES_PAGE_NR_VERSIONS][VRES_LINE_MAX];
    bool collect[VRES_LINE_MAX];
    char buf[VRES_PAGE_SIZE];
    vres_id_t owner;
    vres_id_t holders[VRES_PAGE_NR_HOLDERS];
    vres_member_t candidates[VRES_PAGE_NR_CANDIDATES];
    bool own;
    bool cand;
    bool wait;
    bool cmpl;
    bool save;
    bool ready;
    bool update;
    bool active;
    bool writable;
} vres_page_t;

static inline void vres_pg_mkwrite(vres_page_t *page, int off)
{
    page->writable = true;
    page->flags[off] &= ~VRES_RDONLY;
    page->flags[off] |= VRES_RDWR;
}

static inline void vres_pg_mkread(vres_page_t *page, int off)
{
    page->flags[off] &= ~VRES_RDWR;
    page->flags[off] |= VRES_RDONLY;
}

static inline void vres_pg_mkpresent(vres_page_t *page, int off)
{
    page->flags[off] |= VRES_PRESENT;
}

static inline void vres_pg_mkready(vres_page_t *page)
{
    page->ready = true;
}

static inline void vres_pg_mkwait(vres_page_t *page)
{
    page->wait = true;
}

static inline void vres_pg_mkown(vres_page_t *page)
{
    page->own = true;
}

static inline void vres_pg_mkcand(vres_page_t *page)
{
    page->cand = true;
}

static inline void vres_pg_mkupdate(vres_page_t *page)
{
    page->update = true;
}

static inline void vres_pg_mkcmpl(vres_page_t *page)
{
    page->cmpl = true;
}

static inline void vres_pg_mksave(vres_page_t *page)
{
    page->save = true;
}

static inline void vres_pg_mkactive(vres_page_t *page)
{
    page->active = true;
}

static inline void vres_pg_clrready(vres_page_t *page)
{
    page->ready = false;
}

static inline void vres_pg_clrwait(vres_page_t *page)
{
    page->wait = false;
}

static inline void vres_pg_clrown(vres_page_t *page)
{
    page->own = false;
}

static inline void vres_pg_clrcand(vres_page_t *page)
{
    page->cand = false;
}

static inline void vres_pg_clrupdate(vres_page_t *page)
{
    page->update = false;
}

static inline void vres_pg_clrcmpl(vres_page_t *page)
{
    page->cmpl = false;
}

static inline void vres_pg_clrsave(vres_page_t *page)
{
    page->save = false;
}

static inline void vres_pg_clractive(vres_page_t *page)
{
    page->active = false;
}

static inline void vres_pg_clrpresent(vres_page_t *page, int off)
{
    page->flags[off] &= ~VRES_PRESENT;
}

static inline void vres_pg_rdprotect(vres_page_t *page, int off)
{
    page->writable = false;
    page->flags[off] &= ~VRES_RDWR;
    page->flags[off] &= ~VRES_RDONLY;
}

static inline void vres_pg_wrprotect(vres_page_t *page, int off)
{
    page->writable = false;
    page->flags[off] &= ~VRES_RDWR;
    page->flags[off] |= VRES_RDONLY;
}

static inline int vres_pg_read(vres_page_t *page, int off)
{
    return page->flags[off] & VRES_RDONLY;
}

static inline int vres_pg_write(vres_page_t *page, int off)
{
    return page->flags[off] & VRES_RDWR;
}

static inline int vres_pg_writable(vres_page_t *page)
{
    return page->writable;
}

static inline int vres_pg_present(vres_page_t *page, int off)
{
    return page->flags[off] & VRES_PRESENT;
}

static inline int vres_pg_wait(vres_page_t *page)
{
    return page->wait;
}

static inline int vres_pg_ready(vres_page_t *page)
{
    return page->ready;
}

static inline int vres_pg_own(vres_page_t *page)
{
    return page->own;
}

static inline int vres_pg_cand(vres_page_t *page)
{
    return page->cand;
}

static inline int vres_pg_update(vres_page_t *page)
{
    return page->update;
}

static inline int vres_pg_cmpl(vres_page_t *page)
{
    return page->cmpl;
}

static inline int vres_pg_save(vres_page_t *page)
{
    return page->save;
}

static inline int vres_pg_active(vres_page_t *page)
{
    return page->active;
}

void vres_page_lock_init();
int vres_page_get_off(vres_t *resource);
int vres_page_calc_holders(vres_page_t *page);
void vres_page_put(vres_t *resource, void *entry);
int vres_page_update(vres_page_t *page, char *buf);
vres_index_t vres_page_req_index(vres_page_t *page);
int vres_page_get_hid(vres_page_t *page, vres_id_t id);
int vres_page_protect(vres_t *resource, vres_page_t *page);
void *vres_page_get(vres_t *resource, vres_page_t **page, int flags);
void vres_page_clear_holder_list(vres_t *resource, vres_page_t *page);
int vres_page_add_holder(vres_t *resource, vres_page_t *page, vres_id_t id);
int vres_page_get_diff(vres_page_t *page, vres_version_t version, bool *diff);
int vres_page_check(vres_t *resource, vres_page_t *page, int retry, int flags);
int vres_page_search_holder_list(vres_t *resource, vres_page_t *page, vres_id_t id);
int vres_page_update_holder_list(vres_t *resource, vres_page_t *page, vres_id_t *holders, int nr_holders);

#endif
