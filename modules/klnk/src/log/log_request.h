#ifndef _LOG_REQUEST_H
#define _LOG_REQUEST_H

#include "log.h"

#ifdef LOG_REQUEST_JOIN
#define log_request_join log_resource_ln
#else
#define log_request_join(...) do {} while (0)
#endif

#endif
