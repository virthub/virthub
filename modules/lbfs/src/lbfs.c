/* lbfs.c
 *
 * Copyright (C) 2019 Yi-Wei Ci
 *
 * Distributed under the terms of the MIT license.
 */

#include "lbfs.h"

void lbfs_init(char *path)
{
    int pos;

    if (!path || strlen(path) >= LBFS_PATH_MAX) {
        log_err("invalid path");
        exit(1);
    }

    pos = strlen(path) - 1;
    while (pos > 0) {
        if (path[pos] != '/')
            break;
        path[pos] = '\0';
    }

    strncpy(lbfs_path, path, LBFS_PATH_MAX);
}


static int lbfs_getattr(const char *path, struct stat *stbuf)
{
    if (!strcmp(path, "/") || lbfs_path_can_migrate(path)) {
        memset(stbuf, 0, sizeof(struct stat));
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        if (lbfs_path_can_migrate(path))
            return -EMIGRATE;
    } else {
        int ret;
        char pathname[LBFS_PATH_MAX];

        ret = lbfs_path_get(path, pathname);
        if (ret) {
            log_err("failed to get path, path=%s", path);
            return ret;
        }

        if (lstat(pathname, stbuf) == -1)
            return -errno;
    }

    return 0;
}


static int lbfs_access(const char *path, int mask)
{
    int ret;
    char pathname[LBFS_PATH_MAX];

    log_lbfs_access("path=%s", path);
    if (lbfs_path_can_migrate(path)) {
        log_err("invalid path, path=%s", path);
        return -EINVAL;
    }

    ret = lbfs_path_get(path, pathname);
    if (ret) {
        log_err("failed to get path, path=%s", path);
        return ret;
    }

    if (access(pathname, mask) == -1)
        return -errno;

    return 0;
}


static int lbfs_readlink(const char *path, char *buf, size_t size)
{
    int ret;
    char pathname[LBFS_PATH_MAX];

    log_lbfs_readlink("path=%s", path);
    if (lbfs_path_can_migrate(path)) {
        log_err("invalid path, path=%s", path);
        return -EINVAL;
    }

    ret = lbfs_path_get(path, pathname);
    if (ret) {
        log_err("failed to get path, path=%s", path);
        return ret;
    }

    ret = readlink(pathname, buf, size - 1);
    if (ret == -1) {
        log_err("failed, path=%s", path);
        return -errno;
    }

    buf[ret] = '\0';
    return 0;
}


static int lbfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
    int ret;
    DIR *dir;
    struct dirent *ent;
    char pathname[LBFS_PATH_MAX];

    log_lbfs_readdir("path=%s", path);
    if (lbfs_path_can_migrate(path)) {
        log_err("invalid path, path=%s", path);
        return -EINVAL;
    }

    ret = lbfs_path_get(path, pathname);
    if (ret) {
        log_err("failed to get path, path=%s", path);
        return ret;
    }

    dir = opendir(pathname);
    if (!dir) {
        log_err("failed to open directory, path=%s", path);
        return -errno;
    }

    do {
        ent = readdir(dir);
        if (ent) {
            struct stat st;

            memset(&st, 0, sizeof(st));
            st.st_ino = ent->d_ino;
            st.st_mode = ent->d_type << 12;
            if (filler(buf, ent->d_name, &st, 0))
                break;
        }
    } while (ent);

    closedir(dir);
    return 0;
}


static int lbfs_mknod(const char *path, mode_t mode, dev_t rdev)
{
    int ret;
    char pathname[LBFS_PATH_MAX];

    log_lbfs_mknod("path=%s", path);
    if (lbfs_path_can_migrate(path)) {
        log_err("invalid path, path=%s", path);
        return -EINVAL;
    }

    ret = lbfs_path_get(path, pathname);
    if (ret) {
        log_err("failed to get path, path=%s", path);
        return ret;
    }

    if (mknod(pathname, mode, rdev) == -1) {
        log_err("failed, path=%s", path);
        return -errno;
    }

    return 0;
}


