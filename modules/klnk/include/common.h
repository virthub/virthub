#ifndef _COMMON_H
#define _COMMON_H

#include <string.h>
#include <fuse3/fuse.h>
#include <fuse3/fuse_opt.h>
#include <fuse3/fuse_lowlevel.h>
#include "log_klnk.h"
#include "rbtree.h"

#define addr2str(addr) inet_ntoa(addr)
#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))
#define tree_entry list_entry
#define rbtree_new rb_tree_new
#define rbtree_find rb_tree_find
#define rbtree_insert rb_tree_insert
#define rbtree_remove_node rb_tree_remove
#define rbtree_remove(tree, key) do { \
    rbtree_node_t *node = NULL; \
    if (!rbtree_find(tree, key, &node)) \
        rbtree_remove_node(tree, node); \
} while (0)

struct dirbuf {
	char *p;
	size_t size;
};

typedef struct rb_tree rbtree_t;
typedef struct rb_tree_node rbtree_node_t;

static inline void dirbuf_add(fuse_req_t req, struct dirbuf *b, const char *name, fuse_ino_t ino)
{
	struct stat stbuf;
	size_t oldsize = b->size;

	b->size += fuse_add_direntry(req, NULL, 0, name, NULL, 0);
	b->p = (char *)realloc(b->p, b->size);
	memset(&stbuf, 0, sizeof(stbuf));
	stbuf.st_ino = ino;
	fuse_add_direntry(req, b->p + oldsize, b->size - oldsize, name, &stbuf, b->size);
}

static inline int reply_buf_limited(fuse_req_t req, const char *buf, size_t bufsize, off_t off, size_t maxsize)
{
	if (off < bufsize)
		return fuse_reply_buf(req, buf + off, min(bufsize - off, maxsize));
	else
		return fuse_reply_buf(req, NULL, 0);
}

#endif
