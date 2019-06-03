#ifndef _LOG_RECORD_H
#define _LOG_RECORD_H

#include "log.h"

#ifdef LOG_RECORD_SAVE
#define log_record_save(req, path) log_resource_info(&(req)->resource, "rec={len:%d, path:%s, op:%s}", (req)->length, path, log_get_op(vres_get_op(&(req)->resource)))
#else
#define log_record_save(...) do {} while (0)
#endif

#endif
