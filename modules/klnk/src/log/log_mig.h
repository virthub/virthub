#ifndef _LOG_MIG_H
#define _LOG_MIG_H

#include "log.h"

#ifdef LOG_MIG
#define log_mig(resource, path) log_resource_info(resource, "path=%s", path)
#else
#define log_mig(...) do {} while (0)
#endif

#ifdef LOG_MIG_SET
#define log_mig_set log_resource_ln
#else
#define log_mig_set(...) do {} while (0)
#endif

#ifdef LOG_MIG_CREATE
#define log_mig_create log_resource_ln
#else
#define log_mig_create(...) do {} while (0)
#endif

#endif
