#ifndef _LOG_FILE_H
#define _LOG_FILE_H

#include "log.h"

#ifdef LOG_FILE_MKDIR
#define log_file_mkdir(path) log_func("path=%s", path)
#else
#define log_file_mkdir(...) do {} while(0)
#endif

#ifdef LOG_FILE_RMDIR
#define log_file_rmdir(path) log_func("path=%s", path)
#else
#define log_file_rmdir(...) do {} while(0)
#endif

#ifdef LOG_FILE_DIR_PUSH
#define log_file_dir_push(dir, file) log_func("dir=%s, file=%s", dir, file)
#else
#define log_file_dir_push(...) do {} while(0)
#endif

#ifdef LOG_FILE_HANDLE_DIR
#define log_file_handle_dir(path) log_func("path=%s", path)
#else
#define log_file_handle_dir(...) do {} while(0)
#endif

#ifdef LOG_FILE_HANDLE_FILE
#define log_file_handle_file(path) log_func("path=%s", path)
#else
#define log_file_handle_file(...) do {} while(0)
#endif

#ifdef LOG_FILE_DELETE_FILE
#define log_file_delete_file(path) log_func("path=%s", path)
#else
#define log_file_delete_file(...) do {} while(0)
#endif

#endif
