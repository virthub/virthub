#ifndef _LOG_IPC_H
#define _LOG_IPC_H

#include "log.h"

#ifdef LOG_IPC_CHECK_ARG
#define log_ipc_check_arg(resource, index) do { \
    log_resource(resource); \
    log_ln(", idx=%d", index); \
} while (0)
#else
#define log_ipc_check_arg(...) do {} while(0)
#endif

#endif
