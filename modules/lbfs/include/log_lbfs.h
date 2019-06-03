#ifndef _LOG_LBFS_H
#define _LOG_LBFS_H

#include "log.h"

#ifdef LOG_LBFS_OPEN
#define log_lbfs_open log_debug
#else
#define log_lbfs_open(...) do {} while (0)
#endif

#ifdef LOG_LBFS_LINK
#define log_lbfs_link log_debug
#else
#define log_lbfs_link(...) do {} while (0)
#endif

#ifdef LOG_LBFS_READ
#define log_lbfs_read log_debug
#else
#define log_lbfs_read(...) do {} while (0)
#endif

#ifdef LOG_LBFS_CHMOD
#define log_lbfs_chmod log_debug
#else
#define log_lbfs_chmod(...) do {} while (0)
#endif

#ifdef LOG_LBFS_CHOWN
#define log_lbfs_chown log_debug
#else
#define log_lbfs_chown(...) do {} while (0)
#endif

#ifdef LOG_LBFS_CLOSE
#define log_lbfs_close log_debug
#else
#define log_lbfs_close(...) do {} while (0)
#endif

#ifdef LOG_LBFS_MKDIR
#define log_lbfs_mkdir log_debug
#else
#define log_lbfs_mkdir(...) do {} while (0)
#endif

#ifdef LOG_LBFS_RMDIR
#define log_lbfs_rmdir log_debug
#else
#define log_lbfs_rmdir(...) do {} while (0)
#endif

#ifdef LOG_LBFS_FSYNC
#define log_lbfs_fsync log_debug
#else
#define log_lbfs_fsync(...) do {} while (0)
#endif

#ifdef LOG_LBFS_MKNOD
#define log_lbfs_mknod log_debug
#else
#define log_lbfs_mknod(...) do {} while (0)
#endif

#ifdef LOG_LBFS_WRITE
#define log_lbfs_write log_debug
#else
#define log_lbfs_write(...) do {} while (0)
#endif

#ifdef LOG_LBFS_ACCESS
#define log_lbfs_access log_debug
#else
#define log_lbfs_access(...) do {} while (0)
#endif

#ifdef LOG_LBFS_UNLINK
#define log_lbfs_unlink log_debug
#else
#define log_lbfs_unlink(...) do {} while (0)
#endif

#ifdef LOG_LBFS_GETATTR
#define log_lbfs_getattr log_debug
#else
#define log_lbfs_getattr(...) do {} while (0)
#endif

#ifdef LOG_LBFS_READDIR
#define log_lbfs_readdir log_debug
#else
#define log_lbfs_readdir(...) do {} while (0)
#endif

#ifdef LOG_LBFS_RELEASE
#define log_lbfs_release log_debug
#else
#define log_lbfs_release(...) do {} while (0)
#endif

#ifdef LOG_LBFS_RENAME
#define log_lbfs_rename log_debug
#else
#define log_lbfs_rename(...) do {} while (0)
#endif

#ifdef LOG_LBFS_STATFS
#define log_lbfs_statfs log_debug
#else
#define log_lbfs_statfs(...) do {} while (0)
#endif

#ifdef LOG_LBFS_SYMLINK
#define log_lbfs_symlink log_debug
#else
#define log_lbfs_symlink(...) do {} while (0)
#endif


#ifdef LOG_LBFS_UTIMENS
#define log_lbfs_utimens log_debug
#else
#define log_lbfs_utimens(...) do {} while (0)
#endif

#ifdef LOG_LBFS_GETXATTR
#define log_lbfs_getxattr log_debug
#else
#define log_lbfs_getxattr(...) do {} while (0)
#endif

#ifdef LOG_LBFS_READLINK
#define log_lbfs_readlink log_debug
#else
#define log_lbfs_readlink(...) do {} while (0)
#endif

#ifdef LOG_LBFS_SETXATTR
#define log_lbfs_setxattr log_debug
#else
#define log_lbfs_setxattr(...) do {} while (0)
#endif

#ifdef LOG_LBFS_TRUNCATE
#define log_lbfs_truncate log_debug
#else
#define log_lbfs_truncate(...) do {} while (0)
#endif

#ifdef LOG_LBFS_FALLOCATE
#define log_lbfs_fallocate log_debug
#else
#define log_lbfs_fallocate(...) do {} while (0)
#endif

#ifdef LOG_LBFS_LISTXATTR
#define log_lbfs_listxattr log_debug
#else
#define log_lbfs_listxattr(...) do {} while (0)
#endif

#ifdef LOG_LBFS_REMOVEXATTR
#define log_lbfs_removexattr log_debug
#else
#define log_lbfs_removexattr(...) do {} while (0)
#endif

#endif
