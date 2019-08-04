#ifndef _LOG_SYNC_H
#define _LOG_SYNC_H

#include "log.h"

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
