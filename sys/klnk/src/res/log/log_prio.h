#ifndef _LOG_PRIO_H
#define _LOG_PRIO_H

#include "log.h"

#ifdef LOG_PRIO_LOCK
#define log_prio_lock vres_log
#else
#define log_prio_lock(...) do {} while (0)
#endif

#ifdef LOG_PRIO_UNLOCK
#define log_prio_unlock vres_log
#else
#define log_prio_unlock(...) do {} while (0)
#endif

#ifdef LOG_PRIO_CREATE
#define log_prio_create(resource, offset) do { \
	log_owner(resource); \
	log("key=%d, t_off=%lld\n", resource->key, offset); \
} while (0)
#else
#define log_prio_create(...) do {} while (0)
#endif

#ifdef LOG_PRIO_SYNC_TIME
#define log_prio_sync_time(resource, off) do { \
	log_resource(resource); \
	log(", t_off=%lld\n", off); \
} while (0)
#else
#define log_prio_sync_time(...) do {} while (0)
#endif

#ifdef LOG_PRIO_CHECK
#define log_prio_check(req) do { \
	vres_t *resource = &(req)->resource; \
	log_resource(resource); \
	if (VRES_OP_SHMFAULT == vres_get_op(resource)) { \
		vres_shmfault_arg_t *arg = (vres_shmfault_arg_t *)req->buf; \
		log(" [%d]", arg->index);\
	} \
	log("\n"); \
} while (0)
#else
#define log_prio_check(...) do {} while (0)
#endif

#endif
