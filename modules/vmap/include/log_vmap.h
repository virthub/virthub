#ifndef _LOG_VMAP_H
#define _LOG_VMAP_H

#include "log.h"

#ifdef LOG_VMAP_SETXATTR
#define log_vmap_setxattr(path, name, ret) do { \
    unsigned long owner, area; \
    vmap_path_extract(path, &owner, &area); \
    log_debug("owner=%d, area=%lx, attr=%s, ret=%d", (int)owner, area, name, ret); \
} while(0)
#else
#define log_vmap_setxattr(...) do {} while(0)
#endif

#ifdef LOG_VMAP_READ
#define log_vmap_read(path, size) do { \
    unsigned long owner, area; \
    vmap_path_extract(path, &owner, &area); \
    log_debug("owner=%d, area=%lx, size=%d", (int)owner, area, (int)size); \
} while(0)
#else
#define log_vmap_read(...) do {} while(0)
#endif

#endif