static int lbfs_mkdir(const char *path, mode_t mode)
{
    int ret;
    char pathname[LBFS_PATH_MAX];

    log_lbfs_mkdir("path=%s", path);
    if (lbfs_path_can_migrate(path)) {
        log_err("invalid path, path=%s", path);
        return -EINVAL;
    }

    ret = lbfs_path_get(path, pathname);
    if (ret) {
        log_err("failed to get path, path=%s", path);
        return ret;
    }

    if (mkdir(pathname, mode) == -1) {
        log_err("failed, path=%s", path);
        return -errno;
    }

    return 0;
}


static int lbfs_unlink(const char *path)
{
    int ret;
    char pathname[LBFS_PATH_MAX];

    log_lbfs_unlink("path=%s", path);
    if (lbfs_path_can_migrate(path)) {
        log_err("invalid path, path=%s", path);
        return -EINVAL;
    }

    ret = lbfs_path_get(path, pathname);
    if (ret) {
        log_err("failed to get path, path=%s", path);
        return ret;
    }

    if (unlink(pathname) == -1) {
        log_err("failed, path=%s", path);
        return -errno;
    }

    return 0;
}


static int lbfs_rmdir(const char *path)
{
    int ret;
    char pathname[LBFS_PATH_MAX];

    log_lbfs_rmdir("path=%s", path);
    if (lbfs_path_can_migrate(path)) {
        log_err("invalid path, path=%s", path);
        return -EINVAL;
    }

    ret = lbfs_path_get(path, pathname);
    if (ret) {
        log_err("failed to get path, path=%s", path);
        return ret;
    }

    if (rmdir(pathname) == -1) {
        log_err("failed, path=%s", path);
        return -errno;
    }

    return 0;
}


static int lbfs_symlink(const char *from, const char *to)
{
    int ret;
    char src[LBFS_PATH_MAX];
    char dest[LBFS_PATH_MAX];

    log_lbfs_symlink("from=%s, to=%s", from, to);
    if (lbfs_path_can_migrate(from) || lbfs_path_can_migrate(to)) {
        log_err("invalid path, from=%s, to=%s", from, to);
        return -EINVAL;
    }

    ret = lbfs_path_get(from, src);
    if (ret) {
        log_err("failed to get path, from=%s", from);
        return ret;
    }

    ret = lbfs_path_get(to, dest);
    if (ret) {
        log_err("failed to get path, to=%s", to);
        return ret;
    }

    if (symlink(src, dest) == -1) {
        log_err("failed, from=%s, to=%s", from, to);
        return -errno;
    }

    return 0;
}



static int lbfs_rename(const char *from, const char *to)
{
    int ret;
    char src[LBFS_PATH_MAX];
    char dest[LBFS_PATH_MAX];

    log_lbfs_rename("from=%s, to=%s", from, to);
    if (lbfs_path_can_migrate(from) || lbfs_path_can_migrate(to)) {
        log_err("invalid path, from=%s, to=%s", from, to);
        return -EINVAL;
    }

    ret = lbfs_path_get(from, src);
    if (ret) {
        log_err("failed to get path, from=%s", from);
        return ret;
    }

    ret = lbfs_path_get(to, dest);
    if (ret) {
        log_err("failed to get path, to=%s", to);
        return ret;
    }

    if (rename(src, dest) == -1) {
        log_err("failed, from=%s, to=%s", from, to);
        return -errno;
    }

    return 0;
}


