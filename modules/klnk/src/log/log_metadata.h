#ifndef _LOG_METADATA_H
#define _LOG_METADATA_H

#include "log.h"

#ifdef LOG_METADATA_READ
#define log_metadata_read(path) log_func("path=%s", path)
#else
#define log_metadata_read(...) do {} while (0)
#endif

#endif
