#include "vmap.h"

char vmap_path[VMAP_PATH_MAX] = {0};

void vmap_init(char *path)
{
    strncpy(vmap_path, path, VMAP_PATH_MAX);
}


int vmap_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi)
{
    memset(stbuf, 0, sizeof(struct stat));
    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
    } else {
        stbuf->st_mode = S_IFREG | 0777;
        stbuf->st_nlink = 1;
        stbuf->st_size = 0xffffffff;
    }

    return 0;
}


int vmap_read(const char *path, char *buf, size_t size, loff_t off, struct fuse_file_info *fi)
{
    int ret;
    vmap_file_t file;

    if ((off != vmap_page_align(off)) || (size > PAGE_SIZE)) {
        log_err("invalid parameters");
        return -EINVAL;
    }

    log_vmap_read(path, size);
    ret = vmap_file_open(path, off, VMAP_RDONLY, &file);
    if (ret < 0) {
        if (-ENOENT == ret) {
            memset(buf, 0, size);
            return size;
        } else
            return ret;
    }

    ret = vmap_file_read(&file, buf, size);
    vmap_file_close(&file);
    return ret;
}


int vmap_setxattr(const char *path, const char *name, const char *value, size_t size, int flags)
{
    int ret = -EINVAL;

    if (vmap_is_addr(name))
        ret = vmap_file_update(path, vmap_strtoul(name), (void *)value, size);
    else if (!strncmp(name, VMAP_ATTR_SIZE, strlen(VMAP_ATTR_SIZE)))
        ret = vmap_file_setlen(path, *(size_t *)value);

    log_vmap_setxattr(path, name, ret);
    return ret;
}


struct fuse_operations vmap_oper = {
    .read        = vmap_read,
    .getattr    = vmap_getattr,
    .setxattr    = vmap_setxattr,
};


void usage()
{
    printf("usage: vmap [home] [mountpoint]\n");
    printf("[home]: /path/to/home\n");
    printf("[mountpoint]: /path/to/mountpoint\n");
}


int main(int argc, char *argv[])
{
    const int nr_args = 5;
    char *args[nr_args];

    umask(0);
    if (argc != 3) {
        usage();
        return -EINVAL;
    }

    vmap_init(argv[1]);
    log("path=%s\n", argv[1]);
    log("mount=%s\n", argv[2]);

    args[0] = argv[0];
    args[1] = argv[2];
    args[2] = "-f";
    args[3] = "-o";
    args[4] = "max_readahead=0";
    return fuse_main(nr_args, args, &vmap_oper, NULL);
}