static int lbfs_link(const char *from, const char *to)
{
    int ret;
    char src[LBFS_PATH_MAX];
    char dest[LBFS_PATH_MAX];

    log_lbfs_link("from=%s, to=%s", from, to);
    if (lbfs_path_can_migrate(from) || lbfs_path_can_migrate(to)) {
        log_err("invalid path, from=%s, to=%s", from, to);
        return -EINVAL;
    }

    ret = lbfs_path_get(from, src);
    if (ret) {
        log_err("failed to get path, from=%s", from);
        return ret;
    }

    ret = lbfs_path_get(to, dest);
    if (ret) {
        log_err("failed to get path, to=%s", to);
        return ret;
    }

    if (link(src, dest) == -1) {
        log_err("failed, from=%s, to=%s", from, to);
        return -errno;
    }

    return 0;
}


static int lbfs_chmod(const char *path, mode_t mode)
{
    int ret;
    char pathname[LBFS_PATH_MAX];

    log_lbfs_chmod("path=%s", path);
    if (lbfs_path_can_migrate(path)) {
        log_err("invalid path, path=%s", path);
        return -EINVAL;
    }

    ret = lbfs_path_get(path, pathname);
    if (ret) {
        log_err("failed to get path, path=%s", path);
        return ret;
    }

    if (chmod(pathname, mode) == -1) {
        log_err("failed, path=%s", path);
        return -errno;
    }

    return 0;
}


static int lbfs_chown(const char *path, uid_t uid, gid_t gid)
{
    int ret;
    char pathname[LBFS_PATH_MAX];

    log_lbfs_chown("path=%s", path);
    if (lbfs_path_can_migrate(path)) {
        log_err("invalid path, path=%s", path);
        return -EINVAL;
    }

    ret = lbfs_path_get(path, pathname);
    if (ret) {
        log_err("failed to get path, path=%s", path);
        return ret;
    }

    if (lchown(pathname, uid, gid) == -1) {
        log_err("failed, path=%s", path);
        return -errno;
    }

    return 0;
}


static int lbfs_truncate(const char *path, off_t size)
{
    int ret;
    char pathname[LBFS_PATH_MAX];

    log_lbfs_truncate("path=%s", path);
    if (lbfs_path_can_migrate(path)) {
        log_err("invalid path, path=%s", path);
        return -EINVAL;
    }

    ret = lbfs_path_get(path, pathname);
    if (ret) {
        log_err("failed to get path, path=%s", path);
        return ret;
    }

    if (truncate(pathname, size) == -1) {
        log_err("failed, path=%s", path);
        return -errno;
    }

    return 0;
}


static int lbfs_utimens(const char *path, const struct timespec ts[2])
{
    int ret;
    char pathname[LBFS_PATH_MAX];

    log_lbfs_utimens("path=%s", path);
    if (lbfs_path_can_migrate(path)) {
        log_err("invalid path, path=%s", path);
        return -EINVAL;
    }

    ret = lbfs_path_get(path, pathname);
    if (ret) {
        log_err("failed to get path, path=%s", path);
        return ret;
    }

    if (utimensat(0, pathname, ts, AT_SYMLINK_NOFOLLOW) == -1) {
        log_err("failed, path=%s", path);
        return -errno;
    }

    return 0;
}


static int lbfs_open(const char *path, struct fuse_file_info *fi)
{
    int ret;
    char pathname[LBFS_PATH_MAX];

    log_lbfs_open("path=%s", path);
    if (lbfs_path_can_migrate(path)) {
        log_err("invalid path, path=%s", path);
        return -EINVAL;
    }

    ret = lbfs_path_get(path, pathname);
    if (ret) {
        log_err("failed to get path, path=%s", path);
        return ret;
    }

    ret = open(pathname, fi->flags);
    if (ret == -1) {
        log_err("failed, path=%s", path);
        return -errno;
    }

    fi->fh = ret;
    return 0;
}


static int lbfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    int ret;
    char pathname[LBFS_PATH_MAX];

    log_lbfs_read("path=%s", path);
    if (lbfs_path_can_migrate(path)) {
        log_err("invalid path, path=%s", path);
        return -EINVAL;
    }

    ret = lbfs_path_get(path, pathname);
    if (ret) {
        log_err("failed to get path, path=%s", path);
        return ret;
    }

    ret = pread(fi->fh, buf, size, offset);
    if (ret == -1) {
        log_err("failed, path=%s", path);
        return -errno;
    }

    return ret;
}


