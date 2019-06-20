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

#define VRES_PAGE_RDONLY          VRES_RDONLY
#define VRES_PAGE_RDWR            VRES_RDWR
#define VRES_PAGE_CREAT           VRES_CREAT
#define VRES_PAGE_OWN             VRES_CHOWN
#define VRES_PAGE_CAND            VRES_CAND
#define VRES_PAGE_REDO            VRES_REDO

#define VRES_PAGE_READY           0x00010000
#define VRES_PAGE_ACTIVE          0x00020000
#define VRES_PAGE_WAIT            0x00040000
#define VRES_PAGE_UPDATE          0x00080000
#define VRES_PAGE_EXCL            0x00100000
#define VRES_PAGE_CMPL            0x00200000
#define VRES_PAGE_SAVE            0x00400000

#define VRES_PAGE_DIFF_SIZE       ((VRES_PAGE_NR_VERSIONS * VRES_LINE_MAX + BITS_PER_BYTE - 1) / BITS_PER_BYTE)
#define VRES_PAGE_LOCK_GROUP_SIZE 256
#define VRES_PAGE_LOCK_ENTRY_SIZE 2

#define VRES_PAGE_NR_VERSIONS     16
#define VRES_PAGE_NR_CANDIDATES   16
#define VRES_PAGE_NR_HOLDERS      8
#define VRES_PAGE_NR_HITS         4

#define VRES_PAGE_CHECK_INTERVAL  1000 // usec

#ifdef SHOW_PAGE
#define LOG_PAGE_LOCK
#define LOG_PAGE_UNLOCK

#ifdef SHOW_MORE
#define LOG_PAGE_GET
#define LOG_PAGE_PUT
#define LOG_PAGE_DIFF
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
    int hid; // Holder ID
    int flags;
    int count;
    int nr_holders;
    int nr_candidates;
    int nr_silent_holders;
    int diff[VRES_PAGE_NR_VERSIONS][VRES_LINE_MAX];
    int collect[VRES_LINE_MAX];
    char buf[PAGE_SIZE];
    vres_id_t owner;
    vres_id_t holders[VRES_PAGE_NR_HOLDERS];
    vres_member_t candidates[VRES_PAGE_NR_CANDIDATES];
} vres_page_t;

static inline void vres_pg_mkready(vres_page_t *page)
{
    page->flags |= VRES_PAGE_READY;
}

static inline void vres_pg_mkactive(vres_page_t *page)
{
    page->flags |= VRES_PAGE_ACTIVE;
}

static inline void vres_pg_mkwait(vres_page_t *page)
{
    page->flags |= VRES_PAGE_WAIT;
}

static inline void vres_pg_mkwrite(vres_page_t *page)
{
    page->flags &= ~VRES_PAGE_RDONLY;
    page->flags |= VRES_PAGE_RDWR;
}

static inline void vres_pg_mkread(vres_page_t *page)
{
    page->flags &= ~VRES_PAGE_RDWR;
    page->flags |= VRES_PAGE_RDONLY;
}

static inline void vres_pg_mkredo(vres_page_t *page)
{
    page->flags |= VRES_PAGE_REDO;
}

static inline void vres_pg_mkown(vres_page_t *page)
{
    page->flags |= VRES_PAGE_OWN;
}

static inline void vres_pg_mkcand(vres_page_t *page)
{
    page->flags |= VRES_PAGE_CAND;
}

static inline void vres_pg_mkupdate(vres_page_t *page)
{
    page->flags |= VRES_PAGE_UPDATE;
}

static inline void vres_pg_mkcmpl(vres_page_t *page)
{
    page->flags |= VRES_PAGE_CMPL;
}

static inline void vres_pg_mkpgsave(vres_page_t *page)
{
    page->flags |= VRES_PAGE_SAVE;
}

static inline void vres_pg_clrready(vres_page_t *page)
{
    page->flags &= ~VRES_PAGE_READY;
}

