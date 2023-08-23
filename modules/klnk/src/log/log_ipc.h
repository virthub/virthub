#ifndef _LOG_IPC_H
#define _LOG_IPC_H

#include "log.h"

#ifdef LOG_IPC_CHECK_ARG
#define log_ipc_check_arg(resource, index) log_resource_info(resource, "index=%d", index)
#else
#define log_ipc_check_arg(...) do {} while(0)
#endif

#ifdef LOG_IPC_GET
#define log_ipc_get log_resource_info
#else
#define log_ipc_get(...) do {} while (0)
#endif

#endif