static int lbfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    int ret;
    char pathname[LBFS_PATH_MAX];

    log_lbfs_write("path=%s", path);
    if (lbfs_path_can_migrate(path)) {
        log_err("invalid path, path=%s", path);
        return -EINVAL;
    }

    ret = lbfs_path_get(path, pathname);
    if (ret) {
        log_err("failed to get path, path=%s", path);
        return ret;
    }

    ret = pwrite(fi->fh, buf, size, offset);
    if (ret == -1) {
        log_err("failed, path=%s", path);
        return -errno;
    }

    return ret;
}


static int lbfs_statfs(const char *path, struct statvfs *stbuf)
{
    int ret;
    char pathname[LBFS_PATH_MAX];

    log_lbfs_statfs("path=%s", path);
    if (lbfs_path_can_migrate(path)) {
        log_err("invalid path, path=%s", path);
        return -EINVAL;
    }

    ret = lbfs_path_get(path, pathname);
    if (ret) {
        log_err("failed to get path, path=%s", path);
        return ret;
    }

    if (statvfs(pathname, stbuf) == -1) {
        log_err("failed, path=%s", path);
        return -errno;
    }

    return 0;
}


static int lbfs_release(const char *path, struct fuse_file_info *fi)
{
    int ret;
    char pathname[LBFS_PATH_MAX];

    log_lbfs_release("path=%s", path);
    if (lbfs_path_can_migrate(path)) {
        log_err("invalid path, path=%s", path);
        return -EINVAL;
    }

    ret = lbfs_path_get(path, pathname);
    if (ret) {
        log_err("failed to get path, path=%s", path);
        return ret;
    }

    close(fi->fh);
    return 0;
}


static int lbfs_fsync(const char *path, int isdatasync, struct fuse_file_info *fi)
{
    int ret;
    char pathname[LBFS_PATH_MAX];

    log_lbfs_fsync("path=%s", path);
    if (lbfs_path_can_migrate(path)) {
        log_err("invalid path, path=%s", path);
        return -EINVAL;
    }

    ret = lbfs_path_get(path, pathname);
    if (ret) {
        log_err("failed to get path, path=%s", path);
        return ret;
    }

    if (fsync(fi->fh) == -1) {
        log_err("failed, path=%s", path);
        return -errno;
    }

    return 0;
}


static int lbfs_fallocate(const char *path, int mode, off_t offset, off_t length, struct fuse_file_info *fi)
{
    int ret;
    char pathname[LBFS_PATH_MAX];

    log_lbfs_fallocate("path=%s", path);
    if (lbfs_path_can_migrate(path)) {
        log_err("invalid path, path=%s", path);
        return -EINVAL;
    }

    ret = lbfs_path_get(path, pathname);
    if (ret) {
        log_err("failed to get path, path=%s", path);
        return ret;
    }

    ret = posix_fallocate(fi->fh, offset, length);
    if (ret) {
        log_err("failed, path=%s", path);
        return ret;
    }

    return 0;
}


static int lbfs_setxattr(const char *path, const char *name, const char *value, size_t size, int flags)
{
    int ret;
    char pathname[LBFS_PATH_MAX];

    log_lbfs_setxattr("path=%s", path);
    if (lbfs_path_can_migrate(path)) {
        log_err("invalid path, path=%s", path);
        return -EINVAL;
    }

    ret = lbfs_path_get(path, pathname);
    if (ret) {
        log_err("failed to get path, path=%s", path);
        return ret;
    }

    if (lsetxattr(pathname, name, value, size, flags) == -1) {
        log_err("failed, path=%s", path);
        return -errno;
    }

    return 0;
}


