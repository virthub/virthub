#ifndef _IO_H
#define _IO_H

#include <vres.h>
#include "net.h"
#include "call.h"

#define KLNK_IO_MAX			8192
#define KLNK_RETRY_MAX      10
#define KLNK_RETRY_INTERVAL	5000000 // usec

// #define LOG_KLNK_IO_SYNC
#define LOG_KLNK_IO_SYNC_CONNECT
// #define LOG_KLNK_IO_SYNC_SEND
// #define LOG_KLNK_IO_SYNC_OUTPUT

#ifdef LOG_KLNK_IO_SYNC
#define log_klnk_io_sync log_resource_info
#else
#define log_klnk_io_sync(...) do  {} while (0)
#endif

#ifdef LOG_KLNK_IO_SYNC_SEND
#define log_klnk_io_sync_send(resource) log_resource_info(resource, "start to send")
#else
#define log_klnk_io_sync_send(...) do {} while (0)
#endif

#ifdef LOG_KLNK_IO_SYNC_OUTPUT
#define log_klnk_io_sync_output(resource) log_resource_info(resource, "get output")
#else
#define log_klnk_io_sync_output(...) do {} while (0)
#endif

#ifdef LOG_KLNK_IO_SYNC_CONNECT
#define log_klnk_io_sync_connect(resource, peer) log_resource_info(resource, "connect to %s (id=%d)", vres_addr2str(peer.address), peer.id)
#else
#define log_klnk_io_sync_connect(...) do {} while (0)
#endif

int klnk_io_sync(vres_t *resource, char *in, size_t inlen, char *out, size_t outlen, vres_id_t *dest);
int klnk_io_direct(vres_t *resource, char *in, size_t inlen, char *out, size_t outlen, vres_addr_t addr);
int klnk_io_async(vres_t *resource, char *in, size_t inlen, char *out, size_t outlen, vres_id_t *dest, pthread_t *thread);

#endif
