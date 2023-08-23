#include "file.h"
#include "path.h"

static int vmap_file_get(const char *path, unsigned long off, int flg, vmap_file_t *file)
{
    char *end;
    const char *p = path;

    if (*p++ != '/') {
        log_err("invalid path");
        return -EINVAL;
    }

    memset(file, 0, sizeof(vmap_file_t));
    file->owner = (pid_t)strtoul(p, &end, 16);
    if (p == end) {
        log_err("invalid path");
        return -EINVAL;
    }

    p = end + 1;
    file->area = vmap_strip(strtoul(p, &end, 16));
    file->off = vmap_strip(off);
    file->flg = flg;
    return 0;
}


int vmap_file_open(const char *path, unsigned long off, int flg, vmap_file_t *file)
{
    int ret;
    int fd = -1;
    char filename[VMAP_PATH_MAX];

    ret = vmap_file_get(path, off, flg, file);
    if (ret) {
        log_err("failed to get descriptor");
        return ret;
    }

    ret = vmap_path_check(file, filename);
    if (ret) {
        log_err("failed to get filename");
        return ret;
    }

    if (VMAP_RDONLY == flg) {
        fd = open(filename, VMAP_RDONLY);
        if (fd < 0) {
            if (vmap_path_is_marked(filename))
                vmap_path_unmark(filename);
            else
                vmap_path_mark(filename);
            fd = open(filename, VMAP_RDONLY);
        }
    } else if (VMAP_WRONLY == flg) {
        if (!access(filename, F_OK)) {
            char name[VMAP_PATH_MAX];

            strncpy(name, filename, VMAP_PATH_MAX);
            if (vmap_path_is_marked(name))
                vmap_path_unmark(name);
            else
                vmap_path_mark(name);
            ret = rename(filename, name);
            if (ret) {
                log_err("failed to rename");
                return ret;
            }
        }
        fd = open(filename, VMAP_WRONLY, VMAP_MODE);
        if (fd < 0) {
            vmap_path_check_dir(filename);
            fd = open(filename, VMAP_WRONLY, VMAP_MODE);
        }
    }

    if (fd < 0)
        return -EINVAL;

    file->fd = fd;
    return 0;
}


void vmap_file_close(vmap_file_t *file)
{
    close(file->fd);
}


int vmap_file_get_header(const char *path, vmap_file_header_t *hdr)
{
    int fd;
    int ret = 0;
    vmap_file_t file;
    char filename[VMAP_PATH_MAX];

    vmap_file_get(path, 0, 0, &file);
    vmap_path_get_header(&file, filename);
    fd = open(filename, VMAP_RDONLY);
    if (fd < 0) {
        vmap_path_check_dir(filename);
        memset(hdr, 0, sizeof(vmap_file_header_t));
    } else {
        if (read(fd, hdr, sizeof(vmap_file_header_t)) != sizeof(vmap_file_header_t)) {
            log_err("failed, path=%s", path);
            ret = -EIO;
        }
        close(fd);
    }
    return ret;
}


void vmap_file_put_header(const char *path, vmap_file_header_t *hdr)
{
    int fd;
    vmap_file_t file;
    char filename[VMAP_PATH_MAX];

    vmap_file_get(path, 0, 0, &file);
    vmap_path_get_header(&file, filename);
    fd = open(filename, VMAP_WRONLY, VMAP_MODE);
    if (fd < 0) {
        log_err("failed, path=%s", path);
        return;
    }
    write(fd, hdr, sizeof(vmap_file_header_t));
    close(fd);
}


void vmap_file_truncate(const char *path, unsigned long start, unsigned long end)
{
    char *p;
    vmap_file_t file;
    char filename[VMAP_PATH_MAX];

    vmap_file_get(path, 0, 0, &file);
    vmap_path_get_area(&file, filename);
    p = filename + strlen(filename);

    while (start < end) {
        vmap_path_append(filename, start);
        remove(filename);

        vmap_path_mark(filename);
        remove(filename);

        p[0] = '\0';
        start++;
    }
}


int vmap_file_setlen(const char *path, size_t length)
{
    vmap_file_header_t hdr;

    if (vmap_file_get_header(path, &hdr) < 0) {
        log_err("no entry");
        return -ENOENT;
    }

    if (length < hdr.length)
        vmap_file_truncate(path, length, hdr.length);

    hdr.length = length;
    hdr.ver = !hdr.ver;
    vmap_file_put_header(path, &hdr);
    return 0;
}


int vmap_file_read(vmap_file_t *file, void *buf, size_t len)
{
    return read(file->fd, buf, len);
}


int vmap_file_write(vmap_file_t *file, void *buf, size_t len)
{
    return write(file->fd, buf, len);
}


int vmap_file_update(const char *path, unsigned long off, void *buf, size_t len)
{
    int ret;
    vmap_file_t file;

    ret = vmap_file_open(path, off, VMAP_WRONLY, &file);
    if (ret < 0) {
        log_err("failed to open file");
        return ret;
    }

    if (vmap_file_write(&file, buf, len) != len) {
        log_err("failed to write");
        ret = -EIO;
    }

    vmap_file_close(&file);
    return ret;
}
