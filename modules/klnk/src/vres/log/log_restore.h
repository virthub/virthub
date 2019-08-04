#ifndef _LOG_RESTORE_H
#define _LOG_RESTORE_H

#include "log.h"

#ifdef LOG_RESTORE
#define log_restore log_resource_ln
#else
#define log_restore(...) do {} while(0)
#endif

#ifdef LOG_RESTORE_FILE
#define log_restore_file(path, len) log_func("path=%s, len=%d", path, len)
#else
#define log_restore_file(...) do {} while (0)
#endif

#ifdef LOG_RESTORE_TASK
#define log_restore_task log_resource_ln
#else
#define log_restore_task(...) do {} while(0)
#endif

#ifdef LOG_RESTORE_RESOURCE
#define log_restore_resource log_resource_ln
#else
#define log_restore_resource(...) do {} while(0)
#endif

#endif
