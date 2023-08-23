#ifndef _KLNK_H
#define _KLNK_H

#include <defaults.h>
#include <vres.h>
#include <ctrl.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "handler.h"
#include "trace.h"
#include "mutex.h"
#include "rpc.h"
#ifdef ENABLE_BARRIER
#include "barrier.h"
#endif

#define KLNK_INTERFACE "io"

#endif
