#ifndef _RPC_H
#define _RPC_H

#include <vres.h>
#include "request.h"

#define KLNK_RPC_MAX 64

int klnk_rpc(vres_arg_t *arg);
int klnk_rpc_wait(vres_arg_t *arg);
void klnk_rpc_put(vres_arg_t *arg);
int klnk_rpc_send(vres_arg_t *arg);
int klnk_rpc_broadcast(vres_arg_t *arg);
int klnk_rpc_get(vres_t *resource, unsigned long addr, size_t inlen, size_t outlen, vres_arg_t *arg);

#endif
