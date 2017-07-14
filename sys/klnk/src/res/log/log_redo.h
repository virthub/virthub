#ifndef _LOG_REDO_H
#define _LOG_REDO_H

#include "log.h"

#ifdef LOG_REDO_REQ
#define log_redo_req(resource, in, index) do { \
	log_resource(resource); \
	log(", rec.index=%d, ", index); \
	if (VRES_OP_SHMFAULT == vres_get_op(resource)) { \
		vres_shmfault_arg_t *arg = (vres_shmfault_arg_t *)in; \
		if (arg && (arg->cmd < VRES_SHM_NR_OPERATIONS)) { \
			log("%s\n", log_get_shmop(arg->cmd)); \
			break; \
		} \
	} \
	log("%s\n", log_get_op(vres_get_op(resource))); \
} while (0)
#else
#define log_redo_req(...) do {} while (0)
#endif

#endif
