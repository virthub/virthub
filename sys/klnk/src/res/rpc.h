#ifndef _RPC_H
#define _RPC_H

#include <vres.h>
#include "request.h"

int vres_rpc(vres_arg_t *arg);
int vres_rpc_wait(vres_arg_t *arg);
int vres_rpc_get(vres_t *resource, unsigned long addr, size_t inlen, size_t outlen, vres_arg_t *arg);
void vres_rpc_put(vres_arg_t *arg);

#endif
