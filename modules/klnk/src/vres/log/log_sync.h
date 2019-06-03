#ifndef _LOG_SYNC_H
#define _LOG_SYNC_H

#include "log.h"

#ifdef LOG_JOIN
#define log_join log_resource_info
#else
#define log_join(...) do {} while (0)
#endif

#endif
