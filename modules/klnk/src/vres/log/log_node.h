#ifndef _LOG_NODE_H
#define _LOG_NODE_H

#include <arpa/inet.h>
#include "log.h"

#ifdef LOG_NODE_INIT
#define log_node_init(path) log_func("path=%s", path)
#else
#define log_node_init(...) do {} while (0)
#endif

#ifdef LOG_NODE_LIST
#define log_node_list(list, total) do { \
    int i; \
    log("nodes=["); \
    for (i = 0; i < total - 1; i++) \
        log("%s, ", inet_ntoa(list[i])); \
    log_ln("%s]", inet_ntoa(list[i])); \
} while (0)
#else
#define log_node_list(...) do {} while (0)
#endif

#ifdef LOG_NODE_UPDATE
#define log_node_update(path) log_func("path=%s", path)
#else
#define log_node_update(...) do {} while (0)
#endif

#endif
