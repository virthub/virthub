#ifndef _LOG_MON_H
#define _LOG_MON_H

#include "log.h"

#ifdef LOG_MON_REQ_GET
#define log_mon_req_get log_resource_ln
#else
#define log_mon_req_get(...) do {} while (0)
#endif

#ifdef LOG_MON_REQ_PUT
#define log_mon_req_put log_resource_info
#else
#define log_mon_req_put(...) do {} while (0)
#endif

#ifdef LOG_MON_RELEASE
#define log_mon_release log_resource_info
#else
#define log_mon_release(...) do {} while (0)
#endif

#endif