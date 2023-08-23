#ifndef _CTRL_H
#define _CTRL_H

#include <vres.h>

typedef struct klnk_request {
    vres_cls_t cls;
    vres_key_t key;
    vres_op_t op;
    vres_id_t id;
    vres_val_t val1;
    vres_val_t val2;
    void *buf;
    size_t inlen;
    size_t outlen;
} klnk_request_t;

int klnk_rpc_send(vres_arg_t *arg);
int klnk_rpc_send_to_peers(vres_arg_t *arg);

int klnk_barrier_set(vres_t *resource);
int klnk_barrier_clear(vres_t *resource);

int klnk_io_sync(vres_t *resource, char *in, size_t inlen, char *out, size_t outlen, vres_id_t dest);
int klnk_io_sync_by_addr(vres_t *resource, char *in, size_t inlen, char *out, size_t outlen, vres_addr_t addr);
int klnk_io_async(vres_t *resource, char *in, size_t inlen, char *out, size_t outlen, vres_id_t dest, pthread_t *thread);

#endif
