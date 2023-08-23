#ifndef _NODE_H
#define _NODE_H

#include <vres.h>
#include <time.h>
#include <pthread.h>
#include <hiredis/hiredis.h>

#define NODE_PREFIX   "@"
#define NODE_TIMEOUT  30 // sec
#define NODE_PATH_MAX 256

#ifdef SHOW_NODE
#define LOG_NODE_LIST

#ifdef SHOW_MORE
#define LOG_NODE_INIT
#define LOG_NODE_UPDATE
#endif
#endif

#include "log_node.h"

void vres_node_init();
int vres_node_list(struct in_addr **nodes);

#endif
