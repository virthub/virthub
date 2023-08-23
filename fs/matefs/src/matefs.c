#include "matefs.h"

static int fill_dir_plus = 0;

void matefs_init(char *path)
{
    int pos;

    if (!path || strlen(path) >= MATEFS_PATH_MAX) {
        log_err("invalid path");
        exit(1);
    }
    pos = strlen(path) - 1;
    while (pos > 0) {
        if (path[pos] != '/')
            break;
        path[pos] = '\0';
    }
    strncpy(matefs_path, path, MATEFS_PATH_MAX);
}


static int matefs_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi)
{
    if (!strcmp(path, "/") || matefs_path_can_migrate(path)) {
        memset(stbuf, 0, sizeof(struct stat));
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        if (matefs_path_can_migrate(path))
            return -EMIGRATE;
    } else {
        int ret;
        char pathname[MATEFS_PATH_MAX];

        ret = matefs_path_get(path, pathname);
        if (ret) {
            log_err("failed to get path, path=%s", path);
            return ret;
        }
        if (lstat(pathname, stbuf) == -1)
            return -errno;
    }
    return 0;
}


static int matefs_access(const char *path, int mask)
{
    int ret;
    char pathname[MATEFS_PATH_MAX];

    log_matefs_access("path=%s", path);
    if (matefs_path_can_migrate(path)) {
        log_err("invalid path, path=%s", path);
        return -EINVAL;
    }
    ret = matefs_path_get(path, pathname);
    if (ret) {
        log_err("failed to get path, path=%s", path);
        return ret;
    }
    if (access(pathname, mask) == -1)
        return -errno;
    return 0;
}


static int matefs_readlink(const char *path, char *buf, size_t size)
{
    int ret;
    char pathname[MATEFS_PATH_MAX];

    log_matefs_readlink("path=%s", path);
    if (matefs_path_can_migrate(path)) {
        log_err("invalid path, path=%s", path);
        return -EINVAL;
    }
    ret = matefs_path_get(path, pathname);
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


static int matefs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                          off_t offset, struct fuse_file_info *fi,
                          enum fuse_readdir_flags flags)
{
    int ret;
    DIR *dir;
    struct dirent *ent;
    char pathname[MATEFS_PATH_MAX];

    (void) offset;
    (void) fi;
    (void) flags;
    log_matefs_readdir("path=%s", path);
    if (matefs_path_can_migrate(path)) {
        log_err("invalid path, path=%s", path);
        return -EINVAL;
    }
    ret = matefs_path_get(path, pathname);
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
            if (filler(buf, ent->d_name, &st, 0, fill_dir_plus))
                break;
        }
    } while (ent);
    closedir(dir);
    return 0;
}


