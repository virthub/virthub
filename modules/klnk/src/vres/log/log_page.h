#ifndef _LOG_PAGE_H
#define _LOG_PAGE_H

#include "log.h"

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
#define log_page_lock(resource) log_resource_info(resource, "page_lock (pgno=%ld) >-- start --<", vres_get_pgno(resource))
#else
#define log_page_lock(...) do {} while (0)
#endif

#ifdef LOG_PAGE_UNLOCK
#define log_page_unlock(resource) log_resource_info(resource, "page_lock (pgno=%ld) >-- end --<", vres_get_pgno(resource))
#else
#define log_page_unlock(...) do {} while (0)
#endif

#ifdef LOG_PAGE_UPDATE_HOLDER_LIST
#define log_page_update_holder_list log_resource_info
#else
#define log_page_update_holder_list(...) do {} while (0)
#endif

#endif
