#ifndef _IO_H
#define _IO_H

#include <vres.h>
#include <eval.h>
#include <stdlib.h>
#include <stdbool.h>
#include "resource.h"
#include "net.h"

#define KLNK_IO_MAX         (1 << 20)
#define KLNK_RETRY_MAX      10
#define KLNK_RETRY_INTERVAL 5000000 // usec

typedef struct {
    vres_id_t dest;
    vres_arg_t *arg;
    bool release;
} klnk_arg_t;

int klnk_io_create(pthread_t *thread, vres_id_t dest, vres_arg_t *arg, bool release);
int klnk_io_sync(vres_t *resource, char *in, size_t inlen, char *out, size_t outlen, vres_id_t dest);
int klnk_io_sync_by_addr(vres_t *resource, char *in, size_t inlen, char *out, size_t outlen, vres_addr_t addr);
int klnk_io_async(vres_t *resource, char *in, size_t inlen, char *out, size_t outlen, vres_id_t dest, pthread_t *thread);

#endif
