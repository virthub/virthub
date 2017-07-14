#ifndef _LOG_MIG_H
#define _LOG_MIG_H

#include "log.h"

#ifdef LOG_MIG
#define log_mig(resource, path) do { \
	log_resource(resource); \
	log(", path=%s\n", path); \
} while (0)
#else
#define log_mig(...) do {} while (0)
#endif

#ifdef LOG_MIG_SET
#define log_mig_set(resource) vres_log(resource)
#else
#define log_mig_set(...) do {} while (0)
#endif

#ifdef LOG_MIG_CREATE
#define log_mig_create(resource) vres_log(resource)
#else
#define log_mig_create(...) do {} while (0)
#endif

#endif
