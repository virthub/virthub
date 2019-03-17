#ifndef _LOG_EVENT_H
#define _LOG_EVENT_H

#include "log.h"

#ifdef LOG_EVENT_CANCEL
#define log_event_cancel(path, index) log_debug("path=%s, idx=%d", path, index)
#else
#define log_event_cancel(...) do {} while(0)
#endif

#endif
