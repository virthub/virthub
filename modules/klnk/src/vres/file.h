#ifndef _FILE_H
#define _FILE_H

#include <list.h>
#include <vres.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdbool.h>
#include <pthread.h>
#include <defaults.h>

#define FILE_RDONLY    VRES_RDONLY
#define FILE_RDWR      VRES_RDWR
#define FILE_CREAT     VRES_CREAT

#define FILE_STAT_INIT 1
#define FILE_PATH_MAX  64
#define FILE_BUF_SIZE  (1 << 20)

#define FILE_SEPARATOR '/'
#define FILE_ROOT_PATH "/"

#define vres_file_get_desc(entry, type) ((type *)*((entry)->desc))
#define vres_file_items_count(entry, item_size) (vres_file_size((entry)->file) / item_size)

#ifdef SHOW_FILE
#define LOG_FILE_MKDIR
#define LOG_FILE_RMDIR
#define LOG_FILE_DIR_PUSH
#define LOG_FILE_HANDLE_DIR
#define LOG_FILE_HANDLE_FILE
#define LOG_FILE_DELETE_FILE
#endif

#include "log_file.h"

typedef char ** vres_file_desc_t;

typedef struct vres_file {
    char f_path[FILE_PATH_MAX];
    rbtree_node_t f_node;
    unsigned long f_pos;
    size_t f_size;
    char *f_buf;

} vres_file_t;

typedef struct vres_file_dir {
    char d_path[FILE_PATH_MAX];
    pthread_rwlock_t d_lock;
    rbtree_node_t d_node;
    rbtree_t d_tree;
    int d_count;
} vres_file_dir_t;

typedef struct vres_file_name {
    char name[FILE_PATH_MAX];
    rbtree_node_t node;
} vres_file_name_t;

typedef struct vres_file_entry {
    vres_file_desc_t desc;
    vres_file_t *file;
    size_t size;
} vres_file_entry_t;

typedef int (*vres_file_callback_t)(char *path, void *arg);

int vres_file_rmdir(char *path);
int vres_file_mkdir(char *path);
int vres_file_remove(char *path);
int vres_file_size(vres_file_t *filp);
int vres_file_access(char *path, int mode);
int vres_file_dirname(char *path, char *dirname);
int vres_file_truncate(vres_file_t *filp, off_t length);
int vres_file_seek(vres_file_t *filp, long int offset, int origin);
int vres_file_append(vres_file_entry_t *entry, void *buf, size_t size);
int vres_file_handle_dir(vres_file_callback_t func, char *path, void *arg);
int vres_file_handle_file(vres_file_callback_t func, char *path, void *arg);
int vres_file_read(void *ptr, size_t size, size_t count, vres_file_t *filp);
int vres_file_write(const void *ptr, size_t size, size_t count, vres_file_t *filp);

bool vres_file_is_dir(char *path);
bool vres_file_is_empty_dir(char *path);

void vres_file_init();
void vres_file_close(vres_file_t *filp);
void vres_file_put_entry(vres_file_entry_t *entry);

vres_file_t *vres_file_open(char *path, const char *mode);
vres_file_entry_t *vres_file_get_entry(char *path, size_t size, int flags);

#endif
