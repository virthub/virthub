#ifndef _FILE_H
#define _FILE_H

#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include "util.h"

#define VMAP_RDONLY O_RDONLY
#define VMAP_WRONLY (O_WRONLY | O_CREAT | O_TRUNC)
#define VMAP_MODE   (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)

typedef struct vmap_file {
    unsigned long owner;
    unsigned long area;
    unsigned long off;
    int flg;
    int fd;
} vmap_file_t;

typedef struct vmap_file_header {
    unsigned long ver;
    size_t length;
} vmap_file_header_t;

void vmap_file_close(vmap_file_t *file);
int vmap_file_setlen(const char *path, size_t length);
int vmap_file_read(vmap_file_t *file, void *buf, size_t len);
int vmap_file_write(vmap_file_t *file, void *buf, size_t len);
int vmap_file_update(const char *path, unsigned long off, void *buf, size_t len);
int vmap_file_open(const char *path, unsigned long off, int flg, vmap_file_t *file);

#endif
