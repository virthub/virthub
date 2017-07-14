#ifndef _KLNK_H
#define _KLNK_H

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <vres.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "handler.h"
#include "barrier.h"
#include "mutex.h"
#include "debug.h"
#include "trace.h"
#include "rpc.h"
#include "io.h"

#endif
