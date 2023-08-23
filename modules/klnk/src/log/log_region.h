#ifndef _LOG_REGION_H
#define _LOG_REGION_H

#include "log.h"

#ifdef LOG_REGION_GET
#define log_region_get(resource, path) log_resource_info(resource, "path=%s", path)
#else
#define log_region_get(...) do {} while (0)
#endif

#ifdef LOG_REGION_PUT
#define log_region_put log_resource_ln
#else
#define log_region_put(...) do {} while (0)
#endif

#ifdef LOG_REGION_LOCK
#define log_region_lock(resource) log_resource_info(resource, ">-- region_lock@start --<")
#else
#define log_region_lock(...) do {} while (0)
#endif

#ifdef LOG_REGION_UNLOCK
#define log_region_unlock(resource) log_resource_info(resource, ">-- region_lock@end --<")
#else
#define log_region_unlock(...) do {} while (0)
#endif

#ifdef LOG_REGION_DIFF
#define log_region_diff log_diff
#else
#define log_region_diff(...) do {} while (0)
#endif

#ifdef LOG_REGION_WRPROTECT
#define log_region_wrprotect log_resource_info
#else
#define log_region_wrprotect(...) do {} while (0)
#endif

#ifdef LOG_REGION_RDPROTECT
#define log_region_rdprotect log_resource_info
#else
#define log_region_rdprotect(...) do {} while (0)
#endif

#ifdef LOG_REGION_ACCESS_PROTECT
#define log_region_access_protect log_resource_info
#else
#define log_region_access_protect(...) do {} while (0)
#endif

#ifdef LOG_REGION_LINE
#define log_region_line(resource, region, chunk, line_num, line) do { \
    int chunk_id; \
    char *p = line; \
    int slice_id = vres_get_slice(resource); \
    vres_slice_t *slice = vres_region_get_slice(resource, region); \
    for (chunk_id = 0; chunk_id < VRES_CHUNK_MAX; chunk_id++) \
        if (&slice->chunks[chunk_id] == chunk) \
            break; \
    assert(chunk_id < VRES_CHUNK_MAX); \
    log_resource_info(resource, "slice_id=%d (0x%lx), chunk_id=%d (0x%lx), line=%d (0x%lx)", slice_id, (unsigned long)slice, chunk_id, (unsigned long)chunk, line_num, (unsigned long)p); \
    log_buf(p, VRES_LINE_SIZE, 1); \
} while (0)
#else
#define log_region_line(...) do {} while (0)
#endif

#endif
