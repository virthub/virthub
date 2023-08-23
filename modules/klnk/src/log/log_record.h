#ifndef _LOG_RECORD_H
#define _LOG_RECORD_H

#include "log.h"

#ifdef LOG_RECORD_SAVE
#define log_record_save(req, name) log_resource_info((&(req)->resource), "rec={path:%s, op:%s}", name, log_get_op(vres_get_op(&(req)->resource)))
#else
#define log_record_save(...) do {} while (0)
#endif

#ifdef LOG_RECORD_REMOVE
#define log_record_remove(path, index, head) do { \
    if (index < 0) \
        log_func("remove %s", path); \
    else \
        log_func("path=%s%d, head=%d", path, index, head); \
} while (0)
#else
#define log_record_remove(...) do {} while (0)
#endif

#ifdef LOG_RECORD_CHECK_EMPTY
#define log_record_check_empty(path, index) do { \
    if (index < 0) \
        log_func("no records found, path=%s", path); \
    else \
        log_func("one record at least, path=%s%d", path, index); \
} while (0)
#else
#define log_record_check_empty(...) do {} while (0)
#endif

#endif
