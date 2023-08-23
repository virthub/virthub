#ifndef _MATEFS_H
#define _MATEFS_H

#define FUSE_USE_VERSION 35

#include <fuse3/fuse.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/xattr.h>
#include "path.h"

#ifdef DEBUG
#define LOG_MATEFS_OPEN
#define LOG_MATEFS_LINK
#define LOG_MATEFS_READ
#define LOG_MATEFS_CHMOD
#define LOG_MATEFS_CHOWN
#define LOG_MATEFS_CLOSE
#define LOG_MATEFS_MKDIR
#define LOG_MATEFS_RMDIR
#define LOG_MATEFS_FSYNC
#define LOG_MATEFS_MKNOD
#define LOG_MATEFS_WRITE
#define LOG_MATEFS_ACCESS
#define LOG_MATEFS_RENAME
#define LOG_MATEFS_STATFS
#define LOG_MATEFS_UNLINK
#define LOG_MATEFS_GETATTR
#define LOG_MATEFS_READDIR
#define LOG_MATEFS_RELEASE
#define LOG_MATEFS_SYMLINK
#define LOG_MATEFS_UTIMENS
#define LOG_MATEFS_GETXATTR
#define LOG_MATEFS_READLINK
#define LOG_MATEFS_SETXATTR
#define LOG_MATEFS_TRUNCATE
#define LOG_MATEFS_FALLOCATE
#define LOG_MATEFS_LISTXATTR
#define LOG_MATEFS_REMOVEXATTR
#endif

#include "log_matefs.h"

#define EMIGRATE 903

#endif
