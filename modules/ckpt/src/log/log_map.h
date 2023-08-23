#ifndef _LOG_MAP_H
#define _LOG_MAP_H

#include "../log.h"

#ifdef LOG_MAP_SETATTR
#define log_map_setattr(path) do { \
    char area[32]; \
    char owner[32]; \
    const char *end; \
    const char *start = path + strlen(path); \
    while (start[-1] != '/') \
        start--; \
    for (end = start; *end != '_'; end++); \
    memcpy(owner, start, end - start); \
    owner[end - start] = '\0'; \
    start = end + 1; \
    strcpy(area, start); \
    log_debug("owner=%s, area=%s", owner, area); \
} while(0)
#else
#define log_map_setattr(...) do {} while(0)
#endif

#ifdef LOG_MAP_ATTACH
#define log_map_attach(path) log_debug("path=%s", path)
#else
#define log_map_attach(...) do {} while (0)
#endif

#ifdef LOG_MAP
#define log_map(name, area, address, len) log_debug("name=%s, area=%lx, address=%lx, len=%d", name, area, address, len)
#else
#define log_map(...) do {} while (0)
#endif

#endif
