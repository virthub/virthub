#ifndef _LOG_TSK_H
#define _LOG_TSK_H

#include "log.h"

#ifdef LOG_TSK_PUT
#define log_tsk_put log_resource_ln
#else
#define log_tsk_put(...) do {} while(0)
#endif

#ifdef LOG_TSK_RESUME
#define log_tsk_resume(id) log_func("id=%d", id)
#else
#define log_tsk_resume(...) do {} while(0)
#endif

#ifdef LOG_TSK_SUSPEND
#define log_tsk_suspend(id) log_func("id=%d", id)
#else
#define log_tsk_suspend(...) do {} while(0)
#endif

#ifdef LOG_TSK_TSKCTL
#define log_tsk_tskctl(resource, cmd) log_resource_info(resource, "cmd=%d", cmd)
#else
#define log_tsk_tskctl(...) do {} while(0)
#endif

#ifdef LOG_TSK_WAKEUP
#define log_tsk_wakeup(cmd) log_func("cmd=%s", cmd)
#else
#define log_tsk_wakeup(...) do {} while(0)
#endif

#endif
