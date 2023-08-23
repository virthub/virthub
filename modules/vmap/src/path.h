#ifndef _PATH_H
#define _PATH_H

#include <stdio.h>
#include <sys/stat.h>
#include "file.h"

#define VMAP_PATH_MAX      256
#define VMAP_PATH_MARK     "~"
#define VMAP_PATH_MARK_LEN sizeof(VMAP_PATH_MARK)

extern char vmap_path[VMAP_PATH_MAX];

void vmap_path_mark(char *path);
void vmap_path_unmark(char *path);
void vmap_path_check_dir(const char *path);
void vmap_path_append(char *path, unsigned long val);
void vmap_path_get_area(vmap_file_t *file, char *path);
void vmap_path_get_header(vmap_file_t *file, char *path);
void vmap_path_extract(const char *path, unsigned long *owner, unsigned long *area);

int vmap_path_is_marked(char *path);
int vmap_path_check(vmap_file_t *file, char *path);

#endif
