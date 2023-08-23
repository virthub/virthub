#ifndef _LOG_EVENT_H
#define _LOG_EVENT_H

#include "log.h"

#ifdef LOG_EVENT_CANCEL
#define log_event_cancel(path, index) log_func("path=%s, index=%d", path, index)
#else
#define log_event_cancel(...) do {} while(0)
#endif

#ifdef LOG_EVENT_WAIT
#define log_event_wait log_resource_entry_debug
#else
#define log_event_wait(...) do {} while (0)
#endif

#ifdef LOG_EVENT_SET
#define log_event_set log_resource_entry_debug
#else
#define log_event_set(...) do {} while (0)
#endif

#endif
