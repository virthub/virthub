#ifndef _LOG_SYNC_H
#define _LOG_SYNC_H

#include "log.h"

#ifdef LOG_SYNC
#define log_sync vres_log
#else
#define log_sync(...) do {} while (0)
#endif

#endif
