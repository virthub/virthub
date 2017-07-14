/* mifs.c
 *
 * Copyright (C) 2017 Yi-Wei Ci
 *
 * Distributed under the terms of the MIT license.
 */

#include "mifs.h"

void mifs_init(char *path)
{
	int pos;

	if (!path || strlen(path) >= MIFS_PATH_MAX) {
		log_err("invalid path");
		exit(1);
	}

	pos = strlen(path) - 1;
	while (pos > 0) {
		if (path[pos] != '/')
			break;
		path[pos] = '\0';
	}

	strncpy(mifs_path, path, MIFS_PATH_MAX);
}


static int mifs_getattr(const char *path, struct stat *stbuf)
{
	if (!strcmp(path, "/") || mifs_path_can_migrate(path)) {
		memset(stbuf, 0, sizeof(struct stat));
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
		if (mifs_path_can_migrate(path))
			return -EMIGRATE;
	} else {
		int ret;
		char pathname[MIFS_PATH_MAX];

		ret = mifs_path_get(path, pathname);
		if (ret) {
			log_err("failed to get path, path=%s", path);
			return ret;
		}

		if (lstat(pathname, stbuf) == -1)
			return -errno;
	}

	return 0;
}


static int mifs_access(const char *path, int mask)
{
	int ret;
	char pathname[MIFS_PATH_MAX];

	log_mifs_access("path=%s", path);
	if (mifs_path_can_migrate(path)) {
		log_err("invalid path, path=%s", path);
		return -EINVAL;
	}

	ret = mifs_path_get(path, pathname);
	if (ret) {
		log_err("failed to get path, path=%s", path);
		return ret;
	}

	if (access(pathname, mask) == -1)
		return -errno;

	return 0;
}


