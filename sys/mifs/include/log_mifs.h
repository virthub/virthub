#ifndef _LOG_MIFS_H
#define _LOG_MIFS_H

#include "log.h"

#ifdef LOG_MIFS_OPEN
#define log_mifs_open log_debug
#else
#define log_mifs_open(...) do {} while (0)
#endif

#ifdef LOG_MIFS_LINK
#define log_mifs_link log_debug
#else
#define log_mifs_link(...) do {} while (0)
#endif

#ifdef LOG_MIFS_READ
#define log_mifs_read log_debug
#else
#define log_mifs_read(...) do {} while (0)
#endif

#ifdef LOG_MIFS_CHMOD
#define log_mifs_chmod log_debug
#else
#define log_mifs_chmod(...) do {} while (0)
#endif

#ifdef LOG_MIFS_CHOWN
#define log_mifs_chown log_debug
#else
#define log_mifs_chown(...) do {} while (0)
#endif

#ifdef LOG_MIFS_CLOSE
#define log_mifs_close log_debug
#else
#define log_mifs_close(...) do {} while (0)
#endif

#ifdef LOG_MIFS_MKDIR
#define log_mifs_mkdir log_debug
#else
#define log_mifs_mkdir(...) do {} while (0)
#endif

#ifdef LOG_MIFS_RMDIR
#define log_mifs_rmdir log_debug
#else
#define log_mifs_rmdir(...) do {} while (0)
#endif

#ifdef LOG_MIFS_FSYNC
#define log_mifs_fsync log_debug
#else
#define log_mifs_fsync(...) do {} while (0)
#endif

#ifdef LOG_MIFS_MKNOD
#define log_mifs_mknod log_debug
#else
#define log_mifs_mknod(...) do {} while (0)
#endif

#ifdef LOG_MIFS_WRITE
#define log_mifs_write log_debug
#else
#define log_mifs_write(...) do {} while (0)
#endif

#ifdef LOG_MIFS_ACCESS
#define log_mifs_access log_debug
#else
#define log_mifs_access(...) do {} while (0)
#endif

#ifdef LOG_MIFS_UNLINK
#define log_mifs_unlink log_debug
#else
#define log_mifs_unlink(...) do {} while (0)
#endif

#ifdef LOG_MIFS_GETATTR
#define log_mifs_getattr log_debug
#else
#define log_mifs_getattr(...) do {} while (0)
#endif

#ifdef LOG_MIFS_READDIR
#define log_mifs_readdir log_debug
#else
#define log_mifs_readdir(...) do {} while (0)
#endif

#ifdef LOG_MIFS_RELEASE
#define log_mifs_release log_debug
#else
#define log_mifs_release(...) do {} while (0)
#endif

#ifdef LOG_MIFS_RENAME
#define log_mifs_rename log_debug
#else
#define log_mifs_rename(...) do {} while (0)
#endif

#ifdef LOG_MIFS_STATFS
#define log_mifs_statfs log_debug
#else
#define log_mifs_statfs(...) do {} while (0)
#endif

#ifdef LOG_MIFS_SYMLINK
#define log_mifs_symlink log_debug
#else
#define log_mifs_symlink(...) do {} while (0)
#endif


#ifdef LOG_MIFS_UTIMENS
#define log_mifs_utimens log_debug
#else
#define log_mifs_utimens(...) do {} while (0)
#endif

#ifdef LOG_MIFS_GETXATTR
#define log_mifs_getxattr log_debug
#else
#define log_mifs_getxattr(...) do {} while (0)
#endif

#ifdef LOG_MIFS_READLINK
#define log_mifs_readlink log_debug
#else
#define log_mifs_readlink(...) do {} while (0)
#endif

#ifdef LOG_MIFS_SETXATTR
#define log_mifs_setxattr log_debug
#else
#define log_mifs_setxattr(...) do {} while (0)
#endif

#ifdef LOG_MIFS_TRUNCATE
#define log_mifs_truncate log_debug
#else
#define log_mifs_truncate(...) do {} while (0)
#endif

#ifdef LOG_MIFS_FALLOCATE
#define log_mifs_fallocate log_debug
#else
#define log_mifs_fallocate(...) do {} while (0)
#endif

#ifdef LOG_MIFS_LISTXATTR
#define log_mifs_listxattr log_debug
#else
#define log_mifs_listxattr(...) do {} while (0)
#endif

#ifdef LOG_MIFS_REMOVEXATTR
#define log_mifs_removexattr log_debug
#else
#define log_mifs_removexattr(...) do {} while (0)
#endif

#endif