static inline void vres_pg_clractive(vres_page_t *page)
{
    page->flags &= ~VRES_PAGE_ACTIVE;
}

static inline void vres_pg_clrwait(vres_page_t *page)
{
    page->flags &= ~VRES_PAGE_WAIT;
}

static inline void vres_pg_clrown(vres_page_t *page)
{
    page->flags &= ~VRES_PAGE_OWN;
}

static inline void vres_pg_clrcand(vres_page_t *page)
{
    page->flags &= ~VRES_PAGE_CAND;
}

static inline void vres_pg_clrredo(vres_page_t *page)
{
    page->flags &= ~VRES_PAGE_REDO;
}

static inline void vres_pg_clrupdate(vres_page_t *page)
{
    page->flags &= ~VRES_PAGE_UPDATE;
}

static inline void vres_pg_clrcmpl(vres_page_t *page)
{
    page->flags &= ~VRES_PAGE_CMPL;
}

static inline void vres_pg_clrpgsave(vres_page_t *page)
{
    page->flags &= ~VRES_PAGE_SAVE;
}

static inline void vres_pg_rdprotect(vres_page_t *page)
{
    page->flags &= ~VRES_PAGE_RDWR;
    page->flags &= ~VRES_PAGE_RDONLY;
}

static inline void vres_pg_wrprotect(vres_page_t *page)
{
    page->flags &= ~VRES_PAGE_RDWR;
    page->flags |= VRES_PAGE_RDONLY;
}

static inline int vres_pg_read(vres_page_t *page)
{
    return page->flags & VRES_PAGE_RDONLY;
}

static inline int vres_pg_write(vres_page_t *page)
{
    return page->flags & VRES_PAGE_RDWR;
}

static inline int vres_pg_access(vres_page_t *page)
{
    return vres_pg_read(page) || vres_pg_write(page);
}

static inline int vres_pg_redo(vres_page_t *page)
{
    return page->flags & VRES_PAGE_REDO;
}

static inline int vres_pg_wait(vres_page_t *page)
{
    return page->flags & VRES_PAGE_WAIT;
}

static inline int vres_pg_active(vres_page_t *page)
{
    return page->flags & VRES_PAGE_ACTIVE;
}

static inline int vres_pg_ready(vres_page_t *page)
{
    return page->flags & VRES_PAGE_READY;
}

static inline int vres_pg_own(vres_page_t *page)
{
    return page->flags & VRES_PAGE_OWN;
}

static inline int vres_pg_cand(vres_page_t *page)
{
    return page->flags & VRES_PAGE_CAND;
}

static inline int vres_pg_update(vres_page_t *page)
{
    return page->flags & VRES_PAGE_UPDATE;
}

static inline int vres_pg_cmpl(vres_page_t *page)
{
    return page->flags & VRES_PAGE_CMPL;
}

static inline int vres_pg_pgsave(vres_page_t *page)
{
    return page->flags & VRES_PAGE_SAVE;
}

void vres_page_lock_init();
int vres_page_calc_holders(vres_page_t *page);
void vres_page_put(vres_t *resource, void *entry);
int vres_page_update(vres_page_t *page, char *buf);
int vres_page_get_hid(vres_page_t *page, vres_id_t id);
vres_index_t vres_page_update_index(vres_page_t *page);
int vres_page_protect(vres_t *resource, vres_page_t *page);
void *vres_page_get(vres_t *resource, vres_page_t **page, int flags);
void vres_page_clear_holder_list(vres_t *resource, vres_page_t *page);
int vres_page_add_holder(vres_t *resource, vres_page_t *page, vres_id_t id);
int vres_page_get_diff(vres_page_t *page, vres_version_t version, int *diff);
int vres_page_search_holder_list(vres_t *resource, vres_page_t *page, vres_id_t id);
int vres_page_update_holder_list(vres_t *resource, vres_page_t *page, vres_id_t *holders, int nr_holders);

#endif
