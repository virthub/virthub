#ifndef _NODE_H
#define _NODE_H

#include <vres.h>
#include <time.h>
#include <pthread.h>
#include <hiredis/hiredis.h>

#define NODE_PREFIX		"@"
#define NODE_TIMEOUT	20 // seconds
#define NODE_PATH_MAX	32

#ifdef SHOW_NODE
#define LOG_NODE_INIT
#define LOG_NODE_LIST
#define LOG_NODE_UPDATE
#endif

#include "log_node.h"

void node_init();
int node_list(struct in_addr **nodes);

#endif
