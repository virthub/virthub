#ifndef _LOG_RESOURCE_H
#define _LOG_RESOURCE_H

#ifdef LOG_RESOURCE_LOOKUP
#define log_resource_lookup(resource, fmt, ...) log_func("cls=%s, key=%d, " fmt, log_get_cls((resource)->cls), (resource)->key, ##__VA_ARGS__)
#else
#define log_resource_lookup(...) do {} while(0)
#endif

#ifdef LOG_RESOURCE_GET_PEER
#define log_resource_get_peer(resource, fmt, ...) log_func("cls=%s, key=%d, " fmt, log_get_cls((resource)->cls), (resource)->key, ##__VA_ARGS__)
#else
#define log_resource_get_peer(...) do {} while(0)
#endif

#ifdef LOG_RESOURCE_SAVE_PEER
#define log_resource_save_peer(resource, fmt, ...) log_func("cls=%s, key=%d, " fmt, log_get_cls((resource)->cls), (resource)->key, ##__VA_ARGS__)
#else
#define log_resource_save_peer(...) do {} while(0)
#endif

#endif
