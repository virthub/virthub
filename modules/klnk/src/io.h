#ifndef _IO_H
#define _IO_H

#include <vres.h>
#include <eval.h>
#include "net.h"

#define KLNK_IO_MAX         8192
#define KLNK_RETRY_MAX      10
#define KLNK_RETRY_INTERVAL 5000000 // usec

int klnk_io_sync(vres_t *resource, char *in, size_t inlen, char *out, size_t outlen, vres_id_t *dest);
int klnk_io_direct(vres_t *resource, char *in, size_t inlen, char *out, size_t outlen, vres_addr_t addr);
int klnk_io_async(vres_t *resource, char *in, size_t inlen, char *out, size_t outlen, vres_id_t *dest, pthread_t *thread);

#endif
