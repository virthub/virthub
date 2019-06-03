#ifndef _IPC_H
#define _IPC_H

#include "member.h"
#include "request.h"
#include "resource.h"

#ifdef SHOW_IPC
#define LOG_IPC_GET
#define LOG_IPC_CHECK_ARG
#endif

#include "log_ipc.h"

typedef int(*ipc_init_t)(vres_t *);
typedef int(*ipc_create_t)(vres_t *);

int vres_ipc_put(vres_t *resource);
int vres_ipc_check_arg(vres_arg_t *arg);
int vres_ipc_get(vres_t *resource, ipc_create_t create, ipc_init_t init);

#endif