static int lbfs_getxattr(const char *path, const char *name, char *value, size_t size)
{
    int ret;
    char pathname[LBFS_PATH_MAX];

    log_lbfs_getxattr("path=%s", path);
    if (lbfs_path_can_migrate(path)) {
        log_err("invalid path, path=%s", path);
        return -EINVAL;
    }

    ret = lbfs_path_get(path, pathname);
    if (ret) {
        log_err("failed to get path, path=%s", path);
        return ret;
    }

    ret = lgetxattr(pathname, name, value, size);
    if (ret == -1) {
        log_err("failed, path=%s", path);
        return -errno;
    }

    return ret;
}


static int lbfs_listxattr(const char *path, char *list, size_t size)
{
    int ret;
    char pathname[LBFS_PATH_MAX];

    log_lbfs_listxattr("path=%s", path);
    if (lbfs_path_can_migrate(path)) {
        log_err("invalid path, path=%s", path);
        return -EINVAL;
    }

    ret = lbfs_path_get(path, pathname);
    if (ret) {
        log_err("failed to get path, path=%s", path);
        return ret;
    }

    ret = llistxattr(pathname, list, size);
    if (ret == -1) {
        log_err("failed, path=%s", path);
        return -errno;
    }

    return ret;
}


static int lbfs_removexattr(const char *path, const char *name)
{
    int ret;
    char pathname[LBFS_PATH_MAX];

    log_lbfs_removexattr("path=%s", path);
    if (lbfs_path_can_migrate(path)) {
        log_err("invalid path, path=%s", path);
        return -EINVAL;
    }

    ret = lbfs_path_get(path, pathname);
    if (ret) {
        log_err("failed to get path, path=%s", path);
        return ret;
    }

    if (lremovexattr(pathname, name) == -1) {
        log_err("failed, path=%s", path);
        return -errno;
    }

    return 0;
}


static struct fuse_operations lbfs_oper = {
    .getattr     = lbfs_getattr,
    .access      = lbfs_access,
    .readlink    = lbfs_readlink,
    .readdir     = lbfs_readdir,
    .mknod       = lbfs_mknod,
    .mkdir       = lbfs_mkdir,
    .symlink     = lbfs_symlink,
    .unlink      = lbfs_unlink,
    .rmdir       = lbfs_rmdir,
    .rename      = lbfs_rename,
    .link        = lbfs_link,
    .chmod       = lbfs_chmod,
    .chown       = lbfs_chown,
    .truncate    = lbfs_truncate,
    .utimens     = lbfs_utimens,
    .open        = lbfs_open,
    .read        = lbfs_read,
    .write       = lbfs_write,
    .statfs      = lbfs_statfs,
    .release     = lbfs_release,
    .fsync       = lbfs_fsync,
    .fallocate   = lbfs_fallocate,
    .setxattr    = lbfs_setxattr,
    .getxattr    = lbfs_getxattr,
    .listxattr   = lbfs_listxattr,
    .removexattr = lbfs_removexattr,
};


void usage()
{
    printf("usage: lbfs [name] [home] [mnt]\n");
    printf("[name]: specify the user\n");
    printf("[home]: the directory used for storing data\n");
    printf("[mnt]: mount point\n");
}


int main(int argc, char *argv[])
{
    const int nr_args = 5;
    char *args[nr_args];

    umask(0);
    if (argc != 4) {
        usage();
        return -EINVAL;
    }
    strcpy(lbfs_user, argv[1]);
    lbfs_init(argv[2]);
    log("user=%s\n", argv[1]);
    log("home=%s\n", argv[2]);
    log("mount=%s\n", argv[3]);
    args[0] = argv[0];
    args[1] = argv[3];
    args[2] = "-f";
    args[3] = "-o";
    args[4] = "max_readahead=0";
    return fuse_main(nr_args, args, &lbfs_oper, NULL);
}
