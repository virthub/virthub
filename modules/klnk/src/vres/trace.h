#ifndef _TRACE_H
#define _TRACE_H

#include <arpa/inet.h>
#include "resource.h"
#include "shm.h"
#include "log.h"

#define VRES_TRACE_INTERVAL 256

void vres_trace(vres_req_t *req);

#endif
