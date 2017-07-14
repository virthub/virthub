#ifndef _LOG_TMP_H
#define _LOG_TMP_H

#include "log.h"

#ifdef LOG_TMP_MKDIR
#define log_tmp_mkdir(path) log_debug("path=%s", path)
#else
#define log_tmp_mkdir(...) do {} while(0)
#endif

#ifdef LOG_TMP_RMDIR
#define log_tmp_rmdir(path) log_debug("path=%s", path)
#else
#define log_tmp_rmdir(...) do {} while(0)
#endif

#ifdef LOG_TMP_DIR_PUSH
#define log_tmp_dir_push(dir, file) log_debug("dir=%s, file=%s", dir, file)
#else
#define log_tmp_dir_push(...) do {} while(0)
#endif

#ifdef LOG_TMP_HANDLE_DIR
#define log_tmp_handle_dir(path) log_debug("path=%s", path)
#else
#define log_tmp_handle_dir(...) do {} while(0)
#endif

#ifdef LOG_TMP_HANDLE_FILE
#define log_tmp_handle_file(path) log_debug("path=%s", path)
#else
#define log_tmp_handle_file(...) do {} while(0)
#endif

#ifdef LOG_TMP_DELETE_FILE
#define log_tmp_delete_file(path) log_debug("path=%s", path)
#else
#define log_tmp_delete_file(...) do {} while(0)
#endif

#endif
