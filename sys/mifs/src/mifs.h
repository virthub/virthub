#ifndef _MIFS_H
#define _MIFS_H

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/xattr.h>
#include "path.h"

#ifdef DEBUG
#define LOG_MIFS_OPEN
#define LOG_MIFS_LINK
#define LOG_MIFS_READ
#define LOG_MIFS_CHMOD
#define LOG_MIFS_CHOWN
#define LOG_MIFS_CLOSE
#define LOG_MIFS_MKDIR
#define LOG_MIFS_RMDIR
#define LOG_MIFS_FSYNC
#define LOG_MIFS_MKNOD
#define LOG_MIFS_WRITE
#define LOG_MIFS_ACCESS
#define LOG_MIFS_RENAME
#define LOG_MIFS_STATFS
#define LOG_MIFS_UNLINK
#define LOG_MIFS_GETATTR
#define LOG_MIFS_READDIR
#define LOG_MIFS_RELEASE
#define LOG_MIFS_SYMLINK
#define LOG_MIFS_UTIMENS
#define LOG_MIFS_GETXATTR
#define LOG_MIFS_READLINK
#define LOG_MIFS_SETXATTR
#define LOG_MIFS_TRUNCATE
#define LOG_MIFS_FALLOCATE
#define LOG_MIFS_LISTXATTR
#define LOG_MIFS_REMOVEXATTR
#endif

#include "log_mifs.h"

#define EMIGRATE 903

#endif
