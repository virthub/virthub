#ifndef _LOG_MATEFS_H
#define _LOG_MATEFS_H

#include "log.h"

#ifdef LOG_MATEFS_OPEN
#define log_matefs_open log_debug
#else
#define log_matefs_open(...) do {} while (0)
#endif

#ifdef LOG_MATEFS_LINK
#define log_matefs_link log_debug
#else
#define log_matefs_link(...) do {} while (0)
#endif

#ifdef LOG_MATEFS_READ
#define log_matefs_read log_debug
#else
#define log_matefs_read(...) do {} while (0)
#endif

#ifdef LOG_MATEFS_CHMOD
#define log_matefs_chmod log_debug
#else
#define log_matefs_chmod(...) do {} while (0)
#endif

#ifdef LOG_MATEFS_CHOWN
#define log_matefs_chown log_debug
#else
#define log_matefs_chown(...) do {} while (0)
#endif

#ifdef LOG_MATEFS_CLOSE
#define log_matefs_close log_debug
#else
#define log_matefs_close(...) do {} while (0)
#endif

#ifdef LOG_MATEFS_MKDIR
#define log_matefs_mkdir log_debug
#else
#define log_matefs_mkdir(...) do {} while (0)
#endif

#ifdef LOG_MATEFS_RMDIR
#define log_matefs_rmdir log_debug
#else
#define log_matefs_rmdir(...) do {} while (0)
#endif

#ifdef LOG_MATEFS_FSYNC
#define log_matefs_fsync log_debug
#else
#define log_matefs_fsync(...) do {} while (0)
#endif

#ifdef LOG_MATEFS_MKNOD
#define log_matefs_mknod log_debug
#else
#define log_matefs_mknod(...) do {} while (0)
#endif

#ifdef LOG_MATEFS_WRITE
#define log_matefs_write log_debug
#else
#define log_matefs_write(...) do {} while (0)
#endif

#ifdef LOG_MATEFS_ACCESS
#define log_matefs_access log_debug
#else
#define log_matefs_access(...) do {} while (0)
#endif

#ifdef LOG_MATEFS_UNLINK
#define log_matefs_unlink log_debug
#else
#define log_matefs_unlink(...) do {} while (0)
#endif

#ifdef LOG_MATEFS_GETATTR
#define log_matefs_getattr log_debug
#else
#define log_matefs_getattr(...) do {} while (0)
#endif

#ifdef LOG_MATEFS_READDIR
#define log_matefs_readdir log_debug
#else
#define log_matefs_readdir(...) do {} while (0)
#endif

#ifdef LOG_MATEFS_RELEASE
#define log_matefs_release log_debug
#else
#define log_matefs_release(...) do {} while (0)
#endif

#ifdef LOG_MATEFS_RENAME
#define log_matefs_rename log_debug
#else
#define log_matefs_rename(...) do {} while (0)
#endif

#ifdef LOG_MATEFS_STATFS
#define log_matefs_statfs log_debug
#else
#define log_matefs_statfs(...) do {} while (0)
#endif

#ifdef LOG_MATEFS_SYMLINK
#define log_matefs_symlink log_debug
#else
#define log_matefs_symlink(...) do {} while (0)
#endif


#ifdef LOG_MATEFS_UTIMENS
#define log_matefs_utimens log_debug
#else
#define log_matefs_utimens(...) do {} while (0)
#endif

#ifdef LOG_MATEFS_GETXATTR
#define log_matefs_getxattr log_debug
#else
#define log_matefs_getxattr(...) do {} while (0)
#endif

#ifdef LOG_MATEFS_READLINK
#define log_matefs_readlink log_debug
#else
#define log_matefs_readlink(...) do {} while (0)
#endif

#ifdef LOG_MATEFS_SETXATTR
#define log_matefs_setxattr log_debug
#else
#define log_matefs_setxattr(...) do {} while (0)
#endif

#ifdef LOG_MATEFS_TRUNCATE
#define log_matefs_truncate log_debug
#else
#define log_matefs_truncate(...) do {} while (0)
#endif

#ifdef LOG_MATEFS_FALLOCATE
#define log_matefs_fallocate log_debug
#else
#define log_matefs_fallocate(...) do {} while (0)
#endif

#ifdef LOG_MATEFS_LISTXATTR
#define log_matefs_listxattr log_debug
#else
#define log_matefs_listxattr(...) do {} while (0)
#endif

#ifdef LOG_MATEFS_REMOVEXATTR
#define log_matefs_removexattr log_debug
#else
#define log_matefs_removexattr(...) do {} while (0)
#endif

#endif