static int mifs_readlink(const char *path, char *buf, size_t size)
{
	int ret;
	char pathname[MIFS_PATH_MAX];

	log_mifs_readlink("path=%s", path);
	if (mifs_path_can_migrate(path)) {
		log_err("invalid path, path=%s", path);
		return -EINVAL;
	}

	ret = mifs_path_get(path, pathname);
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


static int mifs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
	int ret;
	DIR *dir;
	struct dirent *ent;
	char pathname[MIFS_PATH_MAX];

	log_mifs_readdir("path=%s", path);
	if (mifs_path_can_migrate(path)) {
		log_err("invalid path, path=%s", path);
		return -EINVAL;
	}

	ret = mifs_path_get(path, pathname);
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


static int mifs_mknod(const char *path, mode_t mode, dev_t rdev)
{
	int ret;
	char pathname[MIFS_PATH_MAX];

	log_mifs_mknod("path=%s", path);
	if (mifs_path_can_migrate(path)) {
		log_err("invalid path, path=%s", path);
		return -EINVAL;
	}

	ret = mifs_path_get(path, pathname);
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


static int mifs_mkdir(const char *path, mode_t mode)
{
	int ret;
	char pathname[MIFS_PATH_MAX];

	log_mifs_mkdir("path=%s", path);
	if (mifs_path_can_migrate(path)) {
		log_err("invalid path, path=%s", path);
		return -EINVAL;
	}

	ret = mifs_path_get(path, pathname);
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


static int mifs_unlink(const char *path)
{
	int ret;
	char pathname[MIFS_PATH_MAX];

	log_mifs_unlink("path=%s", path);
	if (mifs_path_can_migrate(path)) {
		log_err("invalid path, path=%s", path);
		return -EINVAL;
	}

	ret = mifs_path_get(path, pathname);
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


static int mifs_rmdir(const char *path)
{
	int ret;
	char pathname[MIFS_PATH_MAX];

	log_mifs_rmdir("path=%s", path);
	if (mifs_path_can_migrate(path)) {
		log_err("invalid path, path=%s", path);
		return -EINVAL;
	}

	ret = mifs_path_get(path, pathname);
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


static int mifs_symlink(const char *from, const char *to)
{
	int ret;
	char src[MIFS_PATH_MAX];
	char dest[MIFS_PATH_MAX];

	log_mifs_symlink("from=%s, to=%s", from, to);
	if (mifs_path_can_migrate(from) || mifs_path_can_migrate(to)) {
		log_err("invalid path, from=%s, to=%s", from, to);
		return -EINVAL;
	}

	ret = mifs_path_get(from, src);
	if (ret) {
		log_err("failed to get path, from=%s", from);
		return ret;
	}

	ret = mifs_path_get(to, dest);
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



static int mifs_rename(const char *from, const char *to)
{
	int ret;
	char src[MIFS_PATH_MAX];
	char dest[MIFS_PATH_MAX];

	log_mifs_rename("from=%s, to=%s", from, to);
	if (mifs_path_can_migrate(from) || mifs_path_can_migrate(to)) {
		log_err("invalid path, from=%s, to=%s", from, to);
		return -EINVAL;
	}

	ret = mifs_path_get(from, src);
	if (ret) {
		log_err("failed to get path, from=%s", from);
		return ret;
	}

	ret = mifs_path_get(to, dest);
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


static int mifs_link(const char *from, const char *to)
{
	int ret;
	char src[MIFS_PATH_MAX];
	char dest[MIFS_PATH_MAX];

	log_mifs_link("from=%s, to=%s", from, to);
	if (mifs_path_can_migrate(from) || mifs_path_can_migrate(to)) {
		log_err("invalid path, from=%s, to=%s", from, to);
		return -EINVAL;
	}

	ret = mifs_path_get(from, src);
	if (ret) {
		log_err("failed to get path, from=%s", from);
		return ret;
	}

	ret = mifs_path_get(to, dest);
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


static int mifs_chmod(const char *path, mode_t mode)
{
	int ret;
	char pathname[MIFS_PATH_MAX];

	log_mifs_chmod("path=%s", path);
	if (mifs_path_can_migrate(path)) {
		log_err("invalid path, path=%s", path);
		return -EINVAL;
	}

	ret = mifs_path_get(path, pathname);
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


static int mifs_chown(const char *path, uid_t uid, gid_t gid)
{
	int ret;
	char pathname[MIFS_PATH_MAX];

	log_mifs_chown("path=%s", path);
	if (mifs_path_can_migrate(path)) {
		log_err("invalid path, path=%s", path);
		return -EINVAL;
	}

	ret = mifs_path_get(path, pathname);
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


static int mifs_truncate(const char *path, off_t size)
{
	int ret;
	char pathname[MIFS_PATH_MAX];

	log_mifs_truncate("path=%s", path);
	if (mifs_path_can_migrate(path)) {
		log_err("invalid path, path=%s", path);
		return -EINVAL;
	}

	ret = mifs_path_get(path, pathname);
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


static int mifs_utimens(const char *path, const struct timespec ts[2])
{
	int ret;
	char pathname[MIFS_PATH_MAX];

	log_mifs_utimens("path=%s", path);
	if (mifs_path_can_migrate(path)) {
		log_err("invalid path, path=%s", path);
		return -EINVAL;
	}

	ret = mifs_path_get(path, pathname);
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


static int mifs_open(const char *path, struct fuse_file_info *fi)
{
	int ret;
	char pathname[MIFS_PATH_MAX];

	log_mifs_open("path=%s", path);
	if (mifs_path_can_migrate(path)) {
		log_err("invalid path, path=%s", path);
		return -EINVAL;
	}

	ret = mifs_path_get(path, pathname);
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


static int mifs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	int ret;
	char pathname[MIFS_PATH_MAX];

	log_mifs_read("path=%s", path);
	if (mifs_path_can_migrate(path)) {
		log_err("invalid path, path=%s", path);
		return -EINVAL;
	}

	ret = mifs_path_get(path, pathname);
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


static int mifs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	int ret;
	char pathname[MIFS_PATH_MAX];

	log_mifs_write("path=%s", path);
	if (mifs_path_can_migrate(path)) {
		log_err("invalid path, path=%s", path);
		return -EINVAL;
	}

	ret = mifs_path_get(path, pathname);
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


static int mifs_statfs(const char *path, struct statvfs *stbuf)
{
	int ret;
	char pathname[MIFS_PATH_MAX];

	log_mifs_statfs("path=%s", path);
	if (mifs_path_can_migrate(path)) {
		log_err("invalid path, path=%s", path);
		return -EINVAL;
	}

	ret = mifs_path_get(path, pathname);
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


static int mifs_release(const char *path, struct fuse_file_info *fi)
{
	int ret;
	char pathname[MIFS_PATH_MAX];

	log_mifs_release("path=%s", path);
	if (mifs_path_can_migrate(path)) {
		log_err("invalid path, path=%s", path);
		return -EINVAL;
	}

	ret = mifs_path_get(path, pathname);
	if (ret) {
		log_err("failed to get path, path=%s", path);
		return ret;
	}

	close(fi->fh);
	return 0;
}


static int mifs_fsync(const char *path, int isdatasync, struct fuse_file_info *fi)
{
	int ret;
	char pathname[MIFS_PATH_MAX];

	log_mifs_fsync("path=%s", path);
	if (mifs_path_can_migrate(path)) {
		log_err("invalid path, path=%s", path);
		return -EINVAL;
	}

	ret = mifs_path_get(path, pathname);
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


static int mifs_fallocate(const char *path, int mode, off_t offset, off_t length, struct fuse_file_info *fi)
{
	int ret;
	char pathname[MIFS_PATH_MAX];

	log_mifs_fallocate("path=%s", path);
	if (mifs_path_can_migrate(path)) {
		log_err("invalid path, path=%s", path);
		return -EINVAL;
	}

	ret = mifs_path_get(path, pathname);
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


static int mifs_setxattr(const char *path, const char *name, const char *value, size_t size, int flags)
{
	int ret;
	char pathname[MIFS_PATH_MAX];

	log_mifs_setxattr("path=%s", path);
	if (mifs_path_can_migrate(path)) {
		log_err("invalid path, path=%s", path);
		return -EINVAL;
	}

	ret = mifs_path_get(path, pathname);
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


static int mifs_getxattr(const char *path, const char *name, char *value, size_t size)
{
	int ret;
	char pathname[MIFS_PATH_MAX];

	log_mifs_getxattr("path=%s", path);
	if (mifs_path_can_migrate(path)) {
		log_err("invalid path, path=%s", path);
		return -EINVAL;
	}

	ret = mifs_path_get(path, pathname);
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


static int mifs_listxattr(const char *path, char *list, size_t size)
{
	int ret;
	char pathname[MIFS_PATH_MAX];

	log_mifs_listxattr("path=%s", path);
	if (mifs_path_can_migrate(path)) {
		log_err("invalid path, path=%s", path);
		return -EINVAL;
	}

	ret = mifs_path_get(path, pathname);
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


static int mifs_removexattr(const char *path, const char *name)
{
	int ret;
	char pathname[MIFS_PATH_MAX];

	log_mifs_removexattr("path=%s", path);
	if (mifs_path_can_migrate(path)) {
		log_err("invalid path, path=%s", path);
		return -EINVAL;
	}

	ret = mifs_path_get(path, pathname);
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


static struct fuse_operations mifs_oper = {
	.getattr		= mifs_getattr,
	.access			= mifs_access,
	.readlink		= mifs_readlink,
	.readdir		= mifs_readdir,
	.mknod			= mifs_mknod,
	.mkdir			= mifs_mkdir,
	.symlink		= mifs_symlink,
	.unlink			= mifs_unlink,
	.rmdir			= mifs_rmdir,
	.rename			= mifs_rename,
	.link				= mifs_link,
	.chmod			= mifs_chmod,
	.chown			= mifs_chown,
	.truncate		= mifs_truncate,
	.utimens		= mifs_utimens,
	.open				= mifs_open,
	.read				= mifs_read,
	.write			= mifs_write,
	.statfs			= mifs_statfs,
	.release		= mifs_release,
	.fsync			= mifs_fsync,
	.fallocate	= mifs_fallocate,
	.setxattr		= mifs_setxattr,
	.getxattr		= mifs_getxattr,
	.listxattr	= mifs_listxattr,
	.removexattr= mifs_removexattr,
};


void usage()
{
	printf("usage: mifs [home]\n");
	printf("[home]: /path/to/home\n");
}


int main(int argc, char *argv[])
{
	const int nr_args = 5;
	char *args[nr_args];

	umask(0);
	if (argc != 2) {
		usage();
		return -EINVAL;
	}

	mifs_init(argv[1]);
	log("home=%s\n", argv[1]);
	log("mount=%s\n", PATH_MNT);

	args[0] = argv[0];
	args[1] = PATH_MNT;
	args[2] = "-f";
	args[3] = "-o";
	args[4] = "max_readahead=0";
	return fuse_main(nr_args, args, &mifs_oper, NULL);
}
