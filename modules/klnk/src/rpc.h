#ifndef _RPC_H
#define _RPC_H

#include <vres.h>
#include <ctrl.h>
#include "request.h"
#include "io.h"

#define KLNK_PEER_MAX 64

int klnk_rpc(vres_arg_t *arg);
int klnk_rpc_wait(vres_arg_t *arg);
void klnk_rpc_put(vres_arg_t *arg);
int klnk_rpc_send(vres_arg_t *arg);
int klnk_rpc_send_to_peers(vres_arg_t *arg);
int klnk_rpc_get(vres_t *resource, klnk_request_t *req, vres_arg_t *arg);
int klnk_rpc_check(vres_t *resource, klnk_request_t *req, vres_arg_t *arg);

#endif
