#ifndef _LOG_DUMP_H
#define _LOG_DUMP_H

#include "log.h"

#ifdef LOG_DUMP
#define log_dump log_resource_ln
#else
#define log_dump(...) do {} while(0)
#endif

#ifdef LOG_DUMP_FILE
#define log_dump_file(path, len) log_func("path=%s, len=%d", path, len)
#else
#define log_dump_file(...) do {} while (0)
#endif

#ifdef LOG_DUMP_TASK
#define log_dump_task log_resource_ln
#else
#define log_dump_task(...) do {} while(0)
#endif

#ifdef LOG_DUMP_RESOURCE
#define log_dump_resource log_resource_ln
#else
#define log_dump_resource(...) do {} while(0)
#endif

#endif
