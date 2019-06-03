#ifndef _KLNK_H
#define _KLNK_H

#include <vres.h>

int klnk_call(vres_arg_t *arg);
int klnk_mutex_check(vres_t *resource);
int klnk_call_broadcast(vres_arg_t *arg);
int klnk_io_sync(vres_t *resource, char *in, size_t inlen, char *out, size_t outlen, vres_id_t *dest);
int klnk_io_direct(vres_t *resource, char *in, size_t inlen, char *out, size_t outlen, vres_addr_t addr);
int klnk_io_async(vres_t *resource, char *in, size_t inlen, char *out, size_t outlen, vres_id_t *dest, pthread_t *thread);

#endif
