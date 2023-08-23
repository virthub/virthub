#ifndef _LOG_RESOURCE_H
#define _LOG_RESOURCE_H

#ifdef LOG_LOOKUP
#define log_lookup(resource, fmt, ...) log_func("cls=%s, key=%d, " fmt, log_get_cls((resource)->cls), (resource)->key, ##__VA_ARGS__)
#else
#define log_lookup(...) do {} while(0)
#endif

#ifdef LOG_GET_PEER
#define log_get_peer(resource, fmt, ...) log_func("cls=%s, key=%d, " fmt, log_get_cls((resource)->cls), (resource)->key, ##__VA_ARGS__)
#else
#define log_get_peer(...) do {} while(0)
#endif

#ifdef LOG_SAVE_PEER
#define log_save_peer(resource, fmt, ...) log_func("cls=%s, key=%d, " fmt, log_get_cls((resource)->cls), (resource)->key, ##__VA_ARGS__)
#else
#define log_save_peer(...) do {} while(0)
#endif

#ifdef LOG_JOIN
#define log_join log_resource_info
#else
#define log_join(...) do {} while (0)
#endif

#ifdef LOG_SYNC
#define log_sync(resource, reply) log_resource_info(resource,  "ret=%s", log_get_err(vres_get_errno(reply)))
#else
#define log_sync(...) do {} while (0)
#endif

#endif
