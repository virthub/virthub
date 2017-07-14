#ifndef _LOG_MEMBER_H
#define _LOG_MEMBER_H

#include "log.h"

#ifdef LOG_MEMBER_GET
#define log_member_get(resource) vres_log(resource)
#else
#define log_member_get(...) do {} while (0)
#endif

#endif