static int matefs_mknod(const char *path, mode_t mode, dev_t rdev)
{
    int ret;
    char pathname[MATEFS_PATH_MAX];

    log_matefs_mknod("path=%s", path);
    if (matefs_path_can_migrate(path)) {
        log_err("invalid path, path=%s", path);
        return -EINVAL;
    }
    ret = matefs_path_get(path, pathname);
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


static int matefs_mkdir(const char *path, mode_t mode)
{
    int ret;
    char pathname[MATEFS_PATH_MAX];

    log_matefs_mkdir("path=%s", path);
    if (matefs_path_can_migrate(path)) {
        log_err("invalid path, path=%s", path);
        return -EINVAL;
    }
    ret = matefs_path_get(path, pathname);
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


static int matefs_unlink(const char *path)
{
    int ret;
    char pathname[MATEFS_PATH_MAX];

    log_matefs_unlink("path=%s", path);
    if (matefs_path_can_migrate(path)) {
        log_err("invalid path, path=%s", path);
        return -EINVAL;
    }
    ret = matefs_path_get(path, pathname);
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


static int matefs_rmdir(const char *path)
{
    int ret;
    char pathname[MATEFS_PATH_MAX];

    log_matefs_rmdir("path=%s", path);
    if (matefs_path_can_migrate(path)) {
        log_err("invalid path, path=%s", path);
        return -EINVAL;
    }
    ret = matefs_path_get(path, pathname);
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


static int matefs_symlink(const char *from, const char *to)
{
    int ret;
    char src[MATEFS_PATH_MAX];
    char dest[MATEFS_PATH_MAX];

    log_matefs_symlink("from=%s, to=%s", from, to);
    if (matefs_path_can_migrate(from) || matefs_path_can_migrate(to)) {
        log_err("invalid path, from=%s, to=%s", from, to);
        return -EINVAL;
    }
    ret = matefs_path_get(from, src);
    if (ret) {
        log_err("failed to get path, from=%s", from);
        return ret;
    }
    ret = matefs_path_get(to, dest);
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



static int matefs_rename(const char *from, const char *to, unsigned int flags)
{
    int ret;
    char src[MATEFS_PATH_MAX];
    char dest[MATEFS_PATH_MAX];

    (void) flags;
    log_matefs_rename("from=%s, to=%s", from, to);
    if (matefs_path_can_migrate(from) || matefs_path_can_migrate(to)) {
        log_err("invalid path, from=%s, to=%s", from, to);
        return -EINVAL;
    }
    ret = matefs_path_get(from, src);
    if (ret) {
        log_err("failed to get path, from=%s", from);
        return ret;
    }
    ret = matefs_path_get(to, dest);
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


static int matefs_link(const char *from, const char *to)
{
    int ret;
    char src[MATEFS_PATH_MAX];
    char dest[MATEFS_PATH_MAX];

    log_matefs_link("from=%s, to=%s", from, to);
    if (matefs_path_can_migrate(from) || matefs_path_can_migrate(to)) {
        log_err("invalid path, from=%s, to=%s", from, to);
        return -EINVAL;
    }
    ret = matefs_path_get(from, src);
    if (ret) {
        log_err("failed to get path, from=%s", from);
        return ret;
    }
    ret = matefs_path_get(to, dest);
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


static int matefs_chmod(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    int ret;
    char pathname[MATEFS_PATH_MAX];

    (void) fi;
    log_matefs_chmod("path=%s", path);
    if (matefs_path_can_migrate(path)) {
        log_err("invalid path, path=%s", path);
        return -EINVAL;
    }
    ret = matefs_path_get(path, pathname);
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


static int matefs_chown(const char *path, uid_t uid, gid_t gid, struct fuse_file_info *fi)
{
    int ret;
    char pathname[MATEFS_PATH_MAX];

    (void) fi;
    log_matefs_chown("path=%s", path);
    if (matefs_path_can_migrate(path)) {
        log_err("invalid path, path=%s", path);
        return -EINVAL;
    }
    ret = matefs_path_get(path, pathname);
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


static int matefs_truncate(const char *path, off_t size, struct fuse_file_info *fi)
{
    int ret;
    char pathname[MATEFS_PATH_MAX];

    (void) fi;
    log_matefs_truncate("path=%s", path);
    if (matefs_path_can_migrate(path)) {
        log_err("invalid path, path=%s", path);
        return -EINVAL;
    }
    ret = matefs_path_get(path, pathname);
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


static int matefs_utimens(const char *path, const struct timespec ts[2], struct fuse_file_info *fi)
{
    int ret;
    char pathname[MATEFS_PATH_MAX];

    (void) fi;
    log_matefs_utimens("path=%s", path);
    if (matefs_path_can_migrate(path)) {
        log_err("invalid path, path=%s", path);
        return -EINVAL;
    }
    ret = matefs_path_get(path, pathname);
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


static int matefs_open(const char *path, struct fuse_file_info *fi)
{
    int ret;
    char pathname[MATEFS_PATH_MAX];

    log_matefs_open("path=%s", path);
    if (matefs_path_can_migrate(path)) {
        log_err("invalid path, path=%s", path);
        return -EINVAL;
    }
    ret = matefs_path_get(path, pathname);
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


static int matefs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    int ret;
    char pathname[MATEFS_PATH_MAX];

    log_matefs_read("path=%s", path);
    if (matefs_path_can_migrate(path)) {
        log_err("invalid path, path=%s", path);
        return -EINVAL;
    }
    ret = matefs_path_get(path, pathname);
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


static int matefs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    int ret;
    char pathname[MATEFS_PATH_MAX];

    log_matefs_write("path=%s", path);
    if (matefs_path_can_migrate(path)) {
        log_err("invalid path, path=%s", path);
        return -EINVAL;
    }
    ret = matefs_path_get(path, pathname);
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


static int matefs_statfs(const char *path, struct statvfs *stbuf)
{
    int ret;
    char pathname[MATEFS_PATH_MAX];

    log_matefs_statfs("path=%s", path);
    if (matefs_path_can_migrate(path)) {
        log_err("invalid path, path=%s", path);
        return -EINVAL;
    }
    ret = matefs_path_get(path, pathname);
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


static int matefs_release(const char *path, struct fuse_file_info *fi)
{
    int ret;
    char pathname[MATEFS_PATH_MAX];

    log_matefs_release("path=%s", path);
    if (matefs_path_can_migrate(path)) {
        log_err("invalid path, path=%s", path);
        return -EINVAL;
    }
    ret = matefs_path_get(path, pathname);
    if (ret) {
        log_err("failed to get path, path=%s", path);
        return ret;
    }
    close(fi->fh);
    return 0;
}


static int matefs_fsync(const char *path, int isdatasync, struct fuse_file_info *fi)
{
    int ret;
    char pathname[MATEFS_PATH_MAX];

    log_matefs_fsync("path=%s", path);
    if (matefs_path_can_migrate(path)) {
        log_err("invalid path, path=%s", path);
        return -EINVAL;
    }
    ret = matefs_path_get(path, pathname);
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


static int matefs_fallocate(const char *path, int mode, off_t offset, off_t length, struct fuse_file_info *fi)
{
    int ret;
    char pathname[MATEFS_PATH_MAX];

    log_matefs_fallocate("path=%s", path);
    if (matefs_path_can_migrate(path)) {
        log_err("invalid path, path=%s", path);
        return -EINVAL;
    }
    ret = matefs_path_get(path, pathname);
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


static int matefs_setxattr(const char *path, const char *name, const char *value, size_t size, int flags)
{
    int ret;
    char pathname[MATEFS_PATH_MAX];

    log_matefs_setxattr("path=%s", path);
    if (matefs_path_can_migrate(path)) {
        log_err("invalid path, path=%s", path);
        return -EINVAL;
    }
    ret = matefs_path_get(path, pathname);
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


static int matefs_getxattr(const char *path, const char *name, char *value, size_t size)
{
    int ret;
    char pathname[MATEFS_PATH_MAX];

    log_matefs_getxattr("path=%s", path);
    if (matefs_path_can_migrate(path)) {
        log_err("invalid path, path=%s", path);
        return -EINVAL;
    }
    ret = matefs_path_get(path, pathname);
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


static int matefs_listxattr(const char *path, char *list, size_t size)
{
    int ret;
    char pathname[MATEFS_PATH_MAX];

    log_matefs_listxattr("path=%s", path);
    if (matefs_path_can_migrate(path)) {
        log_err("invalid path, path=%s", path);
        return -EINVAL;
    }
    ret = matefs_path_get(path, pathname);
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


static int matefs_removexattr(const char *path, const char *name)
{
    int ret;
    char pathname[MATEFS_PATH_MAX];

    log_matefs_removexattr("path=%s", path);
    if (matefs_path_can_migrate(path)) {
        log_err("invalid path, path=%s", path);
        return -EINVAL;
    }
    ret = matefs_path_get(path, pathname);
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


static struct fuse_operations matefs_oper = {
    .getattr     = matefs_getattr,
    .access      = matefs_access,
    .readlink    = matefs_readlink,
    .readdir     = matefs_readdir,
    .mknod       = matefs_mknod,
    .mkdir       = matefs_mkdir,
    .symlink     = matefs_symlink,
    .unlink      = matefs_unlink,
    .rmdir       = matefs_rmdir,
    .rename      = matefs_rename,
    .link        = matefs_link,
    .chmod       = matefs_chmod,
    .chown       = matefs_chown,
    .truncate    = matefs_truncate,
    .utimens     = matefs_utimens,
    .open        = matefs_open,
    .read        = matefs_read,
    .write       = matefs_write,
    .statfs      = matefs_statfs,
    .release     = matefs_release,
    .fsync       = matefs_fsync,
    .fallocate   = matefs_fallocate,
    .setxattr    = matefs_setxattr,
    .getxattr    = matefs_getxattr,
    .listxattr   = matefs_listxattr,
    .removexattr = matefs_removexattr,
};


void usage()
{
    printf("usage: matefs [name] [home] [mnt]\n");
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
    strcpy(matefs_user, argv[1]);
    matefs_init(argv[2]);
    log("user=%s\n", argv[1]);
    log("home=%s\n", argv[2]);
    log("mount=%s\n", argv[3]);
    args[0] = argv[0];
    args[1] = argv[3];
    args[2] = "-f";
    args[3] = "-o";
    args[4] = "max_readahead=0";
    return fuse_main(nr_args, args, &matefs_oper, NULL);
}
