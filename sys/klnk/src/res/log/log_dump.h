#ifndef _LOG_DUMP_H
#define _LOG_DUMP_H

#include "log.h"

#ifdef LOG_DUMP
#define log_dump(resource) vres_log(resource)
#else
#define log_dump(...) do {} while(0)
#endif

#ifdef LOG_DUMP_FILE
#define log_dump_file(path, len) log_debug("path=%s, len=%d", path, len)
#else
#define log_dump_file(...) do {} while (0)
#endif

#ifdef LOG_DUMP_TASK
#define log_dump_task(resource) vres_log(resource)
#else
#define log_dump_task(...) do {} while(0)
#endif

#ifdef LOG_DUMP_RESOURCE
#define log_dump_resource(resource) vres_log(resource)
#else
#define log_dump_resource(...) do {} while(0)
#endif

#endif
