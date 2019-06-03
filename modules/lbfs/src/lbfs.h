#ifndef _LBFS_H
#define _LBFS_H

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/xattr.h>
#include "path.h"

#ifdef DEBUG
#define LOG_LBFS_OPEN
#define LOG_LBFS_LINK
#define LOG_LBFS_READ
#define LOG_LBFS_CHMOD
#define LOG_LBFS_CHOWN
#define LOG_LBFS_CLOSE
#define LOG_LBFS_MKDIR
#define LOG_LBFS_RMDIR
#define LOG_LBFS_FSYNC
#define LOG_LBFS_MKNOD
#define LOG_LBFS_WRITE
#define LOG_LBFS_ACCESS
#define LOG_LBFS_RENAME
#define LOG_LBFS_STATFS
#define LOG_LBFS_UNLINK
#define LOG_LBFS_GETATTR
#define LOG_LBFS_READDIR
#define LOG_LBFS_RELEASE
#define LOG_LBFS_SYMLINK
#define LOG_LBFS_UTIMENS
#define LOG_LBFS_GETXATTR
#define LOG_LBFS_READLINK
#define LOG_LBFS_SETXATTR
#define LOG_LBFS_TRUNCATE
#define LOG_LBFS_FALLOCATE
#define LOG_LBFS_LISTXATTR
#define LOG_LBFS_REMOVEXATTR
#endif

#include "log_lbfs.h"

#define EMIGRATE 903

#endif
