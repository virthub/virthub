#ifndef _KLNK_H
#define _KLNK_H

#define FUSE_USE_VERSION 26

#ifdef UNSHARE
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sched.h>
#endif

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

// #define LOG_KLNK_OPEN

#ifdef LOG_KLNK_OPEN
#define log_klnk_open klnk_log
#else
#define log_klnk_open(...) do  {} while (0)
#endif

#endif
