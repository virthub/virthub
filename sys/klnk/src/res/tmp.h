#ifndef _TMP_H
#define _TMP_H

#include <list.h>
#include <vres.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <pthread.h>
#include "rbtree.h"

#define TMP_RDONLY			VRES_RDONLY
#define TMP_RDWR				VRES_RDWR
#define TMP_CREAT				VRES_CREAT

#define TMP_STAT_INIT		1
#define TMP_PATH_MAX 		80
#define TMP_BUF_SIZE		(1 << 16)

#define TMP_SEPARATOR		'/'
#define TMP_ROOT_PATH		"/"

#define tmp_get_desc(entry, type) ((type *)*((entry)->desc))
#define tmp_items_count(entry, item_size) (tmp_size((entry)->file) / item_size)

#ifdef SHOW_TMP
#define LOG_TMP_MKDIR
#define LOG_TMP_RMDIR
#define LOG_TMP_DIR_PUSH
#define LOG_TMP_HANDLE_DIR
#define LOG_TMP_HANDLE_FILE
#define LOG_TMP_DELETE_FILE
#endif

#include "log_tmp.h"

typedef char ** tmp_desc_t;

typedef struct tmp_file {
	unsigned long f_pos;
	char f_path[TMP_PATH_MAX];
	char *f_buf;
	size_t f_size;
} tmp_file_t;

typedef struct tmp_dir {
	pthread_rwlock_t d_lock;
	char d_path[TMP_PATH_MAX];
	int d_count;
	rbtree d_tree;
} tmp_dir_t;

typedef struct tmp_name {
	char name[TMP_PATH_MAX];
} tmp_name_t;

typedef struct tmp_entry {
	tmp_desc_t desc;
	tmp_file_t *file;
	size_t size;
} tmp_entry_t;

typedef int (*tmp_callback_t)(char *path, void *arg);

int tmp_rmdir(char *path);
int tmp_mkdir(char *path);
int tmp_remove(char *path);
int tmp_is_dir(char *path);
int tmp_size(tmp_file_t *filp);
int tmp_is_empty_dir(char *path);
int tmp_access(char *path, int mode);
int tmp_dirname(char *path, char *dirname);
int tmp_truncate(tmp_file_t *filp, off_t length);
int tmp_append(tmp_entry_t *entry, void *buf, size_t size);
int tmp_seek(tmp_file_t *filp, long int offset, int origin);
int tmp_handle_dir(tmp_callback_t func, char *path, void *arg);
int tmp_handle_file(tmp_callback_t func, char *path, void *arg);
int tmp_read(void *ptr, size_t size, size_t count, tmp_file_t *filp);
int tmp_write(const void *ptr, size_t size, size_t count, tmp_file_t *filp);

void tmp_init();
void tmp_close(tmp_file_t *filp);
void tmp_put_entry(tmp_entry_t *entry);

tmp_file_t *tmp_open(char *path, const char *mode);
tmp_entry_t *tmp_get_entry(char *path, size_t size, int flags);

#endif
