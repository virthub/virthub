#ifndef _LOG_PRIO_H
#define _LOG_PRIO_H

#include "log.h"

#ifdef LOG_PRIO_CREATE
#define log_prio_create(resource, offset) log_resource_info(resource, "t_off=%lld", offset)
#else
#define log_prio_create(...) do {} while (0)
#endif

#ifdef LOG_PRIO_SYNC_TIME
#define log_prio_sync_time(resource, off) log_resource_info(resource, "t_off=%lld", off)
#else
#define log_prio_sync_time(...) do {} while (0)
#endif

#ifdef LOG_PRIO_CHECK
#define log_prio_check log_resource_info
#else
#define log_prio_check(...) do {} while (0)
#endif

#ifdef LOG_PRIO_SELECT
#define log_prio_select(resource, pos) log_resource_info(resource, "pos=%d", pos)
#else
#define log_prio_select(...) do {} while (0)
#endif

#ifdef LOG_PRIO_SET_BUSY
#define log_prio_set_busy log_resource_ln
#else
#define log_prio_set_busy(...) do {} while (0)
#endif

#ifdef LOG_PRIO_SET_IDLE
#define log_prio_set_idle log_resource_ln
#else
#define log_prio_set_idle(...) do {} while (0)
#endif

#endif
