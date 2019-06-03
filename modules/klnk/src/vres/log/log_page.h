#ifndef _LOG_PAGE_H
#define _LOG_PAGE_H

#include "log.h"

#ifdef LOG_PAGE_DIFF
#define log_page_diff(diff) do { \
    int i, j; \
    char tmp[LOG_STR_LEN] = {0}; \
    log_str(tmp, "page_diff:\n");
    for (i = 0; i < VRES_PAGE_NR_VERSIONS; i++) { \
        for (j = 0; j < VRES_LINE_MAX; j++) \
            log_str(tmp + strlen(tmp)"%d ", diff[i][j]); \
    } \
    log_ln("%s", tmp); \
} while (0)
#else
#define log_page_diff(...) do {} while (0)
#endif

#ifdef LOG_PAGE_GET
#define log_page_get(resource, path) log_resource_info(resource, "path=%s", path)
#else
#define log_page_get(...) do {} while (0)
#endif

#ifdef LOG_PAGE_PUT
#define log_page_put log_resource_ln
#else
#define log_page_put(...) do {} while (0)
#endif

#ifdef LOG_PAGE_LOCK
#define log_page_lock log_resource_ln
#else
#define log_page_lock(...) do {} while (0)
#endif

#ifdef LOG_PAGE_UNLOCK
#define log_page_unlock log_resource_ln
#else
#define log_page_unlock(...) do {} while (0)
#endif

#endif
