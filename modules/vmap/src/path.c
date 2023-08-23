/* path.c
 *
 * Copyright (C) 2019 Yi-Wei Ci
 *
 * Distributed under the terms of the MIT license.
 */

#include "path.h"

void vmap_path_mark(char *path)
{
    strcat(path, VMAP_PATH_MARK);
}


void vmap_path_unmark(char *path)
{
    size_t len = strlen(path);

    if (len > VMAP_PATH_MARK_LEN)
        path[len - VMAP_PATH_MARK_LEN] = '\0';
}


int vmap_path_is_marked(char *path)
{
    size_t len = strlen(path);

    return 0 == strncmp(path + len - VMAP_PATH_MARK_LEN, VMAP_PATH_MARK, VMAP_PATH_MARK_LEN);
}


void vmap_path_append(char *path, unsigned long val)
{
    sprintf(path + strlen(path), "%lx", val);
}


void vmap_path_get_area(vmap_file_t *file, char *path)
{
    sprintf(path, "%s/%lx/mem/%lx/", vmap_path, file->owner, file->area);
}


void vmap_path_get_off(vmap_file_t *file, char *path)
{
    vmap_path_get_area(file, path);
    vmap_path_append(path, file->off);
}


void vmap_path_get_header(vmap_file_t *file, char *path)
{
    vmap_path_get_area(file, path);
    vmap_path_mark(path);
}


int vmap_path_check(vmap_file_t *file, char *path)
{
    int fd;
    int ver = 0;
    char name[VMAP_PATH_MAX];

    vmap_path_get_header(file, name);
    fd = open(name, VMAP_RDONLY);
    if (fd >= 0) {
        int flg = file->flg;
        vmap_file_header_t hdr;

        if (read(fd, &hdr, sizeof(vmap_file_header_t)) != sizeof(vmap_file_header_t)) {
            close(fd);
            return -EIO;
        }

        if (VMAP_WRONLY == flg)
            ver = hdr.ver;
        else if (VMAP_RDONLY & flg)
            ver = !hdr.ver;
        close(fd);
    }
    vmap_path_get_off(file, path);
    if (ver > 0)
        vmap_path_mark(path);
    return 0;
}


void vmap_path_check_dir(const char *path)
{
    struct stat st;
    const char *p = path;
    char name[VMAP_PATH_MAX];

    if (!path || ('\0' == *path))
        return;

    while (p && *p++ != '\0') {
        name[0] = '\0';
        p = strchr(p, '/');
        if (p) {
            strncat(name, path, p - path);
            if (stat(name, &st))
                mkdir(name, S_IRWXU | S_IRWXG | S_IRWXO);
        }
    }
}


void vmap_path_extract(const char *path, unsigned long *owner, unsigned long *area)
{
    char *end;
    const char *p = path;

    if (*p++ != '/') {
        log_err("invalid path");
        return;
    }

    *owner = (pid_t)strtoul(p, &end, 16);
    if (p == end) {
        log_err("invalid path");
        return;
    }

    p = end + 1;
    *area = vmap_strip(strtoul(p, &end, 16));
}
