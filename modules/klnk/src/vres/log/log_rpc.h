#ifndef _LOG_RPC_H
#define _LOG_RPC_H

#ifdef LOG_RPC_GET
#define log_rpc_get(resource) log_resource_info(resource, "op=%s", log_get_op(vres_get_op(resource)))
#else
#define log_rpc_get(...) do {} while (0)
#endif

#ifdef LOG_RPC_PUT
#define log_rpc_put(resource) log_resource_info(resource, "op=%s", log_get_op(vres_get_op(resource)))
#else
#define log_rpc_put(...) do {} while (0)
#endif

#ifdef LOG_RPC_WAIT
#define log_rpc_wait log_resource_info
#else
#define log_rpc_wait(...) do {} while (0)
#endif

#endif
